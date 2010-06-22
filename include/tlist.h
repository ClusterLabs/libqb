/*
 * Copyright (c) 2006-2007, 2009 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>
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

#ifndef TLIST_H_DEFINED
#define TLIST_H_DEFINED

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include <qb/qblist.h>

#ifndef HZ
#define HZ 100			/* 10ms */
#endif

#ifndef TIMER_HANDLE
typedef void *timer_handle;
#define TIMER_HANDLE
#endif

#define TIMERLIST_MS_IN_SEC   1000ULL
#define TIMERLIST_US_IN_SEC   1000000ULL
#define TIMERLIST_NS_IN_SEC   1000000000ULL
#define TIMERLIST_US_IN_MSEC  1000ULL
#define TIMERLIST_NS_IN_MSEC  1000000ULL
#define TIMERLIST_NS_IN_USEC  1000ULL

struct timerlist {
	struct qb_list_head timer_head;
	struct qb_list_head *timer_iter;
};

struct timerlist_timer {
	struct qb_list_head list;
	uint64_t expire_time;
	int is_absolute_timer;
	void (*timer_fn) (void *data);
	void *data;
	timer_handle handle_addr;
};

static inline void timerlist_init(struct timerlist *timerlist)
{
	qb_list_init(&timerlist->timer_head);
}

static inline uint64_t timerlist_nano_from_epoch(void)
{
	uint64_t nano_from_epoch;
	struct timeval time_from_epoch;
	gettimeofday(&time_from_epoch, 0);

	nano_from_epoch = ((time_from_epoch.tv_sec * TIMERLIST_NS_IN_SEC) +
			   (time_from_epoch.tv_usec * TIMERLIST_NS_IN_USEC));

	return (nano_from_epoch);
}

#if defined _POSIX_MONOTONIC_CLOCK && _POSIX_MONOTONIC_CLOCK >= 0
static inline uint64_t timerlist_nano_current_get(void)
{
	uint64_t nano_monotonic;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	nano_monotonic =
	    (ts.tv_sec * TIMERLIST_NS_IN_SEC) + (uint64_t)ts.tv_nsec;
	return (nano_monotonic);
}

static inline uint64_t timerlist_nano_monotonic_hz(void)
{
	uint64_t nano_monotonic_hz;
	struct timespec ts;

	clock_getres(CLOCK_MONOTONIC, &ts);

	nano_monotonic_hz =
	    TIMERLIST_NS_IN_SEC / ((ts.tv_sec * TIMERLIST_NS_IN_SEC) +
				   ts.tv_nsec);

	return (nano_monotonic_hz);
}
#else
#warning "Your system doesn't support monotonic timer. gettimeofday will be used"
static inline uint64_t timerlist_nano_current_get(void)
{
	return (timerlist_nano_from_epoch());
}

static inline uint64_t timerlist_nano_monotonic_hz(void)
{
	return HZ;
}
#endif

static inline void timerlist_add(struct timerlist *timerlist,
				 struct timerlist_timer *timer)
{
	struct qb_list_head *timer_list = 0;
	struct timerlist_timer *timer_from_list;
	int found;

	for (found = 0, timer_list = timerlist->timer_head.next;
	     timer_list != &timerlist->timer_head;
	     timer_list = timer_list->next) {

		timer_from_list = qb_list_entry(timer_list,
						struct timerlist_timer, list);

		if (timer_from_list->expire_time > timer->expire_time) {
			qb_list_add(&timer->list, timer_list->prev);
			found = 1;
			break;	/* for timer iteration */
		}
	}
	if (found == 0) {
		qb_list_add(&timer->list, timerlist->timer_head.prev);
	}
}

static inline int timerlist_add_absolute(struct timerlist *timerlist,
					 void (*timer_fn) (void *data),
					 void *data,
					 uint64_t nano_from_epoch,
					 timer_handle * handle)
{
	struct timerlist_timer *timer;

	timer =
	    (struct timerlist_timer *)malloc(sizeof(struct timerlist_timer));
	if (timer == 0) {
		errno = ENOMEM;
		return (-1);
	}

	timer->expire_time = nano_from_epoch;
	timer->is_absolute_timer = 1;
	timer->data = data;
	timer->timer_fn = timer_fn;
	timer->handle_addr = handle;
	timerlist_add(timerlist, timer);

	*handle = timer;
	return (0);
}

