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
#include "atomic_int.h"

#define QB_RB_FILE_HEADER_VERSION 1

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

/*
 * move the write pointer to the next 128 byte boundary
 * write_pt goes in 4 bytes (sizeof(uint32_t))
 * #define USE_CACHE_LINE_ALIGNMENT 1
 */
#ifdef USE_CACHE_LINE_ALIGNMENT
#define QB_CACHE_LINE_SIZE 128
#define QB_CACHE_LINE_WORDS (QB_CACHE_LINE_SIZE/sizeof(uint32_t))
#define idx_cache_line_step(idx)	\
do {					\
	if (idx % QB_CACHE_LINE_WORDS) {			\
		idx += (QB_CACHE_LINE_WORDS - (idx % QB_CACHE_LINE_WORDS));	\
	}				\
	if (idx > (rb->shared_hdr->word_size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->word_size));	\
	}						\
} while (0)
#else
#define QB_CACHE_LINE_SIZE 0
#define QB_CACHE_LINE_WORDS 0
#define idx_cache_line_step(idx)			\
do {							\
	if (idx > (rb->shared_hdr->word_size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->word_size));	\
	}						\
} while (0)
#endif


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
 * QB_CACHE_LINE_WORDS is to make sure we have space to align the
 * chunk.
 */
#define QB_RB_WORD_ALIGN 1
#define QB_RB_CHUNK_MARGIN (sizeof(uint32_t) * (QB_RB_CHUNK_HEADER_WORDS +\
						QB_RB_WORD_ALIGN +\
						QB_CACHE_LINE_WORDS))
#define QB_RB_CHUNK_MAGIC		0xA1A1A1A1
#define QB_RB_CHUNK_MAGIC_DEAD		0xD0D0D0D0
#define QB_RB_CHUNK_MAGIC_ALLOC		0xA110CED0
#define QB_RB_CHUNK_SIZE_GET(rb, pointer) rb->shared_data[pointer]
#define QB_RB_CHUNK_MAGIC_GET(rb, pointer) \
	qb_atomic_int_get_ex((int32_t*)&rb->shared_data[(pointer + 1) % rb->shared_hdr->word_size], \
                             QB_ATOMIC_ACQUIRE)
#define QB_RB_CHUNK_MAGIC_SET(rb, pointer, new_val) \
	qb_atomic_int_set_ex((int32_t*)&rb->shared_data[(pointer + 1) % rb->shared_hdr->word_size], \
			     new_val, QB_ATOMIC_RELEASE)
#define QB_RB_CHUNK_DATA_GET(rb, pointer) \
	&rb->shared_data[(pointer + QB_RB_CHUNK_HEADER_WORDS) % rb->shared_hdr->word_size]

#define QB_MAGIC_ASSERT(_ptr_) \
do {							\
	uint32_t chunk_magic = QB_RB_CHUNK_MAGIC_GET(rb, _ptr_); \
	if (chunk_magic != QB_RB_CHUNK_MAGIC) print_header(rb); \
	assert(chunk_magic == QB_RB_CHUNK_MAGIC); \
} while (0)

#define idx_step(idx)					\
do {							\
	if (idx > (rb->shared_hdr->word_size - 1)) {		\
		idx = ((idx) % (rb->shared_hdr->word_size));	\
	}						\
} while (0)

static void print_header(struct qb_ringbuffer_s * rb);
static int _rb_chunk_reclaim(struct qb_ringbuffer_s * rb);

qb_ringbuffer_t *
qb_rb_open(const char *name, size_t size, uint32_t flags,
	   size_t shared_user_data_size)
{
	return qb_rb_open_2(name, size, flags, shared_user_data_size, NULL);
}

qb_ringbuffer_t *
qb_rb_open_2(const char *name, size_t size, uint32_t flags,
	     size_t shared_user_data_size,
	     struct qb_rb_notifier *notifiers)
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

#ifdef QB_ARCH_HPPA
	page_size = QB_MAX(page_size, 0x00400000); /* align to page colour */
#elif defined(QB_FORCE_SHM_ALIGN)
	page_size = QB_MAX(page_size, 16 * 1024);
