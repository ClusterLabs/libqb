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
#include "util_int.h"

static struct qb_loop *default_instance = NULL;

static void
qb_loop_run_level(struct qb_loop_level *level)
{
	struct qb_loop_item *job;
	int32_t processed = 0;

Ill_have_another:

	if (!qb_list_empty(&level->job_head)) {
		job = qb_list_first_entry(&level->job_head, struct qb_loop_item, list);
		qb_list_del(&job->list);
		qb_list_init(&job->list);
		job->source->dispatch_and_take_back(job, level->priority);
		level->todo--;
		processed++;
		if (level->l->stop_requested) {
			return;
		}
		if (processed < level->to_process) {
			goto Ill_have_another;
		}
	}
}

void
qb_loop_level_item_add(struct qb_loop_level *level, struct qb_loop_item *job)
{
	qb_list_init(&job->list);
	qb_list_add_tail(&job->list, &level->job_head);
	level->todo++;
}

void
qb_loop_level_item_del(struct qb_loop_level *level, struct qb_loop_item *job)
{
	/*
	 * We may be deleted during dispatch... don't double-decrement todo.
	 */
	if (qb_list_empty(&job->list)) {
		return;
	}
	qb_list_del(&job->list);
	qb_list_init(&job->list);
	level->todo--;
}

struct qb_loop *
qb_loop_default_get(void)
{
	return default_instance;
}

struct qb_loop *
qb_loop_create(void)
{
	struct qb_loop *l = malloc(sizeof(struct qb_loop));
	int32_t p;

	if (l == NULL) {
		return NULL;
	}
	for (p = QB_LOOP_LOW; p <= QB_LOOP_HIGH; p++) {
		l->level[p].priority = p;
		l->level[p].to_process = 4;
		l->level[p].todo = 0;
		l->level[p].l = l;

		qb_list_init(&l->level[p].job_head);
		qb_list_init(&l->level[p].wait_head);
	}

	l->stop_requested = QB_FALSE;
	l->timer_source = qb_loop_timer_create(l);
	l->job_source = qb_loop_jobs_create(l);
	l->fd_source = qb_loop_poll_create(l);
	l->signal_source = qb_loop_signals_create(l);

	if (default_instance == NULL) {
		default_instance = l;
	}
	return l;
}

void
qb_loop_destroy(struct qb_loop *l)
{
	qb_loop_timer_destroy(l);
	qb_loop_jobs_destroy(l);
	qb_loop_poll_destroy(l);
	qb_loop_signals_destroy(l);

	if (default_instance == l) {
		default_instance = NULL;
	}
	free(l);
}

void
qb_loop_stop(struct qb_loop *l)
{
	struct qb_loop *apply_loop = (l != NULL) ? l : default_instance;
	if (apply_loop != NULL) {
		apply_loop->stop_requested = QB_TRUE;
	} else {
		qb_util_log(LOG_CRIT, "API misuse: cannot stop nonexisting loop");
	}
}

void
qb_loop_run(struct qb_loop *lp)
{
	int32_t p;
	int32_t p_stop = QB_LOOP_LOW;
	int32_t rc;
	int32_t remaining_todo = 0;
	int32_t job_todo;
	int32_t timer_todo;
	int32_t ms_timeout;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = default_instance;
	}
	l->stop_requested = QB_FALSE;

	do {
		if (p_stop == QB_LOOP_LOW) {
			p_stop = QB_LOOP_HIGH;
		} else {
			p_stop--;
		}

		job_todo = 0;
		if (l->job_source && l->job_source->poll) {
			rc = l->job_source->poll(l->job_source, 0);
			if (rc > 0) {
				job_todo = rc;
			} else if (rc == -1) {
				errno = -rc;
				qb_util_perror(LOG_WARNING, "job->poll");
			}
		}
		timer_todo = 0;
		if (l->timer_source && l->timer_source->poll) {
			rc = l->timer_source->poll(l->timer_source, 0);
			if (rc > 0) {
				timer_todo = rc;
			} else if (rc == -1) {
				errno = -rc;
				qb_util_perror(LOG_WARNING, "timer->poll");
			}
		}
		if (remaining_todo > 0 || timer_todo > 0) {
			/*
			 * if there are remaining todos or timer todos then don't wait.
			 */
			ms_timeout = 0;
		} else if (job_todo > 0) {
			/*
			 * if we only have jobs to do (not timers or old todos)
			 * then set a non-zero timeout. Jobs can spin out of
			 * control if someone keeps adding them.
			 */
			ms_timeout = 50;
		} else {
			if (l->timer_source) {
				ms_timeout = qb_loop_timer_msec_duration_to_expire(l->timer_source);
			} else {
				ms_timeout = -1;
			}
		}
		rc = l->fd_source->poll(l->fd_source, ms_timeout);
		if (rc < 0) {
			errno = -rc;
			qb_util_perror(LOG_WARNING, "fd->poll");
		}

		remaining_todo = 0;
		for (p = QB_LOOP_HIGH; p >= QB_LOOP_LOW; p--) {
			if (p >= p_stop) {
				qb_loop_run_level(&l->level[p]);
				if (l->stop_requested) {
					return;
				}
			}
			remaining_todo += l->level[p].todo;
		}
	} while (!l->stop_requested);
}
