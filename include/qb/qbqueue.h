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
#ifndef QB_QUEUE_H_DEFINED
#define QB_QUEUE_H_DEFINED

#include <string.h>
#include <pthread.h>
#include "assert.h"

struct qb_queue {
	int head;
	int tail;
	int used;
	int usedhw;
	int size;
	void *items;
	int size_per_item;
	int iterator;
	pthread_mutex_t mutex;
};

static inline int qb_queue_init (struct qb_queue *qb_queue, int qb_queue_items, int size_per_item) {
	qb_queue->head = 0;
	qb_queue->tail = qb_queue_items - 1;
	qb_queue->used = 0;
	qb_queue->usedhw = 0;
	qb_queue->size = qb_queue_items;
	qb_queue->size_per_item = size_per_item;

	qb_queue->items = malloc (qb_queue_items * size_per_item);
	if (qb_queue->items == 0) {
		return (-ENOMEM);
	}
	memset (qb_queue->items, 0, qb_queue_items * size_per_item);
	pthread_mutex_init (&qb_queue->mutex, NULL);
	return (0);
}

static inline int qb_queue_reinit (struct qb_queue *qb_queue)
{
	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue->head = 0;
	qb_queue->tail = qb_queue->size - 1;
	qb_queue->used = 0;
	qb_queue->usedhw = 0;

	memset (qb_queue->items, 0, qb_queue->size * qb_queue->size_per_item);
	pthread_mutex_unlock (&qb_queue->mutex);
	return (0);
}

static inline void qb_queue_free (struct qb_queue *qb_queue) {
	pthread_mutex_destroy (&qb_queue->mutex);
	free (qb_queue->items);
}

static inline int qb_queue_is_full (struct qb_queue *qb_queue) {
	int full;

	pthread_mutex_lock (&qb_queue->mutex);
	full = ((qb_queue->size - 1) == qb_queue->used);
	pthread_mutex_unlock (&qb_queue->mutex);
	return (full);
}

static inline int qb_queue_is_empty (struct qb_queue *qb_queue) {
	int empty;

	pthread_mutex_lock (&qb_queue->mutex);
	empty = (qb_queue->used == 0);
	pthread_mutex_unlock (&qb_queue->mutex);
	return (empty);
}

static inline void qb_queue_item_add (struct qb_queue *qb_queue, void *item)
{
	char *qb_queue_item;
	int qb_queue_position;

	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue_position = qb_queue->head;
	qb_queue_item = qb_queue->items;
	qb_queue_item += qb_queue_position * qb_queue->size_per_item;
	memcpy (qb_queue_item, item, qb_queue->size_per_item);

	assert (qb_queue->tail != qb_queue->head);

	qb_queue->head = (qb_queue->head + 1) % qb_queue->size;
	qb_queue->used++;
	if (qb_queue->used > qb_queue->usedhw) {
		qb_queue->usedhw = qb_queue->used;
	}
	pthread_mutex_unlock (&qb_queue->mutex);
}

static inline void *qb_queue_item_get (struct qb_queue *qb_queue)
{
	char *qb_queue_item;
	int qb_queue_position;

	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue_position = (qb_queue->tail + 1) % qb_queue->size;
	qb_queue_item = qb_queue->items;
	qb_queue_item += qb_queue_position * qb_queue->size_per_item;
	pthread_mutex_unlock (&qb_queue->mutex);
	return ((void *)qb_queue_item);
}

static inline void qb_queue_item_remove (struct qb_queue *qb_queue) {
	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue->tail = (qb_queue->tail + 1) % qb_queue->size;

	assert (qb_queue->tail != qb_queue->head);

	qb_queue->used--;
	assert (qb_queue->used >= 0);
	pthread_mutex_unlock (&qb_queue->mutex);
}

static inline void qb_queue_items_remove (struct qb_queue *qb_queue, int rel_count)
{
	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue->tail = (qb_queue->tail + rel_count) % qb_queue->size;

	assert (qb_queue->tail != qb_queue->head);

	qb_queue->used -= rel_count;
	pthread_mutex_unlock (&qb_queue->mutex);
}


static inline void qb_queue_item_iterator_init (struct qb_queue *qb_queue)
{
	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue->iterator = (qb_queue->tail + 1) % qb_queue->size;
	pthread_mutex_unlock (&qb_queue->mutex);
}

static inline void *qb_queue_item_iterator_get (struct qb_queue *qb_queue)
{
	char *qb_queue_item;
	int qb_queue_position;

	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue_position = (qb_queue->iterator) % qb_queue->size;
	if (qb_queue->iterator == qb_queue->head) {
		pthread_mutex_unlock (&qb_queue->mutex);
		return (0);
	}
	qb_queue_item = qb_queue->items;
	qb_queue_item += qb_queue_position * qb_queue->size_per_item;
	pthread_mutex_unlock (&qb_queue->mutex);
	return ((void *)qb_queue_item);
}

static inline int qb_queue_item_iterator_next (struct qb_queue *qb_queue)
{
	int next_res;

	pthread_mutex_lock (&qb_queue->mutex);
	qb_queue->iterator = (qb_queue->iterator + 1) % qb_queue->size;

	next_res = qb_queue->iterator == qb_queue->head;
	pthread_mutex_unlock (&qb_queue->mutex);
	return (next_res);
}

static inline void qb_queue_avail (struct qb_queue *qb_queue, int *avail)
{
	pthread_mutex_lock (&qb_queue->mutex);
	*avail = qb_queue->size - qb_queue->used - 2;
	assert (*avail >= 0);
	pthread_mutex_unlock (&qb_queue->mutex);
}

static inline int qb_queue_used (struct qb_queue *qb_queue) {
	int used;

	pthread_mutex_lock (&qb_queue->mutex);
	used = qb_queue->used;
	pthread_mutex_unlock (&qb_queue->mutex);

	return (used);
}

static inline int qb_queue_usedhw (struct qb_queue *qb_queue) {
	int usedhw;

	pthread_mutex_lock (&qb_queue->mutex);
	usedhw = qb_queue->usedhw;
	pthread_mutex_unlock (&qb_queue->mutex);

	return (usedhw);
}

#endif /* QB_QUEUE_H_DEFINED */
