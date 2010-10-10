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

static struct qb_loop_source * timer_source;
static struct qb_loop_source * job_source;
static struct qb_loop_source * fd_source;

static int32_t qb_loop_run_level(struct qb_loop_level *level)
{
	struct qb_loop_item *job;
	struct qb_list_head *iter, *iter_next;
	int32_t processed = 0;

	for (iter = level->job_head.next;
			iter != &level->job_head;
			iter = iter_next) {
		if (processed >= level->to_process) {
			break;
		}
		iter_next = iter->next;
		job = qb_list_entry(iter, struct qb_loop_item, list);
		qb_list_del (&job->list);
		qb_list_init (&job->list);
		job->source->dispatch_and_take_back(job, level->priority);
		if (level->l->stop_requested) {
			printf("%s:%d pr:%d STOP!!!\n", __func__, __LINE__, level->priority);
			return processed;
		}
		processed++;
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
	qb_list_init(&l->source_head);
	// install sources
	timer_source = qb_loop_timer_init(l);
	job_source = qb_loop_jobs_init(l);
	fd_source = qb_loop_poll_init(l);

	return l;
}

void qb_loop_stop(struct qb_loop *l)
{
	l->stop_requested = QB_TRUE;
}

void qb_loop_run(struct qb_loop *l)
{
	int32_t p;
	int32_t p_stop;
	int32_t todo;
	int32_t done;
	int32_t ms_timeout;
	int32_t fd_poll_done = QB_FALSE;

	do {
		p_stop = QB_LOOP_HIGH;
		todo = 0;
 poll_again:
		if (!fd_poll_done) {
			todo += fd_source->poll(fd_source, 0);
		}
		todo += job_source->poll(job_source, 0);
		todo += timer_source->poll(timer_source, 0);

		for (p = QB_LOOP_HIGH; p >= p_stop; p--) {
			done = qb_loop_run_level(&l->level[p]);
			if (l->stop_requested) {
				return;
			}
			todo -= done;
		}
		if (p_stop > QB_LOOP_LOW) {
			p_stop--;
			fd_poll_done = QB_FALSE;
			goto poll_again;
		}
		if (todo == 0) {
			ms_timeout = qb_loop_timer_msec_duration_to_expire(timer_source);
			todo = fd_source->poll(fd_source, ms_timeout);
			fd_poll_done = QB_TRUE;
		} else {
			fd_poll_done = QB_FALSE;
		}
	} while (!l->stop_requested);
}

