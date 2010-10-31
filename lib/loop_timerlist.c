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
#include <qb/qbloop.h>
#include "loop_int.h"
#include "tlist.h"

struct qb_loop_timer {
	struct qb_loop_item item;
	qb_loop_timer_dispatch_fn dispatch_fn;
	enum qb_loop_priority p;
};

struct qb_timer_source {
	struct qb_loop_source s;
	struct timerlist timerlist;
};

static void timer_dispatch(struct qb_loop_item * item,
		enum qb_loop_priority p)
{
	struct qb_loop_timer *timer = (struct qb_loop_timer *)item;

	timer->dispatch_fn(timer->item.user_data);
	free(timer);
}

static int32_t expired_timers;
static void make_job_from_tmo(void *data)
{
	struct qb_loop_timer *t = (struct qb_loop_timer *)data;
	struct qb_loop *l = t->item.source->l;
	qb_list_init(&t->item.list);
	qb_list_add_tail(&t->item.list, &l->level[t->p].job_head);
	expired_timers++;
}

static int32_t expire_the_timers(struct qb_loop_source* s, int32_t ms_timeout)
{
	struct qb_timer_source *ts = (struct qb_timer_source *)s;
	expired_timers = 0;
	timerlist_expire(&ts->timerlist);
	return expired_timers;
}

int32_t qb_loop_timer_msec_duration_to_expire(struct qb_loop_source *timer_source)
{
	struct qb_timer_source * my_src = (struct qb_timer_source *)timer_source;
	uint64_t left = timerlist_msec_duration_to_expire(&my_src->timerlist);
	if (left != -1 && left > 0xFFFFFFFF) {
		left = 0xFFFFFFFE;
	}
	return left;
}

struct qb_loop_source*
qb_loop_timer_create(struct qb_loop *l)
{
	struct qb_timer_source * my_src = malloc(sizeof(struct qb_timer_source));
	my_src->s.l = l;
	my_src->s.dispatch_and_take_back = timer_dispatch;
	my_src->s.poll = expire_the_timers;

	timerlist_init(&my_src->timerlist);

	return (struct qb_loop_source*)my_src;
}


void qb_loop_timer_destroy(struct qb_loop *l)
{
	free(l->timer_source);
}

int32_t qb_loop_timer_add(struct qb_loop *l,
			  enum qb_loop_priority p,
			  int32_t msec_duration,
			  void *data,
			  qb_loop_timer_dispatch_fn timer_fn,
			  qb_loop_timer_handle * timer_handle_out)
{
	struct qb_loop_timer *t;
	struct qb_timer_source * my_src;

	if (l == NULL || timer_fn == NULL) {
		return -EINVAL;
	}
	my_src = (struct qb_timer_source *)l->timer_source;
	if (timer_handle_out == NULL) {
		return -ENOENT;
	}
	t = malloc(sizeof(struct qb_loop_timer));
	t->item.user_data = data;
	t->item.source = (struct qb_loop_source*)my_src;
	t->dispatch_fn = timer_fn;
	t->p = p;
	qb_list_init(&t->item.list);

	return timerlist_add_duration(&my_src->timerlist,
			       make_job_from_tmo, t,
			       ((uint64_t)msec_duration) * QB_TIME_NS_IN_MSEC,
			       timer_handle_out);
}

int32_t qb_loop_timer_del(struct qb_loop *l, qb_loop_timer_handle th)
{
	struct qb_timer_source * my_src = (struct qb_timer_source *)l->timer_source;
	if (th == NULL) {
		return -EINVAL;
	}

	timerlist_del(&my_src->timerlist, (void *)th);
	return 0;
}

uint64_t qb_loop_timer_expire_time_get(struct qb_loop *l, qb_loop_timer_handle th)
{
	struct qb_timer_source * my_src = (struct qb_timer_source *)l->timer_source;
	if (th == 0) {
		return 0;
	}
	return timerlist_expire_time (&my_src->timerlist, th);
}

