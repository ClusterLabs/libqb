/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include <config.h>

#include "ringbuffer.h"

//#define CRAZY_DEBUG_PRINTFS 1
#ifdef CRAZY_DEBUG_PRINTFS
#define DEBUG_PRINTF(format, args...)	\
do {				\
	printf(format, ##args);	\
} while(0)
#else
#define DEBUG_PRINTF(format, args...)
#endif /* CRAZY_DEBUG_PRINTFS */

#define ROUNDUP(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

/* the chunk header is two words
 * 1) the chunk data size
 * 2) the magic number
 */
#define RB_CHUNK_HEADER_WORDS 2
#define RB_CHUNK_HEADER_SIZE (sizeof(uint32_t) * RB_CHUNK_HEADER_WORDS)
#define RB_CHUNK_MAGIC		0xAAAAAAAA
#define CHUNK_SIZE_GET(rb, pointer) rb->shared_data[pointer]
#define CHUNK_MAGIC_GET(rb, pointer) rb->shared_data[(pointer + 1) % rb->shared_hdr->size]

#define FDHEAD_INDEX		(rb->shared_hdr->size)
#define FDTAIL_INDEX		(rb->shared_hdr->size + 1)

#define idx_step(idx)					\
do {							\
	if (idx > (rb->shared_hdr->size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->size));	\
	}						\
} while (0)

/*
 * move the write pointer to the next 32 byte boundry
 * write_pt goes in 4 bytes (sizeof(uint32_t))
 */
//#define USE_CACHE_LINE_ALIGNMENT 1
#ifdef USE_CACHE_LINE_ALIGNMENT
#define idx_cache_line_step(idx)	\
do {					\
	if (idx % 8) {			\
		idx += (8 - (idx % 8));	\
	}				\
	if (idx > (rb->shared_hdr->size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->size));	\
	}						\
} while (0)
#else
#define idx_cache_line_step(idx)			\
do {							\
	if (idx > (rb->shared_hdr->size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->size));	\
	}						\
} while (0)
#endif

static void _check_chunk_locked_(qb_ringbuffer_t * rb, uint32_t pointer);
static size_t _qb_rb_space_used_locked_(qb_ringbuffer_t * rb);
static void _qb_rb_chunk_reclaim_locked_(qb_ringbuffer_t * rb);

