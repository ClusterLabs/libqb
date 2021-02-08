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
#include "os_base.h"
#include <pthread.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbarray.h>
#include <qb/qbloop.h>
#include "loop_int.h"
#include "util_int.h"
#include "tlist.h"

struct qb_loop_timer {
	struct qb_loop_item item;
	qb_loop_timer_dispatch_fn dispatch_fn;
	enum qb_loop_priority p;
	timer_handle timerlist_handle;
	enum qb_poll_entry_state state;
	int32_t check;
	uint32_t install_pos;
};

struct qb_timer_source {
	struct qb_loop_source s;
	struct timerlist timerlist;
	qb_array_t *timers;
	size_t timer_entry_count;
	pthread_mutex_t lock;
};

static void
timer_dispatch(struct qb_loop_item *item, enum qb_loop_priority p)
{
	struct qb_loop_timer *timer = (struct qb_loop_timer *)item;

	assert(timer->state == QB_POLL_ENTRY_JOBLIST);
	timer->check = 0;
	timer->dispatch_fn(timer->item.user_data);
	timer->state = QB_POLL_ENTRY_EMPTY;
}

static int32_t expired_timers;
static void
make_job_from_tmo(void *data)
{
	struct qb_loop_timer *t = (struct qb_loop_timer *)data;
	struct qb_loop *l = t->item.source->l;

	assert(t->state == QB_POLL_ENTRY_ACTIVE);
	qb_loop_level_item_add(&l->level[t->p], &t->item);
	t->state = QB_POLL_ENTRY_JOBLIST;
	expired_timers++;
}

static int32_t
expire_the_timers(struct qb_loop_source *s, int32_t ms_timeout)
{
	struct qb_timer_source *ts = (struct qb_timer_source *)s;
	expired_timers = 0;
	timerlist_expire(&ts->timerlist);
	return expired_timers;
}

int32_t
qb_loop_timer_msec_duration_to_expire(struct qb_loop_source * timer_source)
{
	struct qb_timer_source *my_src = (struct qb_timer_source *)timer_source;
	uint64_t left = timerlist_msec_duration_to_expire(&my_src->timerlist);
	if (left != -1 && left > 0xFFFFFFFF) {
		left = 0xFFFFFFFE;
	}
	return left;
}

struct qb_loop_source *
qb_loop_timer_create(struct qb_loop *l)
{
	struct qb_timer_source *my_src = malloc(sizeof(struct qb_timer_source));
	if (my_src == NULL) {
		return NULL;
	}
	my_src->s.l = l;
	my_src->s.dispatch_and_take_back = timer_dispatch;
	my_src->s.poll = expire_the_timers;

	timerlist_init(&my_src->timerlist);
	my_src->timers = qb_array_create_2(16, sizeof(struct qb_loop_timer), 16);
	my_src->timer_entry_count = 0;
	pthread_mutex_init(&my_src->lock, NULL);

	return (struct qb_loop_source *)my_src;
}

void
qb_loop_timer_destroy(struct qb_loop *l)
{
	struct qb_timer_source *my_src =
	    (struct qb_timer_source *)l->timer_source;
	qb_array_free(my_src->timers);
	free(l->timer_source);
}

static int32_t
_timer_from_handle_(struct qb_timer_source *s,
		    qb_loop_timer_handle handle_in,
		    struct qb_loop_timer **timer_pt)
{
	int32_t rc;
	int32_t check;
	uint32_t install_pos;
	struct qb_loop_timer *timer;

	if (handle_in == 0) {
		return -EINVAL;
	}

	check = handle_in >> 32;
	install_pos = handle_in & UINT32_MAX;

	rc = qb_array_index(s->timers, install_pos, (void **)&timer);
	if (rc != 0) {
		return rc;
	}
	if (timer->check != check) {
		return -EINVAL;
	}
	*timer_pt = timer;
	return 0;
}

static int32_t
_get_empty_array_position_(struct qb_timer_source *s)
{
	int32_t install_pos;
	int32_t res = 0;
	struct qb_loop_timer *timer;

	for (install_pos = 0; install_pos < s->timer_entry_count; install_pos++) {
		assert(qb_array_index(s->timers, install_pos, (void **)&timer)
		       == 0);
		if (timer->state == QB_POLL_ENTRY_EMPTY) {
			return install_pos;
		}
	}

	res = qb_array_grow(s->timers, s->timer_entry_count + 1);
	if (res != 0) {
		return res;
	}

	s->timer_entry_count++;
	install_pos = s->timer_entry_count - 1;
	return install_pos;
}

