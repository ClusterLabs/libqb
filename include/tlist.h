/*
 * Copyright (c) 2006-2007, 2009-2021 Red Hat, Inc.
 *
 * Author: Jan Friesse <jfriesse@redhat.com>
 *         Steven Dake <sdake@redhat.com>
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

#ifndef QB_TLIST_H_DEFINED
#define QB_TLIST_H_DEFINED

#include "os_base.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblist.h>

#ifndef TIMER_HANDLE
typedef void *timer_handle;
#define TIMER_HANDLE
#endif

static int64_t timerlist_hertz;

struct timerlist {
	struct timerlist_timer **heap_entries;
	size_t allocated;
	size_t size;
	pthread_mutex_t list_mutex;
};

struct timerlist_timer {
	uint64_t expire_time;
	int32_t is_absolute_timer;
	void (*timer_fn) (void *data);
	void *data;
	timer_handle handle_addr;
	size_t heap_pos;
};

/*
 * Heap helper functions
 */
static inline size_t
timerlist_heap_index_left(size_t index)
{

	return (2 * index + 1);
}

static inline size_t
timerlist_heap_index_right(size_t index)
{

	return (2 * index + 2);
}

static inline size_t
timerlist_heap_index_parent(size_t index)
{

	return ((index - 1) / 2);
}

static inline void
timerlist_heap_entry_set(struct timerlist *timerlist, size_t item_pos, struct timerlist_timer *timer)
{

	assert(item_pos < timerlist->size);

	timerlist->heap_entries[item_pos] = timer;
	timerlist->heap_entries[item_pos]->heap_pos = item_pos;
}

static inline struct timerlist_timer *
timerlist_heap_entry_get(struct timerlist *timerlist, size_t item_pos)
{

	assert(item_pos < timerlist->size);

	return (timerlist->heap_entries[item_pos]);
}

static inline int
timerlist_entry_cmp(const struct timerlist_timer *t1, const struct timerlist_timer *t2)
{

	if (t1->expire_time == t2->expire_time) {
		return (0);
	} else if (t1->expire_time < t2->expire_time) {
		return (-1);
	} else {
		return (1);
	}
}

static inline void
timerlist_heap_sift_up(struct timerlist *timerlist, size_t item_pos)
{
	size_t parent_pos;
	struct timerlist_timer *parent_timer;
	struct timerlist_timer *timer;

	timer = timerlist_heap_entry_get(timerlist, item_pos);

	parent_pos = timerlist_heap_index_parent(item_pos);

	while (item_pos > 0 &&
	    (parent_timer = timerlist_heap_entry_get(timerlist, parent_pos),
	    timerlist_entry_cmp(parent_timer, timer) > 0)) {
		/*
		 * Swap item and parent
		 */
		timerlist_heap_entry_set(timerlist, parent_pos, timer);
		timerlist_heap_entry_set(timerlist, item_pos, parent_timer);

		item_pos = parent_pos;
		parent_pos = timerlist_heap_index_parent(item_pos);
	}
}

static inline void
timerlist_heap_sift_down(struct timerlist *timerlist, size_t item_pos)
{
	int cont;
	size_t left_pos, right_pos, smallest_pos;
	struct timerlist_timer *left_entry;
	struct timerlist_timer *right_entry;
	struct timerlist_timer *smallest_entry;
	struct timerlist_timer *tmp_entry;

	cont = 1;

	while (cont) {
		smallest_pos = item_pos;
		left_pos = timerlist_heap_index_left(item_pos);
		right_pos = timerlist_heap_index_right(item_pos);

		smallest_entry = timerlist_heap_entry_get(timerlist, smallest_pos);

		if (left_pos < timerlist->size &&
		    (left_entry = timerlist_heap_entry_get(timerlist, left_pos),
		    timerlist_entry_cmp(left_entry, smallest_entry) < 0)) {
			smallest_entry = left_entry;
			smallest_pos = left_pos;
		}

		if (right_pos < timerlist->size &&
		    (right_entry = timerlist_heap_entry_get(timerlist, right_pos),
		    timerlist_entry_cmp(right_entry, smallest_entry) < 0)) {
			smallest_entry = right_entry;
			smallest_pos = right_pos;
		}

		if (smallest_pos == item_pos) {
			/*
			 * Item is smallest (or has no children) -> heap property is restored
			 */
			cont = 0;
		} else {
			/*
			 * Swap item with smallest child
			 */
			tmp_entry = timerlist_heap_entry_get(timerlist, item_pos);
			timerlist_heap_entry_set(timerlist, item_pos, smallest_entry);
			timerlist_heap_entry_set(timerlist, smallest_pos, tmp_entry);

			item_pos = smallest_pos;
		}
	}
}

