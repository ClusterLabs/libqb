/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of libqb.
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "ringbuffer_int.h"
#include <qb/qbdefs.h>
#include <qb/qbatomic.h>

/*
 * #define CRAZY_DEBUG_PRINTFS 1
 */
#ifdef CRAZY_DEBUG_PRINTFS
#define DEBUG_PRINTF(format, args...)	\
do {				\
	printf(format, ##args);	\
} while(0)
#else
#define DEBUG_PRINTF(format, args...)
#endif /* CRAZY_DEBUG_PRINTFS */

/* the chunk header is two words
 * 1) the chunk data size
 * 2) the magic number
 */
#define QB_RB_CHUNK_HEADER_WORDS 2
#define QB_RB_CHUNK_HEADER_SIZE (sizeof(uint32_t) * QB_RB_CHUNK_HEADER_WORDS)
/*
 * margin is the gap we leave when checking to see if we have enough
 * space for a new chunk.
 * So:
 * qb_rb_space_free() >= QB_RB_CHUNK_MARGIN + new data chunk
 * The extra word size is to allow for non word sized data chunks.
 */
#define QB_RB_CHUNK_MARGIN (sizeof(uint32_t) * (QB_RB_CHUNK_HEADER_WORDS + 1))
#define QB_RB_CHUNK_MAGIC		0xAAAAAAAA
#define QB_RB_CHUNK_SIZE_GET(rb, pointer) \
	rb->shared_data[pointer]
#define QB_RB_CHUNK_MAGIC_GET(rb, pointer) \
	rb->shared_data[(pointer + 1) % rb->shared_hdr->word_size]

#define QB_RB_WRITE_PT_INDEX (rb->shared_hdr->word_size)
#define QB_RB_READ_PT_INDEX (rb->shared_hdr->word_size + 1)

#define idx_step(idx)					\
do {							\
	if (idx > (rb->shared_hdr->word_size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->word_size));	\
	}						\
} while (0)

/*
 * move the write pointer to the next 32 byte boundary
 * write_pt goes in 4 bytes (sizeof(uint32_t))
 */
/*
 * #define USE_CACHE_LINE_ALIGNMENT 1
 */
#ifdef USE_CACHE_LINE_ALIGNMENT
#define idx_cache_line_step(idx)	\
do {					\
	if (idx % 8) {			\
		idx += (8 - (idx % 8));	\
	}				\
	if (idx > (rb->shared_hdr->word_size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->word_size));	\
	}						\
} while (0)
#else
#define idx_cache_line_step(idx)			\
do {							\
	if (idx > (rb->shared_hdr->word_size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->word_size));	\
	}						\
} while (0)
#endif

static void qb_rb_chunk_check(struct qb_ringbuffer_s * rb, uint32_t pointer);

