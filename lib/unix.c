/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
#include "os_base.h"

#ifdef HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#if defined(HAVE_FCNTL_H) && defined(HAVE_POSIX_FALLOCATE)
#include <fcntl.h>
#endif

#include "util_int.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbatomic.h>

#if defined(MAP_ANON) && ! defined(MAP_ANONYMOUS)
/*
 * BSD derivatives usually have MAP_ANON, not MAP_ANONYMOUS
 **/
#define MAP_ANONYMOUS MAP_ANON
#endif


char *
qb_strerror_r(int errnum, char *buf, size_t buflen)
{
#ifdef STRERROR_R_CHAR_P
	return strerror_r(errnum, buf, buflen);
#else
	if (strerror_r(errnum, buf, buflen) != 0) {
		buf[0] = '\0';
	}
	return buf;
#endif /* STRERROR_R_CHAR_P */
}

static int32_t
open_mmap_file(char *path, uint32_t file_flags)
{
	if (strstr(path, "XXXXXX") != NULL) {
		mode_t old_mode = umask(077);
		int32_t temp_fd = mkstemp(path);
		(void)umask(old_mode);
		return temp_fd;
	}

	return open(path, file_flags, 0600);
}

int32_t
qb_sys_mmap_file_open(char *path, const char *file, size_t bytes,
		       uint32_t file_flags)
{
	int32_t fd;
	int32_t res = 0;
#ifndef HAVE_POSIX_FALLOCATE
	ssize_t written;
	char *buffer = NULL;
	int32_t i;
#endif
	char *is_absolute = strchr(file, '/');

	if (is_absolute) {
		(void)strlcpy(path, file, PATH_MAX);
	} else {
#if defined(QB_LINUX) || defined(QB_CYGWIN)
		/* This is only now called when talking to an old libqb
		   where we need to add qb- to the name */
		snprintf(path, PATH_MAX, "/dev/shm/qb-%s", file);
#else
		snprintf(path, PATH_MAX, "%s/%s", SOCKETDIR, file);
		is_absolute = path;
#endif
	}
	fd = open_mmap_file(path, file_flags);
	if (fd < 0 && !is_absolute) {
		qb_util_perror(LOG_ERR, "couldn't open file %s", path);

		snprintf(path, PATH_MAX, "%s/%s", SOCKETDIR, file);
		fd = open_mmap_file(path, file_flags);
		if (fd < 0) {
			res = -errno;
			qb_util_perror(LOG_ERR, "couldn't open file %s", path);
			return res;
		}
	} else if (fd < 0 && is_absolute) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't open file %s", path);
		return res;
	}

#ifndef _WIN32
	/* ftruncate not supported on WSL
	   https://github.com/microsoft/WSL/issues/902 */
	if (ftruncate(fd, bytes) == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't truncate file %s", path);
		goto unlink_exit;
	}
#endif
#ifdef HAVE_POSIX_FALLOCATE
	if ((res = posix_fallocate(fd, 0, bytes)) != 0) {
		errno = res;
		res = -1 * res;
		qb_util_perror(LOG_ERR, "couldn't allocate file %s", path);
		goto unlink_exit;
	}
#else
	if (file_flags & O_CREAT) {
		long page_size = sysconf(_SC_PAGESIZE);
		long write_size = QB_MIN(page_size, bytes);
		if (page_size < 0) {
			res = -errno;
			goto unlink_exit;
		}
		buffer = calloc(1, write_size);
		if (buffer == NULL) {
			res = -ENOMEM;
			goto unlink_exit;
		}
		for (i = 0; i < (bytes / write_size); i++) {
retry_write:
			written = write(fd, buffer, write_size);
			if (written == -1 && errno == EINTR) {
				goto retry_write;
			}
			if (written != write_size) {
				res = -ENOSPC;
				free(buffer);
				goto unlink_exit;
			}
		}
		free(buffer);
	}
#endif /* HAVE_POSIX_FALLOCATE */

	return fd;

unlink_exit:
	unlink(path);
	if (fd >= 0) {
		close(fd);
	}
	return res;
}


