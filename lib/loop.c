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

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include "loop_int.h"

static int32_t qb_loop_run_level(struct qb_loop_level *level)
{
	struct qb_loop_item *job;
	struct qb_list_head *iter;
	int32_t processed = 0;

 Ill_have_another:

	iter = level->job_head.next;
	if (iter != &level->job_head) {
		job = qb_list_entry(iter, struct qb_loop_item, list);
		qb_list_del (&job->list);
		qb_list_init (&job->list);
		job->source->dispatch_and_take_back(job, level->priority);
		processed++;
		if (level->l->stop_requested) {
			return processed;
		}
		if (processed < level->to_process) {
			goto Ill_have_another;
		}
	}
	return processed;
}


struct qb_loop * qb_loop_create(void)
{
	struct qb_loop *l = malloc(sizeof(struct qb_loop));
	int32_t p;

	for (p = QB_LOOP_LOW; p <= QB_LOOP_HIGH; p++) {
		l->level[p].priority = p;
		l->level[p].to_process = 4;
		l->level[p].l = l;

		qb_list_init(&l->level[p].job_head);
		qb_list_init(&l->level[p].wait_head);
	}

	l->stop_requested = QB_FALSE;
	// install sources
	l->timer_source = qb_loop_timer_create(l);
	l->job_source = qb_loop_jobs_create(l);
	l->fd_source = qb_loop_poll_create(l);

	return l;
}

void qb_loop_destroy(struct qb_loop * l)
{
	qb_loop_timer_destroy(l);
	qb_loop_jobs_destroy(l);
	qb_loop_poll_destroy(l);
	free(l);
}

void qb_loop_stop(struct qb_loop *l)
{
	l->stop_requested = QB_TRUE;
}

void qb_loop_run(struct qb_loop *l)
{
	int32_t p;
	int32_t p_stop = QB_LOOP_LOW;
	int32_t todo = 0;
	int32_t ms_timeout;

	do {
		if (p_stop == QB_LOOP_LOW) {
			p_stop = QB_LOOP_HIGH;
		} else {
			p_stop--;
		}

		todo += l->job_source->poll(l->job_source, 0);
		if (l->timer_source) {
			todo += l->timer_source->poll(l->timer_source, 0);
		}
		if (todo > 0) {
			ms_timeout = 0;
		} else {
			todo = 0;
			if (l->timer_source) {
				ms_timeout = qb_loop_timer_msec_duration_to_expire(l->timer_source);
			} else {
				ms_timeout = -1;
			}
		}
		todo += l->fd_source->poll(l->fd_source, ms_timeout);

		for (p = QB_LOOP_HIGH; p >= p_stop; p--) {
			todo -= qb_loop_run_level(&l->level[p]);
			if (l->stop_requested) {
				return;
			}
		}
	} while (!l->stop_requested);
}