int32_t
qb_loop_timer_add(struct qb_loop * lp,
		  enum qb_loop_priority p,
		  uint64_t nsec_duration,
		  void *data,
		  qb_loop_timer_dispatch_fn timer_fn,
		  qb_loop_timer_handle * timer_handle_out)
{
	struct qb_loop_timer *t;
	struct qb_timer_source *my_src;
	int32_t i;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}

	if (l == NULL || timer_fn == NULL) {
		return -EINVAL;
	}
	my_src = (struct qb_timer_source *)l->timer_source;

	if (pthread_mutex_lock(&my_src->lock)) {
		return -errno;
	}
	i = _get_empty_array_position_(my_src);
	assert(qb_array_index(my_src->timers, i, (void **)&t) >= 0);
	t->state = QB_POLL_ENTRY_ACTIVE;
	t->install_pos = i;
	t->item.user_data = data;
	t->item.source = (struct qb_loop_source *)my_src;
	t->dispatch_fn = timer_fn;
	t->p = p;
	qb_list_init(&t->item.list);

	/* Unlock here to stop anyone else changing the state while we're initializing */
	pthread_mutex_unlock(&my_src->lock);

	/*
	 * Make sure just positive integers are used for the integrity(?)
	 * checks within 2^32 address space, if we miss 200 times in a row
	 * (just 0 is concerned per specification of random), the PRNG may be
	 * broken -> the value is unspecified, subject of previous assignment.
	 */
	for (i = 0; i < 200; i++) {
		t->check = random();

		if (t->check > 0) {
			break;  /* covers also t->check == UINT32_MAX */
		}
	}

	if (timer_handle_out) {
		*timer_handle_out = (((uint64_t) (t->check)) << 32) | t->install_pos;
	}
	return timerlist_add_duration(&my_src->timerlist,
				      make_job_from_tmo, t,
				      nsec_duration, &t->timerlist_handle);
}

int32_t
qb_loop_timer_del(struct qb_loop * lp, qb_loop_timer_handle th)
{
	struct qb_timer_source *s;
	struct qb_loop_timer *t;
	int32_t res;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	s = (struct qb_timer_source *)l->timer_source;

	res = _timer_from_handle_(s, th, &t);
	if (res != 0) {
		return res;
	}

	if (t->state == QB_POLL_ENTRY_DELETED) {
		qb_util_log(LOG_WARNING, "timer already deleted");
		return 0;
	}
	if (t->state != QB_POLL_ENTRY_ACTIVE &&
	    t->state != QB_POLL_ENTRY_JOBLIST) {
		return -EINVAL;
	}
	if (t->state == QB_POLL_ENTRY_JOBLIST) {
		qb_loop_level_item_del(&l->level[t->p], &t->item);
	}

	if (t->timerlist_handle) {
		timerlist_del(&s->timerlist, t->timerlist_handle);
	}
	t->state = QB_POLL_ENTRY_EMPTY;
	return 0;
}

uint64_t
qb_loop_timer_expire_time_get(struct qb_loop * lp, qb_loop_timer_handle th)
{
	struct qb_timer_source *s;
	struct qb_loop_timer *t;
	int32_t res;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	s = (struct qb_timer_source *)l->timer_source;

	res = _timer_from_handle_(s, th, &t);
	if (res != 0) {
		return 0;
	}

	if (t->state != QB_POLL_ENTRY_ACTIVE) {
		return 0;
	}

	return timerlist_expire_time(&s->timerlist, t->timerlist_handle);
}

uint64_t
qb_loop_timer_expire_time_remaining(struct qb_loop * lp, qb_loop_timer_handle th)
{

	uint64_t current_ns;
	/* NOTE: while it does not appear that absolute timers are used anywhere,
	 * we may as well respect this pattern in case that changes.
	 * Unfortunately, that means we do need to repeat timer fetch code from qb_loop_timer_expire_time_get
	 * rather than just a simple call to qb_loop_timer_expire_time_get and qb_util_nano_current_get.
	 */

	struct qb_timer_source *s;
	struct qb_loop_timer *t;
	int32_t res;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	s = (struct qb_timer_source *)l->timer_source;

	res = _timer_from_handle_(s, th, &t);
	if (res != 0) {
		return 0;
	}

	struct timerlist_timer *timer = (struct timerlist_timer *)t->timerlist_handle;


	if (timer->is_absolute_timer) {
		current_ns = qb_util_nano_from_epoch_get();
	}
	else {
		current_ns = qb_util_nano_current_get();
	}
	uint64_t timer_ns = timerlist_expire_time(&s->timerlist, t->timerlist_handle);
	/* since time estimation is racy by nature, I'll try to check the state late,
	 * and try to understand that no matter what, the timer might have expired in the mean time
	 */
	if (t->state != QB_POLL_ENTRY_ACTIVE) {
		return 0;
	}
	if (timer_ns < current_ns) {
		return 0; // respect the "expired" contract
	}
	return timer_ns - current_ns;


}

int32_t
qb_loop_timer_is_running(qb_loop_t *l, qb_loop_timer_handle th)
{
	return (qb_loop_timer_expire_time_get(l, th) > 0);
}