int32_t
qb_sys_circular_mmap(int32_t fd, void **buf, size_t bytes)
{
	void *addr_orig = NULL;
	void *addr;
	void *addr_next;
	int32_t res;
	int flags = MAP_ANONYMOUS;

#ifdef QB_FORCE_SHM_ALIGN
/* On a number of arches any fixed and shared mmap() mapping address
 * must be aligned to 16k. If the first mmap() below is not shared then
 * the first mmap() will succeed because these restrictions do not apply to
 * private mappings. The second mmap() wants a shared memory mapping but
 * the address returned by the first one is only page-aligned and not
 * aligned to 16k.
 */
	flags |= MAP_SHARED;
#else
	flags |= MAP_PRIVATE;
#endif /* QB_FORCE_SHM_ALIGN */

#if defined(QB_ARCH_HPPA)
	/* map twice the size we want to make sure we have already mapped
	   the second memory location behind it too. Otherwise the Linux
	   kernel may map it in the upper memory so that we can't map
	   the second part afterwards since it will conflict. */
	addr = mmap(NULL, 2*bytes, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, 0);

	if (addr == MAP_FAILED)
		return -errno;

	addr_orig = addr;
#else
	addr_orig = mmap(NULL, bytes << 1, PROT_NONE, flags, -1, 0);

	if (addr_orig == MAP_FAILED) {
		return -errno;
	}

	addr = mmap(addr_orig, bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);
#endif

	if (addr != addr_orig) {
		res = -errno;
		goto cleanup_fail;
	}
#if defined(QB_BSD) && defined(MADV_NOSYNC)
	madvise(addr_orig, bytes, MADV_NOSYNC);
#endif
	addr_next = ((char *)addr_orig) + bytes;
	addr = mmap(addr_next,
		    bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);
	if (addr != addr_next) {
		res = -errno;
		goto cleanup_fail;
	}
#if defined(QB_BSD) && defined(MADV_NOSYNC)
	madvise(((char *)addr_orig) + bytes, bytes, MADV_NOSYNC);
#endif

	res = close(fd);
	if (res) {
		goto cleanup_fail;
	}
	*buf = addr_orig;
	return 0;

cleanup_fail:

	if (addr_orig) {
		munmap(addr_orig, bytes << 1);
	}
	close(fd);
	return res;
}

int32_t
qb_sys_fd_nonblock_cloexec_set(int32_t fd)
{
	int32_t res = 0;
	int32_t oldflags = fcntl(fd, F_GETFD, 0);

	if (oldflags < 0) {
		oldflags = 0;
	}
	oldflags |= FD_CLOEXEC;
	res = fcntl(fd, F_SETFD, oldflags);
	if (res == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR,
			       "Could not set close-on-exit on fd:%d", fd);
		return res;
	}

	res = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		res = -errno;
		qb_util_log(LOG_ERR, "Could not set non-blocking on fd:%d", fd);
	}

	return res;
}

int32_t
qb_sys_unlink_or_truncate(const char *path, int32_t truncate_fallback)
{
	int32_t res = 0;

	if (unlink(path) == -1) {
		res = errno;
		qb_util_perror(LOG_DEBUG,
			       "Unlinking file: %s",
			       path);
		if (res != ENOENT && truncate_fallback) {
			res = errno = 0;
			if (truncate(path, 0) == -1) {
				res = errno;
				qb_util_perror(LOG_DEBUG,
					       "Truncating file: %s", path);
			}
		}
	}
	return -res;
}

#if defined(HAVE_OPENAT) && defined(HAVE_UNLINKAT)
int32_t
qb_sys_unlink_or_truncate_at(int32_t dirfd, const char *path,
			     int32_t truncate_fallback)
{
	int32_t fd, res = 0;

	if (unlinkat(dirfd, path, 0) == -1) {
		res = errno;
		qb_util_perror(LOG_DEBUG,
			       "Unlinking file at dir: %s", path);
		if (res != ENOENT && truncate_fallback) {
			res = errno = 0;
			if ((fd = openat(dirfd, path, O_WRONLY|O_TRUNC)) == -1) {
				res = errno;
				qb_util_perror(LOG_DEBUG,
					       "Truncating file at dir: %s",
					       path);
			} else {
				close(fd);
			}
		}
	}
	return -res;
}
#endif

void
qb_sigpipe_ctl(enum qb_sigpipe_ctl ctl)
{
#if !defined(HAVE_MSG_NOSIGNAL) && !defined(HAVE_SO_NOSIGPIPE)
	struct sigaction act;
	struct sigaction oact;

	act.sa_handler = SIG_IGN;

	if (ctl == QB_SIGPIPE_IGNORE) {
		sigaction(SIGPIPE, &act, &oact);
	} else {
		sigaction(SIGPIPE, &oact, NULL);
	}
#endif  /* !MSG_NOSIGNAL && !SO_NOSIGPIPE */
}

void
qb_socket_nosigpipe(int32_t s)
{
#if !defined(HAVE_MSG_NOSIGNAL) && defined(HAVE_SO_NOSIGPIPE)
	int32_t on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
#endif /* !MSG_NOSIGNAL && SO_NOSIGPIPE */
}