qb_ringbuffer_t *qb_rb_open(const char *name, size_t size, uint32_t flags)
{
	struct qb_ringbuffer_s *rb = malloc(sizeof(struct qb_ringbuffer_s));
	size_t real_size = ROUNDUP(size, sysconf(_SC_PAGESIZE));
	char path[PATH_MAX];
	int32_t fd_hdr;
	int32_t fd_data;
	uint32_t file_flags = O_RDWR;

	if (flags & QB_RB_FLAG_CREATE) {
		file_flags |= O_CREAT | O_TRUNC;
	}
	/*
	 * Create a shared_hdr memory segment for the header.
	 */
	fd_hdr = qb_util_mmap_file_open(path, name,
					sizeof(struct qb_ringbuffer_shared_s),
					file_flags);
	if (fd_hdr == -1) {
		qb_util_log(LOG_ERR, "couldn't create file for mmap");
		return NULL;
	}

	rb->shared_hdr = mmap(0,
			      sizeof(struct qb_ringbuffer_shared_s),
			      PROT_READ | PROT_WRITE, MAP_SHARED, fd_hdr, 0);

	if (rb->shared_hdr == MAP_FAILED) {
		qb_util_log(LOG_ERR, "couldn't create mmap for header");
		goto cleanup_hdr;
	}

	rb->flags = flags;

	/*
	 * create the semaphore
	 */
	if (flags & QB_RB_FLAG_CREATE) {
		rb->shared_data = NULL;
		/* rb->shared_hdr->size tracks data by ints and not bytes/chars. */
		rb->shared_hdr->size = real_size / sizeof(uint32_t);
		rb->shared_hdr->write_pt = 0;
		rb->shared_hdr->read_pt = 0;
		rb->shared_hdr->count = 0;
		strncpy(rb->shared_hdr->hdr_path, path, PATH_MAX);
	}
	if (my_lock_it_create(rb, flags) == -1) {
		qb_util_log(LOG_ERR, "couldn't get a shared lock %s",
			    strerror(errno));
		goto cleanup_hdr;
	}

	if (my_sem_create(rb, flags) == -1) {
		qb_util_log(LOG_ERR, "couldn't get a semaphore %s",
			    strerror(errno));
		goto cleanup_hdr;
	}

	/* Create the shared_data memory segment for the actual ringbuffer.
	 * They have to be seperate.
	 */
	if (flags & QB_RB_FLAG_CREATE) {
		fd_data = qb_util_mmap_file_open(path,
						 "qb-ringbuffer-XXXXXX",
						 real_size, file_flags);
		strncpy(rb->shared_hdr->data_path, path, PATH_MAX);
	} else {
		fd_data = qb_util_mmap_file_open(path,
						 rb->shared_hdr->data_path,
						 real_size, file_flags);
	}
	if (fd_data == -1) {
		qb_util_log(LOG_ERR, "couldn't create file for mmap");
		goto cleanup_hdr;
	}

	qb_util_log(LOG_INFO,
		    "shm \n size:%zd\n real_size:%zd\n rb->size:%d\n", size,
		    real_size, rb->shared_hdr->size);

	if (qb_util_circular_mmap(fd_data,
				  (void **)&rb->shared_data, real_size) != 0) {
		goto cleanup_data;
	}

	if (flags & QB_RB_FLAG_CREATE) {
		memset(rb->shared_data, 0, real_size);
		rb->shared_data[rb->shared_hdr->size] = 5;
		rb->shared_hdr->ref_count = 1;
	} else {
		my_lock_it(rb);
		rb->shared_hdr->ref_count++;
		my_unlock_it(rb);
	}

	return rb;

cleanup_data:
	close(fd_data);
	if (flags & QB_RB_FLAG_CREATE) {
		unlink(rb->shared_hdr->data_path);
	}

cleanup_hdr:
	close(fd_hdr);
	if (flags & QB_RB_FLAG_CREATE) {
		unlink(rb->shared_hdr->hdr_path);
		my_lock_it_destroy(rb);
		my_sem_destroy(rb);
	}
	if (rb && (rb->shared_hdr != MAP_FAILED && rb->shared_hdr != NULL)) {
		munmap(rb->shared_hdr, sizeof(struct qb_ringbuffer_shared_s));
	}
	free(rb);
	return NULL;
}

void qb_rb_close(qb_ringbuffer_t * rb)
{
	int32_t destroy_it = 0;

	my_lock_it(rb);
	rb->shared_hdr->ref_count--;
	qb_util_log(LOG_INFO, "ref_count:%d", rb->shared_hdr->ref_count);
	if (rb->shared_hdr->ref_count == 0) {
		destroy_it = 1;
	}
	my_unlock_it(rb);

	if (destroy_it) {
		qb_util_log(LOG_INFO, "Destroying ringbuffer");
		my_lock_it_destroy(rb);
		my_sem_destroy(rb);

		unlink(rb->shared_hdr->data_path);
		unlink(rb->shared_hdr->hdr_path);
	}

	munmap(rb->shared_data, rb->shared_hdr->size);
	munmap(rb->shared_hdr, sizeof(struct qb_ringbuffer_shared_s));
	free(rb);
}

static size_t _qb_rb_space_free_locked_(qb_ringbuffer_t * rb)
{
	uint32_t write_size;
	uint32_t read_size;
	size_t space_free = 0;

	write_size = rb->shared_hdr->write_pt;
	// TODO idx_cache_line_step (write_size);
	read_size = rb->shared_hdr->read_pt;

	if (write_size > read_size) {
		space_free =
		    (read_size - write_size + rb->shared_hdr->size) - 1;
	} else if (write_size < read_size) {
		space_free = (read_size - write_size) - 1;
	} else {
		space_free = rb->shared_hdr->size;
	}

	/* word -> bytes */
	return (space_free * sizeof(uint32_t));
}

