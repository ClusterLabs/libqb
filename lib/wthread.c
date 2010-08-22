/*
 * Copyright (C) 2006, 2010 Red Hat, Inc.
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
#include "os_base.h"

#include <pthread.h>
#include <qb/qbqueue.h>
#include <qb/qbwthread.h>

/*
 * Add work to a work group and have threads process the work
 * Provide blocking for all work to complete
 */


struct thread_data {
	void *thread_state;
	void *data;
};

struct qb_wthread_t {
	struct qb_wthread_group *worker_thread_group;
	pthread_mutex_t new_work_mutex;
	pthread_cond_t new_work_cond;
	pthread_cond_t cond;
	pthread_mutex_t done_work_mutex;
	pthread_cond_t done_work_cond;
	pthread_t thread_id;
	struct qb_queue queue;
	void *thread_state;
	struct thread_data thread_data;
};

static void *start_worker_thread(void *thread_data_in)
{
	struct thread_data *thread_data = (struct thread_data *)thread_data_in;
	struct qb_wthread_t *worker_thread =
	    (struct qb_wthread_t *)thread_data->data;
	void *data_for_worker_fn;

	for (;;) {
		pthread_mutex_lock(&worker_thread->new_work_mutex);
		if (qb_queue_is_empty(&worker_thread->queue) == 1) {
			pthread_cond_wait(&worker_thread->new_work_cond,
					  &worker_thread->new_work_mutex);
		}

		/*
		 * We unlock then relock the new_work_mutex to allow the
		 * worker function to execute and also allow new work to be
		 * added to the work queue
		 */
		data_for_worker_fn = qb_queue_item_get(&worker_thread->queue);
		pthread_mutex_unlock(&worker_thread->new_work_mutex);
		worker_thread->worker_thread_group->worker_fn(worker_thread->
							      thread_state,
							      data_for_worker_fn);
		pthread_mutex_lock(&worker_thread->new_work_mutex);
		qb_queue_item_remove(&worker_thread->queue);
		pthread_mutex_unlock(&worker_thread->new_work_mutex);
		pthread_mutex_lock(&worker_thread->done_work_mutex);
		if (qb_queue_is_empty(&worker_thread->queue) == 1) {
			pthread_cond_signal(&worker_thread->done_work_cond);
		}
		pthread_mutex_unlock(&worker_thread->done_work_mutex);
	}
	return (NULL);
}

int32_t qb_wthread_group_init(struct qb_wthread_group *worker_thread_group,
			  int32_t threads,
			  int32_t items_max,
			  int32_t item_size,
			  int32_t thread_state_size,
			  void (*thread_state_constructor) (void *),
			  void (*worker_fn) (void *thread_state,
					     void *work_item))
{
	int32_t i;

	worker_thread_group->threadcount = threads;
	worker_thread_group->last_scheduled = 0;
	worker_thread_group->worker_fn = worker_fn;
	worker_thread_group->threads = malloc(sizeof(struct qb_wthread_t) *
					      threads);
	if (worker_thread_group->threads == 0) {
		return (-1);
	}

	for (i = 0; i < threads; i++) {
		if (thread_state_size) {
			worker_thread_group->threads[i].thread_state =
			    malloc(thread_state_size);
		} else {
			worker_thread_group->threads[i].thread_state = NULL;
		}
		if (thread_state_constructor) {
			thread_state_constructor(worker_thread_group->threads
						 [i].thread_state);
		}
		worker_thread_group->threads[i].worker_thread_group =
		    worker_thread_group;
		pthread_mutex_init(&worker_thread_group->threads[i].
				   new_work_mutex, NULL);
		pthread_cond_init(&worker_thread_group->threads[i].
				  new_work_cond, NULL);
		pthread_mutex_init(&worker_thread_group->threads[i].
				   done_work_mutex, NULL);
		pthread_cond_init(&worker_thread_group->threads[i].
				  done_work_cond, NULL);
		qb_queue_init(&worker_thread_group->threads[i].queue, items_max,
			      item_size);

		worker_thread_group->threads[i].thread_data.thread_state =
		    worker_thread_group->threads[i].thread_state;
		worker_thread_group->threads[i].thread_data.data =
		    &worker_thread_group->threads[i];
		pthread_create(&worker_thread_group->threads[i].thread_id, NULL,
			       start_worker_thread,
			       &worker_thread_group->threads[i].thread_data);
	}
	return (0);
}

int32_t qb_wthread_group_work_add(struct qb_wthread_group *worker_thread_group,
			      void *item)
{
	int32_t schedule;

	schedule =
	    (worker_thread_group->last_scheduled +
	     1) % (worker_thread_group->threadcount);
	worker_thread_group->last_scheduled = schedule;

	pthread_mutex_lock(&worker_thread_group->threads[schedule].
			   new_work_mutex);
	if (qb_queue_is_full(&worker_thread_group->threads[schedule].queue)) {
		pthread_mutex_unlock(&worker_thread_group->threads[schedule].
				     new_work_mutex);
		return (-1);
	}
	qb_queue_item_add(&worker_thread_group->threads[schedule].queue, item);
	pthread_cond_signal(&worker_thread_group->threads[schedule].
			    new_work_cond);
	pthread_mutex_unlock(&worker_thread_group->threads[schedule].
			     new_work_mutex);
	return (0);
}

void qb_wthread_group_wait(struct qb_wthread_group *worker_thread_group)
{
	int32_t i;

	for (i = 0; i < worker_thread_group->threadcount; i++) {
		pthread_mutex_lock(&worker_thread_group->threads[i].
				   done_work_mutex);
		if (qb_queue_is_empty(&worker_thread_group->threads[i].queue) ==
		    0) {
			pthread_cond_wait(&worker_thread_group->threads[i].
					  done_work_cond,
					  &worker_thread_group->threads[i].
					  done_work_mutex);
		}
		pthread_mutex_unlock(&worker_thread_group->threads[i].
				     done_work_mutex);
	}
}

void qb_wthread_group_exit(struct qb_wthread_group *worker_thread_group)
{
	int32_t i;

	for (i = 0; i < worker_thread_group->threadcount; i++) {
		pthread_cancel(worker_thread_group->threads[i].thread_id);

		/* Wait for worker thread to exit gracefully before destroying
		 * mutexes and processing items in the queue etc.
		 */
		pthread_join(worker_thread_group->threads[i].thread_id, NULL);
		pthread_mutex_destroy(&worker_thread_group->threads[i].
				      new_work_mutex);
		pthread_cond_destroy(&worker_thread_group->threads[i].
				     new_work_cond);
		pthread_mutex_destroy(&worker_thread_group->threads[i].
				      done_work_mutex);
		pthread_cond_destroy(&worker_thread_group->threads[i].
				     done_work_cond);
	}
}

void qb_wthread_group_atsegv(struct qb_wthread_group *worker_thread_group)
{
	void *data_for_worker_fn;
	struct qb_wthread_t *worker_thread;
	uint32_t i;

	for (i = 0; i < worker_thread_group->threadcount; i++) {
		worker_thread = &worker_thread_group->threads[i];
		while (qb_queue_is_empty(&worker_thread->queue) == 0) {
			data_for_worker_fn =
			    qb_queue_item_get(&worker_thread->queue);
			worker_thread->worker_thread_group->
			    worker_fn(worker_thread->thread_state,
				      data_for_worker_fn);
			qb_queue_item_remove(&worker_thread->queue);
		}
	}
}