/*
 * atomic operations
 * --------------------------------------------------------------------------
 */
#ifndef HAVE_GCC_BUILTINS_FOR_SYNC_OPERATIONS
/*
 * We have to use the slow, but safe locking method
 */

static qb_thread_lock_t *qb_atomic_mutex = NULL;

void
qb_atomic_init(void)
{
	if (qb_atomic_mutex == NULL) {
		qb_atomic_mutex = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	assert(qb_atomic_mutex);
}

int32_t
qb_atomic_int_exchange_and_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
			       int32_t val)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	result = *atomic;
	*atomic += val;
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

void
qb_atomic_int_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t val)
{
	qb_thread_lock(qb_atomic_mutex);
	*atomic += val;
	qb_thread_unlock(qb_atomic_mutex);
}

int32_t
qb_atomic_int_compare_and_exchange(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
				   int32_t oldval, int32_t newval)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	if (*atomic == oldval) {
		result = QB_TRUE;
		*atomic = newval;
	} else {
		result = QB_FALSE;
	}
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

int32_t
qb_atomic_pointer_compare_and_exchange(volatile void *QB_GNUC_MAY_ALIAS *
				       atomic, void *oldval, void *newval)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	if (*atomic == oldval) {
		result = QB_TRUE;
		*atomic = newval;
	} else {
		result = QB_FALSE;
	}
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

#ifdef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED
int32_t
(qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	result = *atomic;
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

void
(qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
			  int32_t newval)
{
	qb_thread_lock(qb_atomic_mutex);
	*atomic = newval;
	qb_thread_unlock(qb_atomic_mutex);
}

void *
(qb_atomic_pointer_get) (volatile void *QB_GNUC_MAY_ALIAS * atomic)
{
	void *result;

	qb_thread_lock(qb_atomic_mutex);
	result = (void*)*atomic;
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

void
(qb_atomic_pointer_set) (volatile void *QB_GNUC_MAY_ALIAS * atomic,
			      void *newval)
{
	qb_thread_lock(qb_atomic_mutex);
	*atomic = newval;
	qb_thread_unlock(qb_atomic_mutex);
}
#endif /* QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */

#else

/*
 * gcc built-ins
 */

void
qb_atomic_init(void)
{
}

int32_t
qb_atomic_int_exchange_and_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
			       int32_t val)
{
	return __sync_fetch_and_add(atomic, val);
}

void
qb_atomic_int_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t val)
{
	__sync_fetch_and_add(atomic, val);
}

int32_t
qb_atomic_int_compare_and_exchange(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
				   int32_t oldval, int32_t newval)
{
	return __sync_bool_compare_and_swap(atomic, oldval, newval);
}

int32_t
qb_atomic_pointer_compare_and_exchange(volatile void *QB_GNUC_MAY_ALIAS *
				       atomic, void *oldval, void *newval)
{
	return __sync_bool_compare_and_swap(atomic, oldval, newval);
}

#ifdef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED
#define QB_ATOMIC_MEMORY_BARRIER __sync_synchronize ()

int32_t
(qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic)
{
	QB_ATOMIC_MEMORY_BARRIER;
	return *atomic;
}

void
(qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
		     int32_t newval)
{
	*atomic = newval;
	QB_ATOMIC_MEMORY_BARRIER;
}

void *
(qb_atomic_pointer_get) (volatile void *QB_GNUC_MAY_ALIAS * atomic)
{
	QB_ATOMIC_MEMORY_BARRIER;
	return (void*)*atomic;
}

void
(qb_atomic_pointer_set) (volatile void *QB_GNUC_MAY_ALIAS * atomic,
			 void *newval)
{
	*atomic = newval;
	QB_ATOMIC_MEMORY_BARRIER;
}

#endif /* QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */

#endif /* HAVE_GCC_BUILTINS_FOR_SYNC_OPERATIONS */

#ifndef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED
int32_t
(qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic)
{
	return qb_atomic_int_get(atomic);
}

void
(qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
			  int32_t newval)
{
	qb_atomic_int_set(atomic, newval);
}

void *
(qb_atomic_pointer_get) (volatile void *QB_GNUC_MAY_ALIAS * atomic)
{
	return qb_atomic_pointer_get(atomic);
}

void
(qb_atomic_pointer_set) (volatile void *QB_GNUC_MAY_ALIAS * atomic,
			      void *newval)
{
	qb_atomic_pointer_set(atomic, newval);
}
#endif /* !QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */
