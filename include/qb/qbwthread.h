/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef QB_WTHREAD_H_DEFINED
#define QB_WTHREAD_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

struct qb_wthread_group {
	int32_t threadcount;
	int32_t last_scheduled;
	struct qb_wthread_t *threads;
	void (*worker_fn) (void *thread_state, void *work_item);
};

int32_t qb_wthread_group_init(struct qb_wthread_group *worker_thread_group,
			  int32_t threads,
			  int32_t items_max,
			  int32_t item_size,
			  int32_t thread_state_size,
			  void (*thread_state_constructor) (void *),
			  void (*worker_fn) (void *thread_state,
					     void *work_item));

int32_t qb_wthread_group_work_add(struct qb_wthread_group
			      *worker_thread_group, void *item);

void qb_wthread_group_wait(struct qb_wthread_group *worker_thread_group);

void qb_wthread_group_exit(struct qb_wthread_group *worker_thread_group);

void qb_wthread_group_atsegv(struct qb_wthread_group
			     *worker_thread_group);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_WTHREAD_H_DEFINED */
