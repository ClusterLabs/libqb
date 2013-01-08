/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
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

struct qb_loop_job {
	struct qb_loop_item item;
	qb_loop_job_dispatch_fn dispatch_fn;
};

static void
job_dispatch(struct qb_loop_item *item, enum qb_loop_priority p)
{
	struct qb_loop_job *job = qb_list_entry(item, struct qb_loop_job, item);

	job->dispatch_fn(job->item.user_data);
	free(job);

	/*
	 * this is a one-shot so don't re-add
	 */
}

static int32_t
get_more_jobs(struct qb_loop_source *s, int32_t ms_timeout)
{
	int32_t p;
	int32_t new_jobs = 0;
	int32_t level_jobs = 0;

	/*
	 * this is simple, move jobs from wait_head to job_head
	 */
	for (p = QB_LOOP_LOW; p <= QB_LOOP_HIGH; p++) {
		if (!qb_list_empty(&s->l->level[p].wait_head)) {
			level_jobs = qb_list_length(&s->l->level[p].wait_head);
			new_jobs += level_jobs;
			qb_list_splice_tail(&s->l->level[p].wait_head,
				            &s->l->level[p].job_head);
			qb_list_init(&s->l->level[p].wait_head);
			s->l->level[p].todo += level_jobs;
		}
	}
	return new_jobs;
}

struct qb_loop_source *
qb_loop_jobs_create(struct qb_loop *l)
{
	struct qb_loop_source *s = malloc(sizeof(struct qb_loop_source));
	if (s == NULL) {
		return NULL;
	}
	s->l = l;
	s->dispatch_and_take_back = job_dispatch;
	s->poll = get_more_jobs;

	return s;
}

void
qb_loop_jobs_destroy(struct qb_loop *l)
{
	free(l->job_source);
}

int32_t
qb_loop_job_add(struct qb_loop *lp,
		enum qb_loop_priority p,
		void *data, qb_loop_job_dispatch_fn dispatch_fn)
{
	struct qb_loop_job *job;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	if (l == NULL || dispatch_fn == NULL) {
		return -EINVAL;
	}
	if (p < QB_LOOP_LOW || p > QB_LOOP_HIGH) {
		return -EINVAL;
	}
	job = malloc(sizeof(struct qb_loop_job));
	if (job == NULL) {
		return -ENOMEM;
	}

	job->dispatch_fn = dispatch_fn;
	job->item.user_data = data;
	job->item.source = l->job_source;
	job->item.type = QB_LOOP_JOB;

	qb_list_init(&job->item.list);
	qb_list_add_tail(&job->item.list, &l->level[p].wait_head);

	return 0;
}

int32_t
qb_loop_job_del(struct qb_loop *lp,
		enum qb_loop_priority p,
		void *data, qb_loop_job_dispatch_fn dispatch_fn)
{
	struct qb_loop_job *job;
	struct qb_loop_item *item;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	if (l == NULL || dispatch_fn == NULL) {
		return -EINVAL;
	}
	if (p > QB_LOOP_HIGH) {
		return -EINVAL;
	}

	qb_list_for_each_entry(item, &l->level[p].wait_head, list) {
		job = (struct qb_loop_job *)item;
		if (job->dispatch_fn == dispatch_fn &&
		    job->item.user_data == data &&
		    job->item.type == QB_LOOP_JOB) {
			qb_list_del(&job->item.list);
			free(job);
			return 0;
		}
	}

	qb_list_for_each_entry(item, &l->level[p].job_head, list) {

		if (item->type != QB_LOOP_JOB) {
			continue;
		}
		job = (struct qb_loop_job *)item;
		if (job->dispatch_fn == dispatch_fn &&
		    job->item.user_data == data) {
			qb_loop_level_item_del(&l->level[p], item);
			qb_util_log(LOG_DEBUG, "deleting job in JOBLIST");
			return 0;
		}
	}

	return -ENOENT;
}
