/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
#include <semaphore.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbutil.h>
#include "log_int.h"

static int wthread_active = QB_FALSE;

static int wthread_should_exit = QB_FALSE;

static qb_thread_lock_t *logt_wthread_lock = NULL;

static QB_LIST_DECLARE(logt_print_finished_records);

static int logt_memory_used = 0;

static int logt_dropped_messages = 0;

static sem_t logt_thread_start;

static sem_t logt_print_finished;

static int logt_sched_param_queued = QB_FALSE;

static int logt_sched_policy;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM)
static struct sched_param logt_sched_param;
#endif /* HAVE_PTHREAD_SETSCHEDPARAM */

static pthread_t logt_thread_id = 0;

static void *qb_logt_worker_thread(void *data) __attribute__ ((noreturn));
static void *
qb_logt_worker_thread(void *data)
{
	struct qb_log_record *rec;
	int dropped = 0;
	int res;

	/* Signal qb_log_thread_start that the initialization may continue */
	sem_post(&logt_thread_start);
	for (;;) {
retry_sem_wait:
		res = sem_wait(&logt_print_finished);
		if (res == -1 && errno == EINTR) {
			goto retry_sem_wait;
		} else if (res == -1) {
			/* This case shouldn't happen */
			pthread_exit(NULL);
		}

		(void)qb_thread_lock(logt_wthread_lock);
		if (wthread_should_exit) {
			int value = -1;

			(void)sem_getvalue(&logt_print_finished, &value);
			if (value == 0) {
				(void)qb_thread_unlock(logt_wthread_lock);
				pthread_exit(NULL);
			}
		}

		rec =
		    qb_list_first_entry(&logt_print_finished_records,
				  struct qb_log_record, list);
		qb_list_del(&rec->list);
		logt_memory_used = logt_memory_used - strlen(rec->buffer) -
		    sizeof(struct qb_log_record) - 1;
		dropped = logt_dropped_messages;
		logt_dropped_messages = 0;
		if (dropped) {
			printf("%d messages lost\n", dropped);
		}

		qb_log_thread_log_write(rec->cs, &rec->timestamp, rec->buffer);

		(void)qb_thread_unlock(logt_wthread_lock);
		free(rec->buffer);
		free(rec);
	}
}

int32_t
qb_log_thread_priority_set(int32_t policy, int32_t priority)
{
	int res = 0;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM)

	logt_sched_policy = policy;

	if (policy == SCHED_OTHER
#ifdef SCHED_IDLE
	    || policy == SCHED_IDLE
#endif
#if defined(SCHED_BATCH) && !defined(QB_DARWIN)
	    || policy == SCHED_BATCH
#endif
	    ) {
		logt_sched_param.sched_priority = 0;
	} else {
		logt_sched_param.sched_priority = priority;
	}
	if (wthread_active == QB_FALSE) {
		logt_sched_param_queued = QB_TRUE;
	} else {
		res = pthread_setschedparam(logt_thread_id, policy,
					    &logt_sched_param);
		if (res != 0) {
			res = -res;
		}
	}
#endif
	return res;
}

int32_t
qb_log_thread_start(void)
{
	int res;
	qb_thread_lock_t *wthread_lock;

	if (wthread_active) {
		return 0;
	}

	wthread_active = QB_TRUE;
	sem_init(&logt_thread_start, 0, 0);
	sem_init(&logt_print_finished, 0, 0);
	errno = 0;
	logt_wthread_lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	if (logt_wthread_lock == NULL) {
		return errno ? -errno : -1;
	}
	res = pthread_create(&logt_thread_id, NULL,
			     qb_logt_worker_thread, NULL);
	if (res != 0) {
		wthread_active = QB_FALSE;
		(void)qb_thread_lock_destroy(logt_wthread_lock);
		return -res;
	}
	sem_wait(&logt_thread_start);

	if (logt_sched_param_queued) {
		res = qb_log_thread_priority_set(logt_sched_policy,
#if defined(HAVE_PTHREAD_SETSCHEDPARAM)
		                                 logt_sched_param.sched_priority);
#else
		                                 0);
#endif
		if (res != 0) {
			goto cleanup_pthread;
		}
		logt_sched_param_queued = QB_FALSE;
	}

	return 0;