ssize_t qb_rb_space_free(qb_ringbuffer_t * rb)
{
	size_t space_free;

	if (my_lock_it(rb) == -1) {
		return -1;
	}
	space_free = _qb_rb_space_free_locked_(rb);
	if (my_unlock_it(rb) == -1) {
		/* aarg stuck locked! */
		qb_util_log(LOG_ERR, "failed to unlock ringbuffer lock %s",
			    strerror(errno));
		return -1;
	}
	return space_free;
}

/*
 * TODO write a function that returns the number of chunks
 * in the rb.
 *
 */

static size_t _qb_rb_space_used_locked_(qb_ringbuffer_t * rb)
{
	uint32_t write_size;
	uint32_t read_size;
	size_t space_used;

	write_size = rb->shared_hdr->write_pt;
	read_size = rb->shared_hdr->read_pt;

	if (write_size > read_size) {
		space_used = write_size - read_size;
	} else if (write_size < read_size) {
		space_used =
		    (write_size - read_size + rb->shared_hdr->size) - 1;
	} else {
		space_used = 0;
	}
	/* word -> bytes */
	return (space_used * sizeof(uint32_t));
}

ssize_t qb_rb_space_used(qb_ringbuffer_t * rb)
{
	size_t used;

	if (my_lock_it(rb) == -1) {
		return -1;
	}
	used = _qb_rb_space_used_locked_(rb);
	if (my_unlock_it(rb) == -1) {
		/* aarg stuck locked! */
		qb_util_log(LOG_ERR, "failed to unlock ringbuffer lock %s",
			    strerror(errno));
		return -1;
	}
	return used;
}

void *qb_rb_chunk_writable_alloc(qb_ringbuffer_t * rb, size_t len)
{
	uint32_t idx;

	my_lock_it(rb);
	/*
	 * Reclaim data if we are over writing and we need space
	 */
	if (rb->flags & QB_RB_FLAG_OVERWRITE) {
		while (_qb_rb_space_free_locked_(rb) <
		       (len + RB_CHUNK_HEADER_SIZE + 4)) {
			_qb_rb_chunk_reclaim_locked_(rb);
		}
	} else {
		if (_qb_rb_space_free_locked_(rb) <
		    (len + RB_CHUNK_HEADER_SIZE + 4)) {
			my_unlock_it(rb);
			return NULL;
		}
	}

	idx = rb->shared_hdr->write_pt;
	/*
	 * insert the chunk header
	 */
	rb->shared_data[idx++] = 0;
	idx_step(idx);
	rb->shared_data[idx++] = RB_CHUNK_MAGIC;
	idx_step(idx);

	/*
	 * return a pointer to the begining of the chunk data
	 */
	return (void *)&rb->shared_data[idx];

}