static inline void
timerlist_heap_delete(struct timerlist *timerlist, struct timerlist_timer *entry)
{
	size_t entry_pos;
	struct timerlist_timer *replacement_entry;
	int cmp_entries;

	entry_pos = entry->heap_pos;
	entry->heap_pos = (~(size_t)0);

	/*
	 * Swap element with last element
	 */
	replacement_entry = timerlist_heap_entry_get(timerlist, timerlist->size - 1);
	timerlist_heap_entry_set(timerlist, entry_pos, replacement_entry);

	/*
	 * And "remove" last element (= entry)
	 */
	timerlist->size--;

	/*
	 * Up (or down) heapify based on replacement item size
	 */
	cmp_entries = timerlist_entry_cmp(replacement_entry, entry);

	if (cmp_entries < 0) {
		timerlist_heap_sift_up(timerlist, entry_pos);
	} else if (cmp_entries > 0) {
		timerlist_heap_sift_down(timerlist, entry_pos);
	}
}

/*
 * Check if heap is valid.
 * - Shape property is always fullfiled because of storage in array
 * - Check heap property
 */
static inline int
timerlist_debug_is_valid_heap(struct timerlist *timerlist)
{
	size_t i;
	size_t left_pos, right_pos;
	struct timerlist_timer *left_entry;
	struct timerlist_timer *right_entry;
	struct timerlist_timer *cur_entry;

	for (i = 0; i < timerlist->size; i++) {
		cur_entry = timerlist_heap_entry_get(timerlist, i);

		left_pos = timerlist_heap_index_left(i);
		right_pos = timerlist_heap_index_right(i);

		if (left_pos < timerlist->size &&
		    (left_entry = timerlist_heap_entry_get(timerlist, left_pos),
		    timerlist_entry_cmp(left_entry, cur_entry) < 0)) {
			return (0);
		}

		if (right_pos < timerlist->size &&
		    (right_entry = timerlist_heap_entry_get(timerlist, right_pos),
		    timerlist_entry_cmp(right_entry, cur_entry) < 0)) {
			return (0);
		}
	}

	return (1);
}

/*
 * Main functions implementation
 */
static inline void timerlist_init(struct timerlist *timerlist)
{

	memset(timerlist, 0, sizeof(*timerlist));

	timerlist->heap_entries = NULL;
	pthread_mutex_init(&timerlist->list_mutex, NULL);
	timerlist_hertz = qb_util_nano_monotonic_hz();
}

static inline void timerlist_destroy(struct timerlist *timerlist)
{
	size_t zi;

	pthread_mutex_destroy(&timerlist->list_mutex);

	for (zi = 0; zi < timerlist->size; zi++) {
		free(timerlist->heap_entries[zi]);
	}
	free(timerlist->heap_entries);
}

static inline int32_t timerlist_add(struct timerlist *timerlist,
				 struct timerlist_timer *timer)
{
	size_t new_size;
	struct timerlist_timer **new_heap_entries;
	int32_t res = 0;

	if ( (res=pthread_mutex_lock(&timerlist->list_mutex))) {
		return -res;
	}

	/*
	 * Check that heap array is large enough
	 */
	if (timerlist->size + 1 > timerlist->allocated) {
		new_size = (timerlist->allocated + 1) * 2;

		new_heap_entries = realloc(timerlist->heap_entries,
		    new_size * sizeof(timerlist->heap_entries[0]));
		if (new_heap_entries == NULL) {
			res = -errno;

			goto cleanup;
		}

		timerlist->allocated = new_size;
		timerlist->heap_entries = new_heap_entries;
	}

	timerlist->size++;

	timerlist_heap_entry_set(timerlist, timerlist->size - 1, timer);
	timerlist_heap_sift_up(timerlist, timerlist->size - 1);

cleanup:
	pthread_mutex_unlock(&timerlist->list_mutex);
	return res;
}

