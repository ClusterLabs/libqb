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
#include <pthread.h>
#include <sys/stat.h>
#include <qb/qbconfig.h>
#include <qb/qbdefs.h>
#include <qb/qbutil.h>

struct qb_thread_lock_s {
	qb_thread_lock_type_t type;
#ifdef HAVE_PTHREAD_SHARED_SPIN_LOCK
	pthread_spinlock_t spinlock;
#endif /* HAVE_PTHREAD_SHARED_SPIN_LOCK */
	pthread_mutex_t mutex;
};

qb_thread_lock_t *
qb_thread_lock_create(qb_thread_lock_type_t type)
{
	struct qb_thread_lock_s *tl = malloc(sizeof(struct qb_thread_lock_s));
	int32_t res;

	if (tl == NULL) {
		return NULL;
	}
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

int32_t
qb_thread_lock(qb_thread_lock_t * tl)
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

int32_t
qb_thread_unlock(qb_thread_lock_t * tl)
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

int32_t
qb_thread_trylock(qb_thread_lock_t * tl)
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

int32_t
qb_thread_lock_destroy(qb_thread_lock_t * tl)
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

void
qb_timespec_add_ms(struct timespec *ts, int32_t ms)
{
#ifndef S_SPLINT_S
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * QB_TIME_NS_IN_MSEC;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec = ts->tv_nsec - 1000000000L;
	}
#endif /* S_SPLINT_S */
}

#ifdef HAVE_MONOTONIC_CLOCK
uint64_t
qb_util_nano_current_get(void)
{
	uint64_t nano_monotonic;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	nano_monotonic =
	    (ts.tv_sec * QB_TIME_NS_IN_SEC) + (uint64_t) ts.tv_nsec;
	return (nano_monotonic);
}

uint64_t
qb_util_nano_from_epoch_get(void)
{
	uint64_t nano_monotonic;
	struct timespec ts;
#ifdef CLOCK_REALTIME_COARSE
	clock_gettime(CLOCK_REALTIME_COARSE, &ts);
#else
	clock_gettime(CLOCK_REALTIME, &ts);
#endif
	nano_monotonic =
	    (ts.tv_sec * QB_TIME_NS_IN_SEC) + (uint64_t) ts.tv_nsec;
	return (nano_monotonic);
}

uint64_t
qb_util_nano_monotonic_hz(void)
{
	uint64_t nano_monotonic_hz;
	struct timespec ts;

#if HAVE_CLOCK_GETRES_MONOTONIC
	clock_getres(CLOCK_MONOTONIC, &ts);
#else
	if (clock_getres(CLOCK_REALTIME, &ts) != 0)
	    qb_util_perror(LOG_ERR,"CLOCK_REALTIME");
#endif

	nano_monotonic_hz =
	    QB_TIME_NS_IN_SEC / ((ts.tv_sec * QB_TIME_NS_IN_SEC) + ts.tv_nsec);

	return (nano_monotonic_hz);
}

void
qb_util_timespec_from_epoch_get(struct timespec *ts)
{
#ifdef CLOCK_REALTIME_COARSE
	clock_gettime(CLOCK_REALTIME_COARSE, ts);
#else
	clock_gettime(CLOCK_REALTIME, ts);
#endif
}

#else
uint64_t
qb_util_nano_current_get(void)
{
	return qb_util_nano_from_epoch_get();
}

uint64_t
qb_util_nano_monotonic_hz(void)
{
	return sysconf(_SC_CLK_TCK);
}

void
qb_util_timespec_from_epoch_get(struct timespec *ts)
{
	struct timeval time_from_epoch;
	gettimeofday(&time_from_epoch, 0);

#ifndef S_SPLINT_S
	ts->tv_sec = time_from_epoch.tv_sec;
	ts->tv_nsec = time_from_epoch.tv_usec * QB_TIME_NS_IN_USEC;
#endif /* S_SPLINT_S */
}

uint64_t
qb_util_nano_from_epoch_get(void)
{
	uint64_t nano_from_epoch;
	struct timeval time_from_epoch;
	gettimeofday(&time_from_epoch, 0);

	nano_from_epoch = ((time_from_epoch.tv_sec * QB_TIME_NS_IN_SEC) +
			   (time_from_epoch.tv_usec * QB_TIME_NS_IN_USEC));

	return (nano_from_epoch);
}
#endif /* HAVE_MONOTONIC_CLOCK */