qb_ringbuffer_t *
qb_rb_open(const char *name, size_t size, uint32_t flags,
	   size_t shared_user_data_size)
{
	struct qb_ringbuffer_s *rb;
	size_t real_size;
	size_t shared_size;
	char path[PATH_MAX];
	int32_t fd_hdr;
	int32_t fd_data;
	uint32_t file_flags = O_RDWR;
	char filename[PATH_MAX];
	int32_t error = 0;
	void *shm_addr;
	long page_size = sysconf(_SC_PAGESIZE);

#ifdef QB_FORCE_SHM_ALIGN
	page_size = QB_MAX(page_size, 16 * 1024);
#endif /* QB_FORCE_SHM_ALIGN */
	real_size = QB_ROUNDUP(size, page_size);
	shared_size =
	    sizeof(struct qb_ringbuffer_shared_s) + shared_user_data_size;

	if (flags & QB_RB_FLAG_CREATE) {
		file_flags |= O_CREAT | O_TRUNC;
	}

	rb = calloc(1, sizeof(struct qb_ringbuffer_s));
	if (rb == NULL) {
		return NULL;
	}

	/*
	 * Create a shared_hdr memory segment for the header.
	 */
	snprintf(filename, PATH_MAX, "qb-%s-header", name);
	fd_hdr = qb_sys_mmap_file_open(path, filename,
				       shared_size, file_flags);
	if (fd_hdr < 0) {
		error = fd_hdr;
		qb_util_log(LOG_ERR, "couldn't create file for mmap");
		goto cleanup_hdr;
	}

	rb->shared_hdr = mmap(0,
			      shared_size,
			      PROT_READ | PROT_WRITE, MAP_SHARED, fd_hdr, 0);

	if (rb->shared_hdr == MAP_FAILED) {
		error = -errno;
		qb_util_log(LOG_ERR, "couldn't create mmap for header");
		goto cleanup_hdr;
	}

	rb->flags = flags;

	/*
	 * create the semaphore
	 */
	if (flags & QB_RB_FLAG_CREATE) {
		rb->shared_data = NULL;
		/* rb->shared_hdr->word_size tracks data by ints and not bytes/chars. */
		rb->shared_hdr->word_size = real_size / sizeof(uint32_t);
		rb->shared_hdr->write_pt = 0;
		rb->shared_hdr->read_pt = 0;
		(void)strlcpy(rb->shared_hdr->hdr_path, path, PATH_MAX);
	}
	error = qb_rb_sem_create(rb, flags);
	if (error < 0) {
		qb_util_perror(LOG_ERR, "couldn't get a semaphore");
		goto cleanup_hdr;
	}

	/* Create the shared_data memory segment for the actual ringbuffer.
	 * They have to be separate.
	 */
	if (flags & QB_RB_FLAG_CREATE) {
		snprintf(filename, PATH_MAX, "qb-%s-data", name);
		fd_data = qb_sys_mmap_file_open(path,
						filename,
						real_size, file_flags);
		(void)strlcpy(rb->shared_hdr->data_path, path, PATH_MAX);
	} else {
		fd_data = qb_sys_mmap_file_open(path,
						rb->shared_hdr->data_path,
						real_size, file_flags);
	}
	if (fd_data < 0) {
		error = fd_data;
		qb_util_log(LOG_ERR, "couldn't create file for mmap");
		goto cleanup_hdr;
	}

	qb_util_log(LOG_DEBUG,
		    "shm size:%zd; real_size:%zd; rb->word_size:%d", size,
		    real_size, rb->shared_hdr->word_size);

	error = qb_sys_circular_mmap(fd_data, &shm_addr, real_size);
	rb->shared_data = shm_addr;
	if (error != 0) {
		qb_util_log(LOG_ERR, "couldn't create circular mmap on %s",
			    rb->shared_hdr->data_path);
		goto cleanup_data;
	}

	if (flags & QB_RB_FLAG_CREATE) {
		memset(rb->shared_data, 0, real_size);
		rb->shared_data[rb->shared_hdr->word_size] = 5;
		rb->shared_hdr->ref_count = 1;
	} else {
		qb_atomic_int_inc(&rb->shared_hdr->ref_count);
	}

	close(fd_hdr);
	close(fd_data);
	return rb;

cleanup_data:
	close(fd_data);
	if (flags & QB_RB_FLAG_CREATE) {
		unlink(rb->shared_hdr->data_path);
	}

cleanup_hdr:
	if (fd_hdr >= 0) {
		close(fd_hdr);
	}
	if (rb && (flags & QB_RB_FLAG_CREATE)) {
		unlink(rb->shared_hdr->hdr_path);
		(void)rb->sem_destroy_fn(rb);
	}
	if (rb && (rb->shared_hdr != MAP_FAILED && rb->shared_hdr != NULL)) {
		munmap(rb->shared_hdr, sizeof(struct qb_ringbuffer_shared_s));
	}
	free(rb);
	errno = -error;
	return NULL;
}

void
qb_rb_close(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return;
	}

	(void)qb_atomic_int_dec_and_test(&rb->shared_hdr->ref_count);
	if (rb->flags & QB_RB_FLAG_CREATE) {
		(void)rb->sem_destroy_fn(rb);
		unlink(rb->shared_hdr->data_path);
		unlink(rb->shared_hdr->hdr_path);
		qb_util_log(LOG_DEBUG,
			    "Free'ing ringbuffer: %s",
			    rb->shared_hdr->hdr_path);
	} else {
		qb_util_log(LOG_DEBUG,
			    "Closing ringbuffer: %s", rb->shared_hdr->hdr_path);
	}
	munmap(rb->shared_data, (rb->shared_hdr->word_size * sizeof(uint32_t)) << 1);
	munmap(rb->shared_hdr, sizeof(struct qb_ringbuffer_shared_s));
	free(rb);
}

void
qb_rb_force_close(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return;
	}

	(void)rb->sem_destroy_fn(rb);
	unlink(rb->shared_hdr->data_path);
	unlink(rb->shared_hdr->hdr_path);
	qb_util_log(LOG_DEBUG,
		    "Force free'ing ringbuffer: %s",
		    rb->shared_hdr->hdr_path);
	munmap(rb->shared_data, (rb->shared_hdr->word_size * sizeof(uint32_t)) << 1);
	munmap(rb->shared_hdr, sizeof(struct qb_ringbuffer_shared_s));
	free(rb);
}

