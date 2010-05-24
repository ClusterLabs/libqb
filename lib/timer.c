/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
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

#include <config.h>

#include "os_base.h"

#include <poll.h>
#include <signal.h>
#include <time.h>

#include "tlist.h"

#include <qb/qblist.h>
#include <qb/qbpoll.h>
#include <qb/qbtimer.h>


#define SERVER_BACKLOG 5

static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t expiry_thread;

static pthread_attr_t thread_attr;

static struct timerlist timers_timerlist;

static int sched_priority = 0;

static void (*timer_serialize_lock_fn) (void);

static void (*timer_serialize_unlock_fn) (void);

static void *prioritized_timer_thread (void *data);

extern void pthread_exit(void *) __attribute__((noreturn));

/*
 * This thread runs at the highest priority to run system wide timers
 */
static void *prioritized_timer_thread (void *data)
{
	int fds;
	unsigned long long timeout;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX)
	if (sched_priority != 0) {
		struct sched_param sched_param;

		sched_param.sched_priority = sched_priority;
		pthread_setschedparam (expiry_thread, SCHED_RR, &sched_param);
	}
#endif

	pthread_mutex_unlock (&timer_mutex);
	for (;;) {
		timer_serialize_lock_fn ();
		timeout = timerlist_msec_duration_to_expire (&timers_timerlist);
		if (timeout != -1 && timeout > 0xFFFFFFFF) {
			timeout = 0xFFFFFFFE;
		}
		timer_serialize_unlock_fn ();
		fds = poll (NULL, 0, timeout);
		if (fds < 0 && errno == EINTR) {
			continue;
		}
		if (fds < 0) {
			return NULL;
		}
		pthread_mutex_lock (&timer_mutex);
		timer_serialize_lock_fn ();

		timerlist_expire (&timers_timerlist);

		timer_serialize_unlock_fn ();
		pthread_mutex_unlock (&timer_mutex);
	}
}

static void sigusr1_handler (int num) {
#ifdef qb_SOLARIS
	/* Rearm the signal facility */
        signal (num, sigusr1_handler);
#endif
}

int qb_timer_init (
        void (*serialize_lock_fn) (void),
        void (*serialize_unlock_fn) (void),
	int sched_priority_in)
{
	int res;

	timer_serialize_lock_fn = serialize_lock_fn;
	timer_serialize_unlock_fn = serialize_unlock_fn;
	sched_priority = sched_priority_in;

	timerlist_init (&timers_timerlist);

	signal (SIGUSR1, sigusr1_handler);

	pthread_mutex_lock (&timer_mutex);
	pthread_attr_init (&thread_attr);
	pthread_attr_setstacksize (&thread_attr, 100000);
	pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
	res = pthread_create (&expiry_thread, &thread_attr,
		prioritized_timer_thread, NULL);

	return (res);
}

int qb_timer_add_absolute (
	unsigned long long nanosec_from_epoch,
	void *data,
	void (*timer_fn) (void *data),
	timer_handle *handle)
{
	int res;
	int unlock;

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	res = timerlist_add_absolute (
		&timers_timerlist,
		timer_fn,
		data,
		nanosec_from_epoch,
		handle);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}

	pthread_kill (expiry_thread, SIGUSR1);

	return (res);
}

int qb_timer_add_duration (
	unsigned long long nanosec_duration,
	void *data,
	void (*timer_fn) (void *data),
	timer_handle *handle)
{
	int res;
	int unlock;

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	res = timerlist_add_duration (
		&timers_timerlist,
		timer_fn,
		data,
		nanosec_duration,
		handle);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}

	pthread_kill (expiry_thread, SIGUSR1);

	return (res);
}

void qb_timer_delete (
	timer_handle th)
{
	int unlock;

	if (th == 0) {
		return;
	}

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	timerlist_del (&timers_timerlist, th);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}
}

void qb_timer_lock (void)
{
	pthread_mutex_lock (&timer_mutex);
}

void qb_timer_unlock (void)
{
	pthread_mutex_unlock (&timer_mutex);
}

unsigned long long qb_timer_time_get (void)
{
	return (timerlist_nano_from_epoch());
}

unsigned long long qb_timer_expire_time_get (
	timer_handle th)
{
	int unlock;
	unsigned long long expire;

	if (th == 0) {
		return (0);
	}

	if (pthread_equal (pthread_self(), expiry_thread) != 0) {
		unlock = 0;
	} else {
		unlock = 1;
		pthread_mutex_lock (&timer_mutex);
	}

	expire = timerlist_expire_time (&timers_timerlist, th);

	if (unlock) {
		pthread_mutex_unlock (&timer_mutex);
	}

	return (expire);
}