struct qb_util_stopwatch {
	uint64_t started;
	uint64_t stopped;
	uint32_t split_options;
	uint32_t split_size;
	uint32_t split_entries;
	uint64_t *split_entry_list;
};

qb_util_stopwatch_t *
qb_util_stopwatch_create(void)
{
	struct qb_util_stopwatch *sw;
	sw = (struct qb_util_stopwatch *)calloc(1, sizeof(struct qb_util_stopwatch));
	return sw;
}

void
qb_util_stopwatch_free(qb_util_stopwatch_t * sw)
{
	free(sw->split_entry_list);
	free(sw);
}

void
qb_util_stopwatch_start(qb_util_stopwatch_t * sw)
{
	sw->started = qb_util_nano_current_get();
	sw->stopped = 0;
	sw->split_entries = 0;
}

void
qb_util_stopwatch_stop(qb_util_stopwatch_t * sw)
{
	sw->stopped = qb_util_nano_current_get();
}

uint64_t
qb_util_stopwatch_us_elapsed_get(qb_util_stopwatch_t * sw)
{
	if (sw->stopped == 0 || sw->started == 0) {
		return 0;
	}
	return ((sw->stopped - sw->started) / QB_TIME_NS_IN_USEC);
}

float
qb_util_stopwatch_sec_elapsed_get(qb_util_stopwatch_t * sw)
{
	uint64_t e6;
	if (sw->stopped == 0 || sw->started == 0) {
		return 0;
	}
	e6 = qb_util_stopwatch_us_elapsed_get(sw);
	return ((float)e6 / (float)QB_TIME_US_IN_SEC);
}

int32_t
qb_util_stopwatch_split_ctl(qb_util_stopwatch_t *sw,
        uint32_t max_splits, uint32_t options)
{
	sw->split_size = max_splits;
	sw->split_options = options;
	sw->split_entry_list = (uint64_t *)calloc(1, sizeof (uint64_t) * max_splits);
	if (sw->split_entry_list == NULL) {
		return -errno;
	}
	return 0;
}

uint64_t
qb_util_stopwatch_split(qb_util_stopwatch_t *sw)
{
	uint32_t new_entry_pos;
	uint64_t time_start;
	uint64_t time_end;

	if (sw->split_size == 0) {
		return 0;
	}
	if (!(sw->split_options & QB_UTIL_SW_OVERWRITE) &&
	    sw->split_entries == sw->split_size) {
		return 0;
	}
	if (sw->started == 0) {
		qb_util_stopwatch_start(sw);
	}
	new_entry_pos = sw->split_entries % (sw->split_size);
	sw->split_entry_list[new_entry_pos] = qb_util_nano_current_get();
	sw->split_entries++;

	time_start = sw->split_entry_list[new_entry_pos];
	if (sw->split_entries == 1) {
		/* first entry */
		time_end = sw->started;
	} else if (new_entry_pos == 0) {
		/* wrap around */
		time_end = sw->split_entry_list[sw->split_size - 1];
	} else {
		time_end = sw->split_entry_list[(new_entry_pos - 1) % sw->split_size];
	}
	return (time_start - time_end) / QB_TIME_NS_IN_USEC;
}

uint32_t
qb_util_stopwatch_split_last(qb_util_stopwatch_t *sw)
{
	if (sw->split_entries) {
		return sw->split_entries - 1;
	}
	return sw->split_entries;
}

uint64_t
qb_util_stopwatch_time_split_get(qb_util_stopwatch_t *sw,
				 uint32_t receint, uint32_t older)
{
	uint64_t time_start;
	uint64_t time_end;

	if (sw->started == 0 ||
	    receint >= sw->split_entries ||
	    older >= sw->split_entries ||
	    receint < older) {
		return 0;
	}
	if (sw->split_options & QB_UTIL_SW_OVERWRITE &&
	    (receint < (sw->split_entries - sw->split_size) ||
	     older < (sw->split_entries - sw->split_size))) {
		return 0;
	}

	time_start = sw->split_entry_list[receint % (sw->split_size)];
	if (older == receint) {
		time_end = sw->started;
	} else {
		time_end = sw->split_entry_list[older % (sw->split_size)];
	}
	return (time_start - time_end) / QB_TIME_NS_IN_USEC;
}

const struct qb_version qb_ver = {
	.major = QB_VER_MAJOR,
	.minor = QB_VER_MINOR,
	.micro = QB_VER_MICRO,
	.rest = QB_VER_REST,
};

const char *const qb_ver_str = QB_VER_STR;