char *
qb_rb_name_get(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return NULL;
	}
	return rb->shared_hdr->hdr_path;
}

void *
qb_rb_shared_user_data_get(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return NULL;
	}
	return rb->shared_hdr->user_data;
}

int32_t
qb_rb_refcount_get(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return -EINVAL;
	}
	return qb_atomic_int_get(&rb->shared_hdr->ref_count);
}

ssize_t
qb_rb_space_free(struct qb_ringbuffer_s * rb)
{
	uint32_t write_size;
	uint32_t read_size;
	size_t space_free = 0;

	if (rb == NULL) {
		return -EINVAL;
	}
	write_size = rb->shared_hdr->write_pt;
	/*
	 * TODO idx_cache_line_step (write_size);
	 */
	read_size = rb->shared_hdr->read_pt;

	if (write_size > read_size) {
		space_free =
		    (read_size - write_size + rb->shared_hdr->word_size) - 1;
	} else if (write_size < read_size) {
		space_free = (read_size - write_size) - 1;
	} else {
		space_free = rb->shared_hdr->word_size;
	}

	/* word -> bytes */
	return (space_free * sizeof(uint32_t));
}

ssize_t
qb_rb_space_used(struct qb_ringbuffer_s * rb)
{
	uint32_t write_size;
	uint32_t read_size;
	size_t space_used;

	if (rb == NULL) {
		return -EINVAL;
	}
	write_size = rb->shared_hdr->write_pt;
	read_size = rb->shared_hdr->read_pt;

	if (write_size > read_size) {
		space_used = write_size - read_size;
	} else if (write_size < read_size) {
		space_used =
		    (write_size - read_size + rb->shared_hdr->word_size) - 1;
	} else {
		space_used = 0;
	}
	/* word -> bytes */
	return (space_used * sizeof(uint32_t));
}

ssize_t
qb_rb_chunks_used(struct qb_ringbuffer_s *rb)
{
	if (rb == NULL) {
		return -EINVAL;
	}
	return rb->sem_getvalue_fn(rb);
}

void *
qb_rb_chunk_alloc(struct qb_ringbuffer_s * rb, size_t len)
{
	uint32_t write_pt;

	if (rb == NULL) {
		return NULL;
	}
	/*
	 * Reclaim data if we are over writing and we need space
	 */
	if (rb->flags & QB_RB_FLAG_OVERWRITE) {
		while (qb_rb_space_free(rb) < (len + QB_RB_CHUNK_MARGIN)) {
			qb_rb_chunk_reclaim(rb);
		}
	} else {
		if (qb_rb_space_free(rb) < (len + QB_RB_CHUNK_MARGIN)) {
			errno = EAGAIN;
			return NULL;
		}
	}

	write_pt = rb->shared_hdr->write_pt;
	/*
	 * insert the chunk header
	 */
	rb->shared_data[write_pt++] = 0;
	idx_step(write_pt);
	rb->shared_data[write_pt++] = QB_RB_CHUNK_MAGIC;
	idx_step(write_pt);

	/*
	 * return a pointer to the beginning of the chunk data
	 */
	return (void *)&rb->shared_data[write_pt];

}

static uint32_t
qb_rb_chunk_step(struct qb_ringbuffer_s * rb, uint32_t pointer)
{
	uint32_t chunk_size = QB_RB_CHUNK_SIZE_GET(rb, pointer);
	/*
	 * skip over the chunk header
	 */
	pointer += QB_RB_CHUNK_HEADER_WORDS;
	idx_step(pointer);

	/*
	 * skip over the user's data.
	 */
	pointer += (chunk_size / sizeof(uint32_t));
	/* make allowance for non-word sizes */
	if ((chunk_size % sizeof(uint32_t)) != 0) {
		pointer++;
	}

	idx_cache_line_step(pointer);
	return pointer;
}

int32_t
qb_rb_chunk_commit(struct qb_ringbuffer_s * rb, size_t len)
{
	uint32_t old_write_pt;

	if (rb == NULL) {
		return -EINVAL;
	}
	/*
	 * commit the magic & chunk_size
	 */
	old_write_pt = rb->shared_hdr->write_pt;
	rb->shared_data[old_write_pt] = len;
	rb->shared_data[old_write_pt + 1] = QB_RB_CHUNK_MAGIC;

	/*
	 * commit the new write pointer
	 */
	rb->shared_hdr->write_pt = qb_rb_chunk_step(rb, old_write_pt);

	DEBUG_PRINTF("%s: read: %u, write: %u (was:%u)\n", __func__,
		     rb->shared_hdr->read_pt, rb->shared_hdr->write_pt,
		     old_write_pt);

	/*
	 * post the notification to the reader
	 */
	return rb->sem_post_fn(rb);
}

