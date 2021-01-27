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

#ifndef QB_TLIST_H_DEFINED
#define QB_TLIST_H_DEFINED

#include "os_base.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblist.h>

#ifndef TIMER_HANDLE
typedef void *timer_handle;
#define TIMER_HANDLE
#endif

static int64_t timerlist_hertz;

struct timerlist {
	struct qb_list_head timer_head;
	pthread_mutex_t list_mutex;
};

struct timerlist_timer {
	struct qb_list_head list;
	uint64_t expire_time;
	int32_t is_absolute_timer;
	void (*timer_fn) (void *data);
	void *data;
	timer_handle handle_addr;
};

static inline void timerlist_init(struct timerlist *timerlist)
{
	qb_list_init(&timerlist->timer_head);
	pthread_mutex_init(&timerlist->list_mutex, NULL);
	timerlist_hertz = qb_util_nano_monotonic_hz();
}

static inline int32_t timerlist_add(struct timerlist *timerlist,
				 struct timerlist_timer *timer)
{
	struct qb_list_head *timer_list = 0;
	struct timerlist_timer *timer_from_list;
	int32_t found = QB_FALSE;

	if (pthread_mutex_lock(&timerlist->list_mutex)) {
		return -errno;
	}
	qb_list_for_each(timer_list, &timerlist->timer_head) {

		timer_from_list = qb_list_entry(timer_list,
						struct timerlist_timer, list);

		if (timer_from_list->expire_time > timer->expire_time) {
			qb_list_add_tail(&timer->list, timer_list);
			found = QB_TRUE;
			break;	/* for timer iteration */
		}
	}
	if (found == QB_FALSE) {
		qb_list_add_tail(&timer->list, &timerlist->timer_head);
	}
	pthread_mutex_unlock(&timerlist->list_mutex);
	return 0;
}

static inline int32_t timerlist_add_duration(struct timerlist *timerlist,
					 void (*timer_fn) (void *data),
					 void *data,
					 uint64_t nano_duration,
					 timer_handle * handle)
{
	int res;
	struct timerlist_timer *timer;

	timer =
	    (struct timerlist_timer *)malloc(sizeof(struct timerlist_timer));
	if (timer == 0) {
		return -ENOMEM;
	}

	timer->expire_time = qb_util_nano_current_get() + nano_duration;
	timer->is_absolute_timer = QB_FALSE;
	timer->data = data;
	timer->timer_fn = timer_fn;
	timer->handle_addr = handle;
	res = timerlist_add(timerlist, timer);
	if (res) {
		free(timer);
		return res;
	}

	*handle = timer;
	return (0);
}

static inline void timerlist_del(struct timerlist *timerlist,
				 timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	memset(timer->handle_addr, 0, sizeof(struct timerlist_timer *));
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
static inline uint64_t timerlist_msec_duration_to_expire(struct timerlist *timerlist)
{
	struct timerlist_timer *timer_from_list;
	volatile uint64_t current_time;
	volatile uint64_t msec_duration_to_expire;

	/*
	 * empty list, no expire
	 */
	if (qb_list_empty(&timerlist->timer_head)) {
		return (-1);
	}

	timer_from_list = qb_list_first_entry(&timerlist->timer_head,
					struct timerlist_timer, list);

	if (timer_from_list->is_absolute_timer) {
		current_time = qb_util_nano_from_epoch_get();
	} else {
		current_time = qb_util_nano_current_get();
	}

	/*
	 * timer at head of list is expired, zero msecs required
	 */
	if (timer_from_list->expire_time < current_time) {
		return (0);
	}

	msec_duration_to_expire =
	    ((timer_from_list->expire_time -
	      current_time) / QB_TIME_NS_IN_MSEC) + (1000 / timerlist_hertz);
	return (msec_duration_to_expire);
}

/*
 * Expires any timers that should be expired
 */
static inline void timerlist_expire(struct timerlist *timerlist)
{
	struct timerlist_timer *timer_from_list;
	struct qb_list_head *pos;
	struct qb_list_head *next;
	uint64_t current_time_from_epoch;
	uint64_t current_monotonic_time;
	uint64_t current_time;

	current_monotonic_time = qb_util_nano_current_get();
	current_time_from_epoch = qb_util_nano_from_epoch_get();

	qb_list_for_each_safe(pos, next, &timerlist->timer_head) {

		timer_from_list = qb_list_entry(pos,
						struct timerlist_timer, list);

		current_time =
		    (timer_from_list->
		     is_absolute_timer ? current_time_from_epoch :
		     current_monotonic_time);

		if (timer_from_list->expire_time < current_time) {

			timerlist_pre_dispatch(timerlist, timer_from_list);

			timer_from_list->timer_fn(timer_from_list->data);

			timerlist_post_dispatch(timerlist, timer_from_list);
		} else {
			break;	/* for timer iteration */
		}
	}
}
#endif /* QB_TLIST_H_DEFINED */
