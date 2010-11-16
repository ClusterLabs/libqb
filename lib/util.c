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
#include "os_base.h"

#include "util_int.h"
#include <sys/shm.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/stat.h>
#include <qb/qbdefs.h>
#include <qb/qbutil.h>

struct qb_thread_lock_s {
	qb_thread_lock_type_t type;
#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	pthread_spinlock_t spinlock;
#endif /* HAVE_PTHREAD_SHARED_SPIN_LOCK */
	pthread_mutex_t mutex;
};

qb_thread_lock_t *qb_thread_lock_create(qb_thread_lock_type_t type)
{
	struct qb_thread_lock_s *tl = malloc(sizeof(struct qb_thread_lock_s));
	int32_t res;

#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	if (type == QB_THREAD_LOCK_SHORT) {
		tl->type = QB_THREAD_LOCK_SHORT;
		res = pthread_spin_init(&tl->spinlock, 1);
	} else
#endif /* HAVE_PTHREAD_SHARED_SPIN_LOCK */
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
	int32_t res;
#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = -pthread_spin_lock(&tl->spinlock);
	} else
#endif /* HAVE_PTHREAD_SHARED_SPIN_LOCK */
	{
		res = -pthread_mutex_lock(&tl->mutex);
	}
	return res;
}

int32_t qb_thread_unlock(qb_thread_lock_t * tl)
{
	int32_t res;
#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = -pthread_spin_unlock(&tl->spinlock);
	} else
#endif
	{
		res = -pthread_mutex_unlock(&tl->mutex);
	}
	return res;
}

int32_t qb_thread_trylock(qb_thread_lock_t * tl)
{
	int32_t res;
#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = -pthread_spin_trylock(&tl->spinlock);
	} else
#endif
	{
		res = -pthread_mutex_trylock(&tl->mutex);
	}
	return res;
}

int32_t qb_thread_lock_destroy(qb_thread_lock_t * tl)
{
	int32_t res;
#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = -pthread_spin_destroy(&tl->spinlock);
	} else
#endif
	{
		res = -pthread_mutex_destroy(&tl->mutex);
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

void qb_timespec_add_ms(struct timespec *ts, int32_t ms)
{
#ifndef S_SPLINT_S
	ts->tv_sec = ms / 1000;
	ts->tv_nsec = (ms % 1000) * QB_TIME_NS_IN_MSEC;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec = ts->tv_nsec - 1000000000L;
	}
#endif /* S_SPLINT_S */
}

#ifdef HAVE_MONOTONIC_CLOCK
uint64_t qb_util_nano_current_get(void)
{
	uint64_t nano_monotonic;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	nano_monotonic =
	    (ts.tv_sec * QB_TIME_NS_IN_SEC) + (uint64_t)ts.tv_nsec;
	return (nano_monotonic);
}
uint64_t qb_util_nano_from_epoch_get(void)
{
	uint64_t nano_monotonic;
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	nano_monotonic =
	    (ts.tv_sec * QB_TIME_NS_IN_SEC) + (uint64_t)ts.tv_nsec;
	return (nano_monotonic);
}

uint64_t qb_util_nano_monotonic_hz(void)
{
	uint64_t nano_monotonic_hz;
	struct timespec ts;

	clock_getres(CLOCK_MONOTONIC, &ts);

	nano_monotonic_hz =
	    QB_TIME_NS_IN_SEC / ((ts.tv_sec * QB_TIME_NS_IN_SEC) +
				   ts.tv_nsec);

	return (nano_monotonic_hz);
}

void qb_util_timespec_from_epoch_get(struct timespec *ts)
{
	clock_gettime(CLOCK_REALTIME, ts);
}
#else
uint64_t qb_util_nano_current_get(void)
{
	return qb_util_nano_from_epoch_get();
}

uint64_t qb_util_nano_monotonic_hz(void)
{
	return HZ;
}

void qb_util_timespec_from_epoch_get(struct timespec *ts)
{
	struct timeval time_from_epoch;
	gettimeofday(&time_from_epoch, 0);

#ifndef S_SPLINT_S
	ts->tv_sec = time_from_epoch.tv_sec;
	ts->tv_nsec = time_from_epoch.tv_usec * QB_TIME_NS_IN_USEC;
#endif /* S_SPLINT_S */
}

uint64_t qb_util_nano_from_epoch_get(void)
{
	uint64_t nano_from_epoch;
	struct timeval time_from_epoch;
	gettimeofday(&time_from_epoch, 0);

	nano_from_epoch = ((time_from_epoch.tv_sec * QB_TIME_NS_IN_SEC) +
			   (time_from_epoch.tv_usec * QB_TIME_NS_IN_USEC));

	return (nano_from_epoch);
}
#endif

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
	int32_t i;
	int32_t res = 0;
	ssize_t written;
	char *buffer = NULL;
	char *is_absolute = strchr(file, '/');;

	if (is_absolute) {
		strcpy(path, file);
	} else {
		snprintf(path, PATH_MAX, "/dev/shm/%s", file);
	}
	fd = open_mmap_file(path, file_flags);
	if (fd < 0 && !is_absolute) {
		res = -errno;
		qb_util_log(LOG_ERR, "couldn't open file %s error: %s",
			    path, strerror(-res));

		snprintf(path, PATH_MAX, LOCALSTATEDIR "/run/%s", file);
		fd = open_mmap_file(path, file_flags);
		if (fd < 0) {
			res = -errno;
			qb_util_log(LOG_ERR, "couldn't open file %s error: %s",
					path, strerror(-res));
			return res;
		}
	}

	if (ftruncate(fd, bytes) == -1) {
		res = -errno;
		qb_util_log(LOG_ERR,
			    "couldn't truncate file %s error: %s",
			    path, strerror(-res));
		goto unlink_exit;
	}


	if (file_flags & O_CREAT) {
		long page_size = sysconf(_SC_PAGESIZE);
		buffer = calloc(1, page_size);
		if (buffer == NULL) {
			res = -ENOMEM;
			goto unlink_exit;
		}
		for (i = 0; i < (bytes / page_size); i++) {
retry_write:
			written = write (fd, buffer, page_size);
			if (written == -1 && errno == EINTR) {
				goto retry_write;
			}
			if (written != page_size) {
				res = -ENOSPC;
				free (buffer);
				goto unlink_exit;
			}
		}
		free (buffer);
	}

	return fd;

unlink_exit:
	unlink (path);
	if (fd > 0) {
		close (fd);
	}
	return res;
}

int32_t qb_util_circular_mmap(int32_t fd, void **buf, size_t bytes)
{
	void *addr_orig;
	void *addr;
	int32_t res;

	addr_orig = mmap(NULL, bytes << 1, PROT_NONE,
			 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr_orig == MAP_FAILED) {
		return (-errno);
	}

	addr = mmap(addr_orig, bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		return (-errno);
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
		return (-errno);
	}
	*buf = addr_orig;
	return (0);
}