ssize_t
qb_rb_chunk_write(struct qb_ringbuffer_s * rb, const void *data, size_t len)
{
	char *dest = qb_rb_chunk_alloc(rb, len);
	int32_t res = 0;

	if (rb == NULL) {
		return -EINVAL;
	}

	if (dest == NULL) {
		return -errno;
	}

	memcpy(dest, data, len);

	res = qb_rb_chunk_commit(rb, len);
	if (res < 0) {
		return res;
	}

	return len;
}

void
qb_rb_chunk_reclaim(struct qb_ringbuffer_s * rb)
{
	uint32_t old_read_pt;

	if (rb == NULL || qb_rb_space_used(rb) == 0) {
		return;
	}
	old_read_pt = rb->shared_hdr->read_pt;
	qb_rb_chunk_check(rb, old_read_pt);

	rb->shared_hdr->read_pt = qb_rb_chunk_step(rb, old_read_pt);

	/*
	 * clear the header
	 */
	rb->shared_data[old_read_pt] = 0;
	rb->shared_data[old_read_pt + 1] = 0;

	DEBUG_PRINTF("%s: read: %u (was:%u), write: %u\n", __func__,
		     rb->shared_hdr->read_pt, old_read_pt,
		     rb->shared_hdr->write_pt);
}

ssize_t
qb_rb_chunk_peek(struct qb_ringbuffer_s * rb, void **data_out, int32_t timeout)
{
	uint32_t read_pt;
	uint32_t chunk_size;
	uint32_t chunk_magic;
	int32_t res;

	if (rb == NULL) {
		return -EINVAL;
	}
	res = rb->sem_timedwait_fn(rb, timeout);
	if (res < 0 && res != -EIDRM) {
		if (res != -ETIMEDOUT) {
			qb_util_perror(LOG_ERR, "sem_timedwait");
		}
		return res;
	}
	read_pt = rb->shared_hdr->read_pt;
	chunk_size = QB_RB_CHUNK_SIZE_GET(rb, read_pt);
	chunk_magic = QB_RB_CHUNK_MAGIC_GET(rb, read_pt);
	*data_out = &rb->shared_data[read_pt + QB_RB_CHUNK_HEADER_WORDS];

	if (chunk_magic != QB_RB_CHUNK_MAGIC) {
		errno = ENOMSG;
		return 0;
	} else {
		return chunk_size;
	}
}

ssize_t
qb_rb_chunk_read(struct qb_ringbuffer_s * rb, void *data_out, size_t len,
		 int32_t timeout)
{
	uint32_t read_pt;
	uint32_t chunk_size;
	int32_t res = 0;

	if (rb == NULL) {
		return -EINVAL;
	}
	if (rb->sem_timedwait_fn) {
		res = rb->sem_timedwait_fn(rb, timeout);
	}
	if (res < 0 && res != -EIDRM) {
		if (res != -ETIMEDOUT) {
			qb_util_perror(LOG_ERR, "sem_timedwait");
		}
		return res;
	}

	if (qb_rb_space_used(rb) == 0) {
		return -ENOMSG;
	}

	read_pt = rb->shared_hdr->read_pt;
	qb_rb_chunk_check(rb, read_pt);
	chunk_size = QB_RB_CHUNK_SIZE_GET(rb, read_pt);

	if (len < chunk_size) {
		qb_util_log(LOG_ERR,
			    "trying to recv chunk of size %d but %d available",
			    len, chunk_size);
		return -ENOBUFS;
	}

	memcpy(data_out,
	       &rb->shared_data[read_pt + QB_RB_CHUNK_HEADER_WORDS],
	       chunk_size);

	qb_rb_chunk_reclaim(rb);

	return chunk_size;
}

static void
print_header(struct qb_ringbuffer_s * rb)
{
	printf("Ringbuffer: \n");
	if (rb->flags & QB_RB_FLAG_OVERWRITE) {
		printf(" ->OVERWRITE\n");
	} else {
		printf(" ->NORMAL\n");
	}
	printf(" ->write_pt [%d]\n", rb->shared_hdr->write_pt);
	printf(" ->read_pt [%d]\n", rb->shared_hdr->read_pt);
	printf(" ->size [%d words]\n", rb->shared_hdr->word_size);
#ifndef S_SPLINT_S
	printf(" =>free [%zu bytes]\n", qb_rb_space_free(rb));
	printf(" =>used [%zu bytes]\n", qb_rb_space_used(rb));
#endif /* S_SPLINT_S */
}