static inline int32_t timerlist_add_duration(struct timerlist *timerlist,
					 void (*timer_fn) (void *data),
					 void *data,
					 uint64_t nano_duration,
					 timer_handle * handle)
{
	int res;
	struct timerlist_timer *timer;

	timer =
	    (struct timerlist_timer *)malloc(sizeof(struct timerlist_timer));

	if (timer == NULL) {
		return -ENOMEM;
	}

	timer->expire_time = qb_util_nano_current_get() + nano_duration;
	timer->is_absolute_timer = QB_FALSE;
	timer->data = data;
	timer->timer_fn = timer_fn;
	timer->handle_addr = handle;
	res = timerlist_add(timerlist, timer);
	if (res) {
		free(timer);
		return res;
	}

	*handle = timer;
	return (0);
}

static inline int32_t timerlist_del(struct timerlist *timerlist,
				 timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;
	int res;

	if ( (res=pthread_mutex_lock(&timerlist->list_mutex))) {
		return -res;
	}

	memset(timer->handle_addr, 0, sizeof(struct timerlist_timer *));

	timerlist_heap_delete(timerlist, timer);
	free(timer);

	pthread_mutex_unlock(&timerlist->list_mutex);
	return 0;
}

static inline uint64_t timerlist_expire_time(struct timerlist
						       *timerlist,
						       timer_handle
						       _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	return (timer->expire_time);
}

static inline void timerlist_pre_dispatch(struct timerlist *timerlist,
					  timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	memset(timer->handle_addr, 0, sizeof(struct timerlist_timer *));

	timerlist_heap_delete(timerlist, timer);
}

static inline void timerlist_post_dispatch(struct timerlist *timerlist,
					   timer_handle _timer_handle)
{
	struct timerlist_timer *timer = (struct timerlist_timer *)_timer_handle;

	free(timer);
}

/*
 * returns the number of msec until the next timer will expire for use with poll
 */
static inline uint64_t timerlist_msec_duration_to_expire(struct timerlist *timerlist)
{
	struct timerlist_timer *timer_from_list;
	volatile uint64_t current_time;
	volatile uint64_t msec_duration_to_expire;

	/*
	 * There is really no reasonable value to return when mutex lock fails
	 */
	if (pthread_mutex_lock(&timerlist->list_mutex)) {
		return (-1);
	}

	/*
	 * empty list, no expire
	 */
	if (timerlist->size == 0) {
		pthread_mutex_unlock(&timerlist->list_mutex);

		return (-1);
	}

	timer_from_list = timerlist_heap_entry_get(timerlist, 0);

	/*
	 * Mutex is no longer needed
	 */
	pthread_mutex_unlock(&timerlist->list_mutex);

	if (timer_from_list->is_absolute_timer) {
		current_time = qb_util_nano_from_epoch_get();
	} else {
		current_time = qb_util_nano_current_get();
	}

	/*
	 * timer at head of list is expired, zero msecs required
	 */
	if (timer_from_list->expire_time < current_time) {
		return (0);
	}

	msec_duration_to_expire =
	    ((timer_from_list->expire_time -
	      current_time) / QB_TIME_NS_IN_MSEC) + (1000 / timerlist_hertz);
	return (msec_duration_to_expire);
}

/*
 * Expires any timers that should be expired
 */
static inline int32_t timerlist_expire(struct timerlist *timerlist)
{
	struct timerlist_timer *timer;
	uint64_t current_time_from_epoch;
	uint64_t current_monotonic_time;
	uint64_t current_time;
	int res;

	current_monotonic_time = qb_util_nano_current_get();
	current_time_from_epoch = qb_util_nano_from_epoch_get();

	if ( (res=pthread_mutex_lock(&timerlist->list_mutex))) {
		return -res;
	}

	while (timerlist->size > 0) {
		timer = timerlist_heap_entry_get(timerlist, 0);

		current_time =
		    (timer->
		     is_absolute_timer ? current_time_from_epoch :
		     current_monotonic_time);

		if (timer->expire_time < current_time) {

			timerlist_pre_dispatch(timerlist, timer);

			timer->timer_fn(timer->data);

			timerlist_post_dispatch(timerlist, timer);
		} else {
			break;	/* for timer iteration */
		}
	}

	pthread_mutex_unlock(&timerlist->list_mutex);

	return (0);
}
#endif /* QB_TLIST_H_DEFINED */
