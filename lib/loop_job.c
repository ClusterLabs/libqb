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

struct qb_loop_job {
	struct qb_loop_item item;
	qb_loop_job_dispatch_fn dispatch_fn;
};

static struct qb_loop_source * my_src;


static void job_dispatch(struct qb_loop_item * item,
			 enum qb_loop_priority p)
{
	struct qb_loop_job *job = qb_list_entry(item, struct qb_loop_job, item);

	job->dispatch_fn(job->item.user_data);
	free(job);

	// this is a one-shot so don't re-add
}

static int32_t get_more_jobs(struct qb_loop_source* s, int32_t ms_timeout)
{
	struct qb_list_head* iter;
	struct qb_list_head* iter_next;
	int32_t p;
	int32_t new_jobs = 0;

	// this is simple, move jobs from wait_head to job_head
	for (p = QB_LOOP_LOW; p <= QB_LOOP_HIGH; p++) {
		qb_list_splice(&s->l->level[p].wait_head, &s->l->level[p].job_head);
		qb_list_init(&s->l->level[p].wait_head);
		new_jobs += qb_list_length(&s->l->level[p].job_head);
	}
	return new_jobs;
}

struct qb_loop_source *
qb_loop_jobs_init(struct qb_loop *l)
{
	my_src = malloc(sizeof(struct qb_loop_source));
	my_src->l = l;
	my_src->dispatch_and_take_back = job_dispatch;
	my_src->poll = get_more_jobs;

	qb_list_init(&my_src->list);
	qb_list_add_tail(&my_src->list, &l->source_head);
	return my_src;
}

int32_t qb_loop_job_add(struct qb_loop *l,
			enum qb_loop_priority p,
			void *data,
			qb_loop_job_dispatch_fn dispatch_fn)
{
	struct qb_loop_job *job = malloc(sizeof(struct qb_loop_job));

	job->dispatch_fn = dispatch_fn;
	job->item.user_data = data;
	job->item.source = my_src;

	qb_list_init(&job->item.list);
	qb_list_add_tail(&job->item.list, &l->level[p].wait_head);

	return 0;
}