static uint32_t
_qb_rb_chunk_step_to_next_locked_(qb_ringbuffer_t * rb, uint32_t pointer)
{
	uint32_t chunk_size = CHUNK_SIZE_GET(rb, pointer);
	/*
	 * skip over the chunk header
	 */
	pointer += RB_CHUNK_HEADER_WORDS;
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

int32_t qb_rb_chunk_writable_commit(qb_ringbuffer_t * rb, size_t len)
{
	uint32_t old_write_pt = rb->shared_hdr->write_pt;
	/*
	 * commit the magic & chunk_size
	 */
	rb->shared_data[old_write_pt] = len;
	rb->shared_data[old_write_pt + 1] = RB_CHUNK_MAGIC;
	rb->shared_hdr->count++;

	/*
	 * commit the new write pointer
	 */
	rb->shared_hdr->write_pt =
	    _qb_rb_chunk_step_to_next_locked_(rb, rb->shared_hdr->write_pt);

	DEBUG_PRINTF("%s: read: %u, write: %u (was:%u)\n", __func__,
		     rb->shared_hdr->read_pt, rb->shared_hdr->write_pt,
		     old_write_pt);

	if (my_unlock_it(rb) != 0) {
		qb_util_log(LOG_ERR, "failed to unlock ringbuffer lock %s",
			    strerror(errno));
		return -1;
	}

	/*
	 * post the notification to the reader
	 */
	return my_sem_post(rb);
}

ssize_t qb_rb_chunk_write(qb_ringbuffer_t * rb, const void *data, size_t len)
{
	char *dest = qb_rb_chunk_writable_alloc(rb, len);

	if (dest == NULL) {
		return -1;
	}

	/*
	 * copy the data
	 */
	memcpy(dest, data, len);

	qb_rb_chunk_writable_commit(rb, len);

	return len;
}

static void _qb_rb_chunk_reclaim_locked_(qb_ringbuffer_t * rb)
{
	uint32_t old_read_pt = rb->shared_hdr->read_pt;

	if (_qb_rb_space_used_locked_(rb) == 0) {
		return;
	}

	_check_chunk_locked_(rb, rb->shared_hdr->read_pt);

	rb->shared_hdr->read_pt =
	    _qb_rb_chunk_step_to_next_locked_(rb, rb->shared_hdr->read_pt);
	rb->shared_hdr->count--;

	/*
	 * clear the header
	 */
	rb->shared_data[old_read_pt] = 0;
	rb->shared_data[old_read_pt + 1] = 0;

	DEBUG_PRINTF("%s: read: %u (was:%u), write: %u\n", __func__,
		     rb->shared_hdr->read_pt, old_read_pt,
		     rb->shared_hdr->write_pt);
}

void qb_rb_chunk_reclaim(qb_ringbuffer_t * rb)
{
	my_lock_it(rb);
	_qb_rb_chunk_reclaim_locked_(rb);
	my_unlock_it(rb);
}

ssize_t qb_rb_chunk_peek(qb_ringbuffer_t * rb, void **data_out)
{
	uint32_t chunk_size = CHUNK_SIZE_GET(rb, rb->shared_hdr->read_pt);
	uint32_t chunk_magic = CHUNK_MAGIC_GET(rb, rb->shared_hdr->read_pt);

	*data_out =
	    &rb->shared_data[rb->shared_hdr->read_pt + RB_CHUNK_HEADER_WORDS];

	if (chunk_magic != RB_CHUNK_MAGIC) {
		errno = ENODATA;
		return -1;
	} else {
		return chunk_size;
	}
}

ssize_t
qb_rb_chunk_read(qb_ringbuffer_t * rb, void *data_out, size_t len,
		 int32_t timeout)
{
	uint32_t chunk_size;
	int32_t res;

	res = my_sem_timedwait(rb, timeout);
	if (res == -1 && errno != EIDRM) {
		printf("%s:%d %s\n", __func__, __LINE__, strerror(errno));
		return -1;
	}

	if (my_lock_it(rb) == -1) {
		res = errno;
		qb_util_log(LOG_ERR, "could not lock ringbuffer %s",
			    strerror(errno));
		errno = res;
		return -1;
	}
	if (_qb_rb_space_used_locked_(rb) == 0) {
		printf("%s:%d count:%d\n", __func__, __LINE__,
		       rb->shared_hdr->count);
		my_unlock_it(rb);
		errno = ENODATA;
		return -1;
	}

	_check_chunk_locked_(rb, rb->shared_hdr->read_pt);
	chunk_size = CHUNK_SIZE_GET(rb, rb->shared_hdr->read_pt);

	if (len < chunk_size) {
		printf("%s:%d\n", __func__, __LINE__);
		my_unlock_it(rb);
		return 0;
	}

	memcpy(data_out,
	       &rb->shared_data[rb->shared_hdr->read_pt +
				RB_CHUNK_HEADER_WORDS], chunk_size);

	_qb_rb_chunk_reclaim_locked_(rb);
	my_unlock_it(rb);

	return chunk_size;
}

static void print_header(qb_ringbuffer_t * rb)
{
	printf("Ringbuffer: \n");
	if (rb->flags & QB_RB_FLAG_OVERWRITE) {
		printf(" ->OVERWRITE\n");
	} else {
		printf(" ->NORMAL\n");
	}
	printf(" ->write_pt [%d]\n", rb->shared_hdr->write_pt);
	printf(" ->read_pt [%d]\n", rb->shared_hdr->read_pt);
	printf(" ->size [%d words]\n", rb->shared_hdr->size);

	printf(" =>free [%zu bytes]\n", qb_rb_space_free(rb));
	printf(" =>used [%zu bytes]\n", qb_rb_space_used(rb));
}

static void _check_chunk_locked_(qb_ringbuffer_t * rb, uint32_t pointer)
{
	uint32_t chunk_size = CHUNK_SIZE_GET(rb, pointer);
	uint32_t chunk_magic = CHUNK_MAGIC_GET(rb, pointer);

	if (chunk_magic != RB_CHUNK_MAGIC) {
		printf("size: %x\n", chunk_size);
		printf("magic: %x\n", chunk_magic);
		print_header(rb);
		my_unlock_it(rb);
		assert(0);
	}
}

ssize_t qb_rb_write_to_file(qb_ringbuffer_t * rb, int32_t fd)
{
	ssize_t result = 0;
	ssize_t written_size = 0;

	print_header(rb);

	result = write(fd, &rb->shared_hdr->size, sizeof(uint32_t));
	if ((result < 0) || (result != sizeof(uint32_t))) {
		return -1;
	}
	written_size += result;

	result = write(fd, rb->shared_data,
		       rb->shared_hdr->size * sizeof(uint32_t));
	if ((result < 0)
	    || (result != rb->shared_hdr->size * sizeof(uint32_t))) {
		return -1;
	}
	written_size += result;

	/*
	 * store the read & write pointers
	 */
	result +=
	    write(fd, (void *)&rb->shared_hdr->write_pt, sizeof(uint32_t));
	if ((result < 0) || (result != sizeof(uint32_t))) {
		return -1;
	}
	written_size += result;
	result += write(fd, (void *)&rb->shared_hdr->read_pt, sizeof(uint32_t));
	if ((result < 0) || (result != sizeof(uint32_t))) {
		return -1;
	}
	written_size += result;

	return written_size;
}

qb_ringbuffer_t *qb_rb_create_from_file(int32_t fd, uint32_t flags)
{
	ssize_t n_read;
	size_t n_required;
	qb_ringbuffer_t *rb = malloc(sizeof(qb_ringbuffer_t));
	rb->shared_hdr = malloc(sizeof(struct qb_ringbuffer_shared_s));

	rb->flags = flags;

	n_required = sizeof(uint32_t);
	n_read = read(fd, &rb->shared_hdr->size, n_required);
	if (n_read != n_required) {
		fprintf(stderr, "Unable to read fdata header\n");
		return NULL;
	}

	n_required = ((rb->shared_hdr->size + 2) * sizeof(uint32_t));

	if ((rb->shared_data = malloc(n_required)) == NULL) {
		fprintf(stderr, "exhausted virtual memory\n");
		return NULL;
	}
	n_read = read(fd, rb->shared_data, n_required);
	if (n_read < 0) {
		fprintf(stderr, "reading file failed: %s\n", strerror(errno));
		return NULL;
	}

	if (n_read != n_required) {
		printf("Warning: read %lu bytes, but expected %lu\n",
		       (unsigned long)n_read, (unsigned long)n_required);
	}

	rb->shared_hdr->write_pt = rb->shared_data[FDHEAD_INDEX];
	rb->shared_hdr->read_pt = rb->shared_data[FDTAIL_INDEX];

	print_header(rb);

	return rb;
}