static void
qb_rb_chunk_check(struct qb_ringbuffer_s * rb, uint32_t pointer)
{
	uint32_t chunk_size;
	uint32_t chunk_magic = QB_RB_CHUNK_MAGIC_GET(rb, pointer);

	if (chunk_magic != QB_RB_CHUNK_MAGIC) {
		chunk_size = QB_RB_CHUNK_SIZE_GET(rb, pointer);
		printf("size: %x\n", chunk_size);
		printf("magic: %x\n", chunk_magic);
		print_header(rb);
		assert(0);
	}
}

ssize_t
qb_rb_write_to_file(struct qb_ringbuffer_s * rb, int32_t fd)
{
	ssize_t result;
	ssize_t written_size = 0;

	if (rb == NULL) {
		return -EINVAL;
	}
	print_header(rb);

	result = write(fd, &rb->shared_hdr->word_size, sizeof(uint32_t));
	if (result != sizeof(uint32_t)) {
		return -errno;
	}
	written_size += result;

	result = write(fd, rb->shared_data,
		       rb->shared_hdr->word_size * sizeof(uint32_t));
	if (result != rb->shared_hdr->word_size * sizeof(uint32_t)) {
		return -errno;
	}
	written_size += result;

	/*
	 * store the read & write pointers
	 */
	result = write(fd, (void *)&rb->shared_hdr->write_pt, sizeof(uint32_t));
	if (result != sizeof(uint32_t)) {
		return -errno;
	}
	written_size += result;
	result = write(fd, (void *)&rb->shared_hdr->read_pt, sizeof(uint32_t));
	if (result != sizeof(uint32_t)) {
		return -errno;
	}
	written_size += result;

	qb_util_log(LOG_DEBUG, " writing total of: %zd\n", written_size);

	return written_size;
}

qb_ringbuffer_t *
qb_rb_create_from_file(int32_t fd, uint32_t flags)
{
	ssize_t n_read;
	size_t n_required;
	size_t total_read = 0;
	uint32_t read_pt;
	uint32_t write_pt;
	struct qb_ringbuffer_s *rb;

	if (fd < 0) {
		return NULL;
	}
	rb = calloc(1, sizeof(struct qb_ringbuffer_s));
	if (rb == NULL) {
		return NULL;
	}
	rb->shared_hdr = calloc(1, sizeof(struct qb_ringbuffer_shared_s));
	if (rb->shared_hdr == NULL) {
		goto cleanup_fail2;
	}

	rb->flags = flags;

	n_required = sizeof(uint32_t);
	n_read = read(fd, &rb->shared_hdr->word_size, n_required);
	if (n_read != n_required) {
		qb_util_perror(LOG_ERR, "Unable to read blackbox file header");
		goto cleanup_fail;
	}
	total_read += n_read;

	n_required = (rb->shared_hdr->word_size * sizeof(uint32_t));

	if ((rb->shared_data = malloc(n_required)) == NULL) {
		qb_util_perror(LOG_ERR, "exhausted virtual memory");
		goto cleanup_fail;
	}
	n_read = read(fd, rb->shared_data, n_required);
	if (n_read < 0) {
		qb_util_perror(LOG_ERR, "Unable to read blackbox file data");
		goto cleanup_fail;
	}
	total_read += n_read;

	if (n_read != n_required) {
		qb_util_log(LOG_WARNING, "read %zd bytes, but expected %zu",
			    n_read, n_required);
		goto cleanup_fail;
	}

	n_read = read(fd, &write_pt, sizeof(uint32_t));
	assert(n_read == sizeof(uint32_t));
	rb->shared_hdr->write_pt = write_pt;
	total_read += n_read;

	n_read = read(fd, &read_pt, sizeof(uint32_t));
	assert(n_read == sizeof(uint32_t));
	rb->shared_hdr->read_pt = read_pt;
	total_read += n_read;

	qb_util_log(LOG_DEBUG, "read total of: %zd", total_read);
	print_header(rb);

	return rb;

cleanup_fail:
	free(rb->shared_hdr);
cleanup_fail2:
	free(rb);
	return NULL;
}

int32_t
qb_rb_chown(struct qb_ringbuffer_s * rb, uid_t owner, gid_t group)
{
	int32_t res;

	if (rb == NULL) {
		return -EINVAL;
	}
	res = chown(rb->shared_hdr->data_path, owner, group);
	if (res < 0) {
		return -errno;
	}
	res = chown(rb->shared_hdr->hdr_path, owner, group);
	if (res < 0) {
		return -errno;
	}
	return 0;
}