static inline int timerlist_add_duration(struct timerlist *timerlist,
					 void (*timer_fn) (void *data),
					 void *data,
					 uint64_t nano_duration,
					 timer_handle * handle)
{
	struct timerlist_timer *timer;

	timer =
	    (struct timerlist_timer *)malloc(sizeof(struct timerlist_timer));
	if (timer == 0) {
		errno = ENOMEM;
		return (-1);
	}

	timer->expire_time = timerlist_nano_current_get() + nano_duration;
	timer->is_absolute_timer = 0;
	timer->data = data;
	timer->timer_fn = timer_fn;
	timer->handle_addr = handle;
	timerlist_add(timerlist, timer);

	*handle = timer;
	return (0);
}

static inline void timerlist_del(struct timerlist *timerlist,
				 timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	memset(timer->handle_addr, 0, sizeof(struct timerlist_timer *));
	/*
	 * If the next timer after the currently expiring timer because
	 * timerlist_del is called from a timer handler, get to the next
	 * timer
	 */
	if (timerlist->timer_iter == &timer->list) {
		timerlist->timer_iter = timerlist->timer_iter->next;
	}
	qb_list_del(&timer->list);
	qb_list_init(&timer->list);
	free(timer);
}

static inline uint64_t timerlist_expire_time(struct timerlist
						       *timerlist,
						       timer_handle
						       _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	return (timer->expire_time);
}

static inline void timerlist_pre_dispatch(struct timerlist *timerlist,
					  timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	memset(timer->handle_addr, 0, sizeof(struct timerlist_timer *));
	qb_list_del(&timer->list);
	qb_list_init(&timer->list);
}

static inline void timerlist_post_dispatch(struct timerlist *timerlist,
					   timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	free(timer);
}

/*
 * returns the number of msec until the next timer will expire for use with poll
 */
static inline uint64_t timerlist_msec_duration_to_expire(struct
								   timerlist
								   *timerlist)
{
	struct timerlist_timer *timer_from_list;
	volatile uint64_t current_time;
	volatile uint64_t msec_duration_to_expire;

	/*
	 * empty list, no expire
	 */
	if (timerlist->timer_head.next == &timerlist->timer_head) {
		return (-1);
	}

	timer_from_list = qb_list_entry(timerlist->timer_head.next,
					struct timerlist_timer, list);

	if (timer_from_list->is_absolute_timer) {
		current_time = timerlist_nano_from_epoch();
	} else {
		current_time = timerlist_nano_current_get();
	}

	/*
	 * timer at head of list is expired, zero msecs required
	 */
	if (timer_from_list->expire_time < current_time) {
		return (0);
	}

	msec_duration_to_expire =
	    ((timer_from_list->expire_time -
	      current_time) / TIMERLIST_NS_IN_MSEC) + (1000 / HZ);
	return (msec_duration_to_expire);
}

/*
 * Expires any timers that should be expired
 */
static inline void timerlist_expire(struct timerlist *timerlist)
{
	struct timerlist_timer *timer_from_list;
	uint64_t current_time_from_epoch;
	uint64_t current_monotonic_time;
	uint64_t current_time;

	current_monotonic_time = timerlist_nano_current_get();
	current_time_from_epoch = current_time = timerlist_nano_from_epoch();

	for (timerlist->timer_iter = timerlist->timer_head.next;
	     timerlist->timer_iter != &timerlist->timer_head;) {

		timer_from_list = qb_list_entry(timerlist->timer_iter,
						struct timerlist_timer, list);

		current_time =
		    (timer_from_list->
		     is_absolute_timer ? current_time_from_epoch :
		     current_monotonic_time);

		if (timer_from_list->expire_time < current_time) {
			timerlist->timer_iter = timerlist->timer_iter->next;

			timerlist_pre_dispatch(timerlist, timer_from_list);

			timer_from_list->timer_fn(timer_from_list->data);

			timerlist_post_dispatch(timerlist, timer_from_list);
		} else {
			break;	/* for timer iteration */
		}
	}
	timerlist->timer_iter = 0;
}
#endif /* TLIST_H_DEFINED */