#endif /* QB_FORCE_SHM_ALIGN */
	/* The user of this api expects the 'size' parameter passed into this function
	 * to be reflective of the max size single write we can do to the 
	 * ringbuffer.  This means we have to add both the 'margin' space used
	 * to calculate if there is enough space for a new chunk as well as the '+1' that
	 * prevents overlap of the read/write pointers */
	size += QB_RB_CHUNK_MARGIN + 1;
	real_size = QB_ROUNDUP(size, page_size);

	shared_size =
	    sizeof(struct qb_ringbuffer_shared_s) + shared_user_data_size;

	if (flags & QB_RB_FLAG_CREATE) {
		file_flags |= O_CREAT | O_TRUNC | O_EXCL;
	}

	rb = calloc(1, sizeof(struct qb_ringbuffer_s));
	if (rb == NULL) {
		return NULL;
	}

	/*
	 * Create a shared_hdr memory segment for the header.
	 */
	snprintf(filename, PATH_MAX, "%s-header", name);
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
	qb_atomic_init();

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
	if (notifiers && notifiers->post_fn) {
		error = 0;
		memcpy(&rb->notifier,
		       notifiers,
		       sizeof(struct qb_rb_notifier));
	} else {
		error = qb_rb_sem_create(rb, flags);
	}
	if (error < 0) {
		errno = -error;
		qb_util_perror(LOG_ERR, "couldn't create a semaphore");
		goto cleanup_hdr;
	}

	/* Create the shared_data memory segment for the actual ringbuffer.
	 * They have to be separate.
	 */
	if (flags & QB_RB_FLAG_CREATE) {
		snprintf(filename, PATH_MAX, "%s-data", name);
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
		    "shm size:%ld; real_size:%ld; rb->word_size:%d", size,
		    real_size, rb->shared_hdr->word_size);

	/* this function closes fd_data */
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
	return rb;

cleanup_data:
	if (flags & QB_RB_FLAG_CREATE) {
		unlink(rb->shared_hdr->data_path);
	}

cleanup_hdr:
	if (fd_hdr >= 0) {
		close(fd_hdr);
	}
	if (rb && (rb->shared_hdr != MAP_FAILED) && (flags & QB_RB_FLAG_CREATE)) {
		unlink(rb->shared_hdr->hdr_path);
		if (rb->notifier.destroy_fn) {
			(void)rb->notifier.destroy_fn(rb->notifier.instance);
		}
	}
	if (rb && (rb->shared_hdr != MAP_FAILED)) {
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
	qb_enter();

	(void)qb_atomic_int_dec_and_test(&rb->shared_hdr->ref_count);
	(void)qb_rb_close_helper(rb, rb->flags & QB_RB_FLAG_CREATE, QB_FALSE);
}

