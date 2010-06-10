/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include <config.h>

#include "os_base.h"
#include "util_int.h"
#include <syslog.h>
#include <stdarg.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <pthread.h>
#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#else
#include <sys/sem.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <qb/qbutil.h>

struct qb_thread_lock_s {
	qb_thread_lock_type_t type;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spinlock_t spinlock;
#endif
	pthread_mutex_t mutex;
};

qb_thread_lock_t *qb_thread_lock_create(qb_thread_lock_type_t type)
{
	struct qb_thread_lock_s *tl = malloc(sizeof(struct qb_thread_lock_s));
	int res;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (type == QB_THREAD_LOCK_SHORT) {
		tl->type = QB_THREAD_LOCK_SHORT;
		res = pthread_spin_init(&tl->spinlock, 1);
	} else
#endif
	{
		tl->type = QB_THREAD_LOCK_LONG;
		res = pthread_mutex_init(&tl->mutex, NULL);
	}
	if (res == 0) {
		return tl;
	} else {
		free(tl);
		return NULL;
	}
}

int32_t qb_thread_lock(qb_thread_lock_t * tl)
{
	int res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_lock(&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_lock(&tl->mutex);
	}
	return res;
}

int32_t qb_thread_unlock(qb_thread_lock_t * tl)
{
	int res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_unlock(&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_unlock(&tl->mutex);
	}
	return res;
}

int32_t qb_thread_trylock(qb_thread_lock_t * tl)
{
	int res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_trylock(&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_trylock(&tl->mutex);
	}
	return res;
}

int32_t qb_thread_lock_destroy(qb_thread_lock_t * tl)
{
	int32_t res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_destroy(&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_destroy(&tl->mutex);
	}
	free(tl);
	return res;
}

/*
 * ---------------------------------------------------
 * Logging functions for the library.
 */
static qb_util_log_fn_t real_log_fn = NULL;

void _qb_util_log(const char *file_name,
		  int32_t file_line, int32_t severity, const char *format, ...)
{
	if (real_log_fn) {
		va_list ap;
		char msg[256];

		va_start(ap, format);
		vsnprintf(msg, 256, format, ap);
		va_end(ap);

		real_log_fn(file_name, file_line, severity, msg);
	}
}

void qb_util_set_log_function(qb_util_log_fn_t fn)
{
	real_log_fn = fn;
}

static int32_t open_mmap_file(char *path, uint32_t file_flags)
{
	if (strstr(path, "XXXXXX") != NULL) {
		return mkstemp(path);
	}

	return open(path, file_flags, 0600);
}

/*
 * ---------------------------------------------------
 * shared memory functions.
 */
int32_t qb_util_mmap_file_open(char *path, const char *file, size_t bytes,
			       uint32_t file_flags)
{
	int32_t fd;
	char *is_absolute = strchr(file, '/');;

	if (is_absolute) {
		strcpy(path, file);
	} else {
		snprintf(path, PATH_MAX, "/dev/shm/%s", file);
	}
	fd = open_mmap_file(path, file_flags);
	if (fd == -1 && !is_absolute) {
		qb_util_log(LOG_ERR, "couldn't open file %s error: %s",
			    path, strerror(errno));
		snprintf(path, PATH_MAX, LOCALSTATEDIR "/run/%s", file);
		fd = open_mmap_file(path, file_flags);
		if (fd == -1) {
			return -1;
		}
	}

	if (fd != -1) {
		ftruncate(fd, bytes);
	}
	return fd;
}

int32_t qb_util_circular_mmap(int32_t fd, void **buf, size_t bytes)
{
	void *addr_orig;
	void *addr;
	int32_t res;

	addr_orig = mmap(NULL, bytes << 1, PROT_NONE,
			 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr_orig == MAP_FAILED) {
		return (-1);
	}

	addr = mmap(addr_orig, bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		return (-1);
	}
#ifdef QB_BSD
	madvise(addr_orig, bytes, MADV_NOSYNC);
#endif

	addr = mmap(((char *)addr_orig) + bytes,
		    bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);
#ifdef QB_BSD
	madvise(((char *)addr_orig) + bytes, bytes, MADV_NOSYNC);
#endif

	res = close(fd);
	if (res) {
		return (-1);
	}
	*buf = addr_orig;
	return (0);
}