cleanup_pthread:
	wthread_should_exit = QB_TRUE;
	sem_post(&logt_print_finished);
	pthread_join(logt_thread_id, NULL);

	wthread_active = QB_FALSE;
	wthread_lock = logt_wthread_lock;
	logt_wthread_lock = NULL;
	(void)qb_thread_lock_destroy(wthread_lock);

	sem_destroy(&logt_print_finished);
	sem_destroy(&logt_thread_start);

	return res;
}


void
qb_log_thread_pause(struct qb_log_target *t)
{
	if (t->threaded) {
		(void)qb_thread_lock(logt_wthread_lock);
	}
}

void
qb_log_thread_resume(struct qb_log_target *t)
{
	if (t->threaded) {
		(void)qb_thread_unlock(logt_wthread_lock);
	}
}

void
qb_log_thread_log_post(struct qb_log_callsite *cs,
		       struct timespec *timestamp, const char *buffer)
{
	struct qb_log_record *rec;
	size_t buf_size;
	size_t total_size;

	rec = malloc(sizeof(struct qb_log_record));
	if (rec == NULL) {
		return;
	}

	buf_size = strlen(buffer) + 1;
	total_size = sizeof(struct qb_log_record) + buf_size;

	rec->cs = cs;
	rec->buffer = malloc(buf_size);
	if (rec->buffer == NULL) {
		goto free_record;
	}
	memcpy(rec->buffer, buffer, buf_size);

	memcpy(&rec->timestamp, timestamp, sizeof(struct timespec));

	qb_list_init(&rec->list);
	(void)qb_thread_lock(logt_wthread_lock);
	logt_memory_used += total_size;
	if (logt_memory_used > 512000) {
		free(rec->buffer);
		free(rec);
		logt_memory_used = logt_memory_used - total_size;
		logt_dropped_messages += 1;
		(void)qb_thread_unlock(logt_wthread_lock);
		return;

	} else {
		qb_list_add_tail(&rec->list, &logt_print_finished_records);
	}
	(void)qb_thread_unlock(logt_wthread_lock);

	sem_post(&logt_print_finished);
	return;

free_record:
	free(rec);
}

void
qb_log_thread_stop(void)
{
	int res;
	int value;
	struct qb_log_record *rec;

	if (wthread_active == QB_FALSE && logt_wthread_lock == NULL) {
		return;
	}
	if (wthread_active == QB_FALSE) {
		for (;;) {
			res = sem_getvalue(&logt_print_finished, &value);
			if (res != 0 || value == 0) {
				break;
			}
			sem_wait(&logt_print_finished);

			(void)qb_thread_lock(logt_wthread_lock);

			rec = qb_list_first_entry(&logt_print_finished_records,
					    struct qb_log_record, list);
			qb_list_del(&rec->list);
			logt_memory_used = logt_memory_used -
					   strlen(rec->buffer) -
					   sizeof(struct qb_log_record) - 1;

			qb_log_thread_log_write(rec->cs, &rec->timestamp,
						rec->buffer);
			(void)qb_thread_unlock(logt_wthread_lock);
			free(rec->buffer);
			free(rec);
		}
	} else {
		(void)qb_thread_lock(logt_wthread_lock);
		wthread_should_exit = QB_TRUE;
		(void)qb_thread_unlock(logt_wthread_lock);
		sem_post(&logt_print_finished);
		pthread_join(logt_thread_id, NULL);
	}
	(void)qb_thread_lock_destroy(logt_wthread_lock);
	sem_destroy(&logt_print_finished);
	sem_destroy(&logt_thread_start);
}