void
qb_rb_force_close(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return;
	}
	qb_enter();

	qb_atomic_int_set(&rb->shared_hdr->ref_count, -1);
	(void)qb_rb_close_helper(rb, QB_TRUE, QB_TRUE);
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
	if (rb->notifier.space_used_fn) {
		return (rb->shared_hdr->word_size * sizeof(uint32_t)) -
			rb->notifier.space_used_fn(rb->notifier.instance);
	}
	write_size = rb->shared_hdr->write_pt;
	read_size = rb->shared_hdr->read_pt;

	if (write_size > read_size) {
		space_free =
		    (read_size - write_size + rb->shared_hdr->word_size) - 1;
	} else if (write_size < read_size) {
		space_free = (read_size - write_size) - 1;
	} else {
		if (rb->notifier.q_len_fn && rb->notifier.q_len_fn(rb->notifier.instance) > 0) {
			space_free = 0;
		} else {
			space_free = rb->shared_hdr->word_size;
		}
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
	if (rb->notifier.space_used_fn) {
		return rb->notifier.space_used_fn(rb->notifier.instance);
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
	if (rb->notifier.q_len_fn) {
		return rb->notifier.q_len_fn(rb->notifier.instance);
	}
	return -ENOTSUP;
}

void *
qb_rb_chunk_alloc(struct qb_ringbuffer_s * rb, size_t len)
{
	uint32_t write_pt;

	if (rb == NULL) {
		errno = EINVAL;
		return NULL;
	}
	/*
	 * Reclaim data if we are over writing and we need space
	 */
	if (rb->flags & QB_RB_FLAG_OVERWRITE) {
		while (qb_rb_space_free(rb) < (len + QB_RB_CHUNK_MARGIN)) {
			int rc = _rb_chunk_reclaim(rb);
			if (rc != 0) {
				return NULL;  /* errno already set */
			}
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
	rb->shared_data[write_pt] = 0;
	QB_RB_CHUNK_MAGIC_SET(rb, write_pt, QB_RB_CHUNK_MAGIC_ALLOC);

	/*
	 * return a pointer to the beginning of the chunk data
	 */
	return (void *)QB_RB_CHUNK_DATA_GET(rb, write_pt);

}

static uint32_t
qb_rb_chunk_step(struct qb_ringbuffer_s * rb, uint32_t pointer)
{
	uint32_t chunk_size = QB_RB_CHUNK_SIZE_GET(rb, pointer);
	/*
	 * skip over the chunk header
	 */
	pointer += QB_RB_CHUNK_HEADER_WORDS;

	/*
	 * skip over the user's data.
	 */
	pointer += (chunk_size / sizeof(uint32_t));
	/* make allowance for non-word sizes */
	if ((chunk_size % (sizeof(uint32_t) * QB_RB_WORD_ALIGN)) != 0) {
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

	/*
	 * commit the new write pointer
	 */
	rb->shared_hdr->write_pt = qb_rb_chunk_step(rb, old_write_pt);
	QB_RB_CHUNK_MAGIC_SET(rb, old_write_pt, QB_RB_CHUNK_MAGIC);

	DEBUG_PRINTF("commit [%zd] read: %u, write: %u -> %u (%u)\n",
		     (rb->notifier.q_len_fn ?
		      rb->notifier.q_len_fn(rb->notifier.instance) : 0),
		     rb->shared_hdr->read_pt,
		     old_write_pt,
		     rb->shared_hdr->write_pt,
		     rb->shared_hdr->word_size);

	/*
	 * post the notification to the reader
	 */
	if (rb->notifier.post_fn) {
		return rb->notifier.post_fn(rb->notifier.instance, len);
	}
	return 0;
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

static int
_rb_chunk_reclaim(struct qb_ringbuffer_s * rb)
{
	uint32_t old_read_pt;
	uint32_t new_read_pt;
	uint32_t old_chunk_size;
	uint32_t chunk_magic;
	int rc = 0;

	old_read_pt = rb->shared_hdr->read_pt;
	chunk_magic = QB_RB_CHUNK_MAGIC_GET(rb, old_read_pt);
	if (chunk_magic != QB_RB_CHUNK_MAGIC) {
		errno = EINVAL;
		return -errno;
	}

	old_chunk_size = QB_RB_CHUNK_SIZE_GET(rb, old_read_pt);
	new_read_pt = qb_rb_chunk_step(rb, old_read_pt);

	/*
	 * clear the header
	 */
	rb->shared_data[old_read_pt] = 0;
	QB_RB_CHUNK_MAGIC_SET(rb, old_read_pt, QB_RB_CHUNK_MAGIC_DEAD);

	/*
	 * set the new read pointer after clearing the header
	 * to prevent a situation where a fast writer will write their
	 * new chunk between setting the new read pointer and clearing the
	 * header.
	 */
	rb->shared_hdr->read_pt = new_read_pt;

	if (rb->notifier.reclaim_fn) {
		rc = rb->notifier.reclaim_fn(rb->notifier.instance,
						 old_chunk_size);
		if (rc < 0) {
			errno = -rc;
			qb_util_perror(LOG_WARNING, "reclaim_fn");
		}
	}

	DEBUG_PRINTF("reclaim [%zd]: read: %u -> %u, write: %u\n",
		     (rb->notifier.q_len_fn ?
		      rb->notifier.q_len_fn(rb->notifier.instance) : 0),
		     old_read_pt,
		     rb->shared_hdr->read_pt,
		     rb->shared_hdr->write_pt);

	return rc;
}

void
qb_rb_chunk_reclaim(struct qb_ringbuffer_s * rb)
{
	if (rb == NULL) {
		return;
	}
	_rb_chunk_reclaim(rb);
}

ssize_t
qb_rb_chunk_peek(struct qb_ringbuffer_s * rb, void **data_out, int32_t timeout)
{
	uint32_t read_pt;
	uint32_t chunk_size;
	uint32_t chunk_magic;
	int32_t res = 0;

	if (rb == NULL) {
		return -EINVAL;
	}
	if (rb->notifier.timedwait_fn) {
		res = rb->notifier.timedwait_fn(rb->notifier.instance, timeout);
	}
	if (res < 0 && res != -EIDRM) {
		if (res == -ETIMEDOUT) {
			return 0;
		} else {
			errno = -res;
			qb_util_perror(LOG_ERR, "sem_timedwait");
		}
		return res;
	}
	read_pt = rb->shared_hdr->read_pt;
	chunk_magic = QB_RB_CHUNK_MAGIC_GET(rb, read_pt);
	if (chunk_magic != QB_RB_CHUNK_MAGIC) {
		if (rb->notifier.post_fn) {
			(void)rb->notifier.post_fn(rb->notifier.instance, res);
		}
#ifdef EBADMSG
		return -EBADMSG;
#else
		return -EINVAL;
#endif
	}
	chunk_size = QB_RB_CHUNK_SIZE_GET(rb, read_pt);
	*data_out = QB_RB_CHUNK_DATA_GET(rb, read_pt);
	return chunk_size;
}

ssize_t
qb_rb_chunk_read(struct qb_ringbuffer_s * rb, void *data_out, size_t len,
		 int32_t timeout)
{
	uint32_t read_pt;
	uint32_t chunk_size;
	uint32_t chunk_magic;
	int32_t res = 0;

	if (rb == NULL) {
		return -EINVAL;
	}
	if (rb->notifier.timedwait_fn) {
		res = rb->notifier.timedwait_fn(rb->notifier.instance, timeout);
	}
	if (res < 0 && res != -EIDRM) {
		if (res != -ETIMEDOUT) {
			errno = -res;
			qb_util_perror(LOG_ERR, "sem_timedwait");
		}
		return res;
	}

	read_pt = rb->shared_hdr->read_pt;
	chunk_magic = QB_RB_CHUNK_MAGIC_GET(rb, read_pt);

	if (chunk_magic != QB_RB_CHUNK_MAGIC) {
		if (rb->notifier.timedwait_fn == NULL) {
			return -ETIMEDOUT;
		} else {
			(void)rb->notifier.post_fn(rb->notifier.instance, res);
#ifdef EBADMSG
			return -EBADMSG;
#else
			return -EINVAL;
#endif
		}
	}

	chunk_size = QB_RB_CHUNK_SIZE_GET(rb, read_pt);
	if (len < chunk_size) {
		qb_util_log(LOG_ERR,
			    "trying to recv chunk of size %d but %d available",
			    len, chunk_size);
		if (rb->notifier.post_fn) {
			(void)rb->notifier.post_fn(rb->notifier.instance, chunk_size);
		}
		return -ENOBUFS;
	}

	memcpy(data_out,
	       QB_RB_CHUNK_DATA_GET(rb, read_pt),
	       chunk_size);

	_rb_chunk_reclaim(rb);

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
#ifndef S_SPLINT_S
	printf(" ->write_pt [%" PRIu32 "]\n", rb->shared_hdr->write_pt);
	printf(" ->read_pt [%" PRIu32 "]\n", rb->shared_hdr->read_pt);
	printf(" ->size [%" PRIu32 " words]\n", rb->shared_hdr->word_size);
	printf(" =>free [%zd bytes]\n", qb_rb_space_free(rb));
	printf(" =>used [%zd bytes]\n", qb_rb_space_used(rb));
#endif /* S_SPLINT_S */
}

/*
 * FILE HEADER ORDER
 * 1. word_size
 * 2. write_pt
 * 3. read_pt
 * 4. version
 * 5. header_hash
 *
 * 6. data
 */

ssize_t
qb_rb_write_to_file(struct qb_ringbuffer_s * rb, int32_t fd)
{
	ssize_t result;
	ssize_t written_size = 0;
	uint32_t hash = 0;
	uint32_t version = QB_RB_FILE_HEADER_VERSION;

	if (rb == NULL) {
		return -EINVAL;
	}
	print_header(rb);

	/*
 	 * 1. word_size
 	 */
	result = write(fd, &rb->shared_hdr->word_size, sizeof(uint32_t));
	if (result != sizeof(uint32_t)) {
		return -errno;
	}
	written_size += result;

	/*
	 * 2. 3. store the read & write pointers
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

	/*
	 * 4. version used
	 */
	result = write(fd, &version, sizeof(uint32_t));
	if (result != sizeof(uint32_t)) {
		return -errno;
	}
	written_size += result;

	/*
	 * 5. hash helps us verify header is not corrupted on file read
	 */
	hash = rb->shared_hdr->word_size + rb->shared_hdr->write_pt + rb->shared_hdr->read_pt + QB_RB_FILE_HEADER_VERSION;
	result = write(fd, &hash, sizeof(uint32_t));
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
	uint32_t word_size = 0;
	uint32_t version = 0;
	uint32_t hash = 0;
	uint32_t calculated_hash = 0;

	if (fd < 0) {
		return NULL;
	}

	/*
	 * 1. word size
	 */
	n_required = sizeof(uint32_t);
	n_read = read(fd, &word_size, n_required);
	if (n_read != n_required) {
		qb_util_perror(LOG_ERR, "Unable to read blackbox file header");
		return NULL;
	}
	total_read += n_read;

	/*
	 * 2. 3. read & write pointers
	 */
	n_read = read(fd, &write_pt, sizeof(uint32_t));
	assert(n_read == sizeof(uint32_t));
	total_read += n_read;

	n_read = read(fd, &read_pt, sizeof(uint32_t));
	assert(n_read == sizeof(uint32_t));
	total_read += n_read;

	/*
	 * 4. version
	 */
	n_required = sizeof(uint32_t);
	n_read = read(fd, &version, n_required);
	if (n_read != n_required) {
		qb_util_perror(LOG_ERR, "Unable to read blackbox file header");
		return NULL;
	}
	total_read += n_read;

	/*
	 * 5. Hash
	 */
	n_required = sizeof(uint32_t);
	n_read = read(fd, &hash, n_required);
	if (n_read != n_required) {
		qb_util_perror(LOG_ERR, "Unable to read blackbox file header");
		return NULL;
	}
	total_read += n_read;

	calculated_hash = word_size + write_pt + read_pt + version;
	if (hash != calculated_hash) {
		qb_util_log(LOG_ERR, "Corrupt blackbox: File header hash (%d) does not match calculated hash (%d)", hash, calculated_hash);
		return NULL;
	} else if (version != QB_RB_FILE_HEADER_VERSION) {
		qb_util_log(LOG_ERR, "Wrong file header version. Expected %d got %d",
			QB_RB_FILE_HEADER_VERSION, version);
		return NULL;
	}

	/*
	 * 6. data
	 */
	n_required = (word_size * sizeof(uint32_t));

	/*
	 * qb_rb_open adds QB_RB_CHUNK_MARGIN + 1 to the requested size.
	 */
	rb = qb_rb_open("create_from_file", n_required - (QB_RB_CHUNK_MARGIN + 1),
			QB_RB_FLAG_CREATE | QB_RB_FLAG_NO_SEMAPHORE, 0);
	if (rb == NULL) {
		return NULL;
	}
	rb->shared_hdr->read_pt = read_pt;
	rb->shared_hdr->write_pt = write_pt;

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

	qb_util_log(LOG_DEBUG, "read total of: %zd", total_read);
	print_header(rb);

	return rb;

cleanup_fail:
	qb_rb_close(rb);
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
	if (res < 0 && errno != EPERM) {
		return -errno;
	}
	res = chown(rb->shared_hdr->hdr_path, owner, group);
	if (res < 0 && errno != EPERM) {
		return -errno;
	}
	return 0;
}

int32_t
qb_rb_chmod(qb_ringbuffer_t * rb, mode_t mode)
{
	int32_t res;

	if (rb == NULL) {
		return -EINVAL;
	}
	res = chmod(rb->shared_hdr->data_path, mode);
	if (res < 0) {
		return -errno;
	}
	res = chmod(rb->shared_hdr->hdr_path, mode);
	if (res < 0) {
		return -errno;
	}
	return 0;
}
