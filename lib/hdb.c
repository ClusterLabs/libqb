/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
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

#include <qb/qbutil.h>
#include <qb/qbhdb.h>

enum QB_HDB_HANDLE_STATE {
	QB_HDB_HANDLE_STATE_EMPTY,
	QB_HDB_HANDLE_STATE_PENDINGREMOVAL,
	QB_HDB_HANDLE_STATE_ACTIVE
};

static void qb_hdb_create_first_run(struct qb_hdb *hdb)
{
	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->handles = qb_array_create(32, sizeof(struct qb_hdb_handle));
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
}

void qb_hdb_create(struct qb_hdb *hdb)
{
	memset(hdb, 0, sizeof(struct qb_hdb));
	hdb->first_run = 1;
	qb_hdb_create_first_run(hdb);
}

void qb_hdb_destroy(struct qb_hdb
		    *hdb)
{
	qb_array_free(hdb->handles);
	qb_thread_lock_destroy(hdb->lock);
	memset(hdb, 0, sizeof(struct qb_hdb));
}

int32_t qb_hdb_handle_create(struct qb_hdb *hdb, int32_t instance_size,
			     qb_handle_t * handle_id_out)
{
	int32_t handle;
	int32_t res;
	uint32_t check;
	int32_t found = 0;
	void *instance;
	int32_t i;
	struct qb_hdb_handle *entry = NULL;

	qb_hdb_create_first_run(hdb);
	qb_thread_lock(hdb->lock);

	for (handle = 0; handle < hdb->handle_count; handle++) {
		if (qb_array_index(hdb->handles, handle, (void**)&entry) == 0 &&
		    entry->state == QB_HDB_HANDLE_STATE_EMPTY) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		hdb->handle_count++;
		res = qb_array_grow(hdb->handles, hdb->handle_count);
		if (res != 0) {
			goto unlock_exit;
		}
		res = qb_array_index(hdb->handles, hdb->handle_count - 1, (void**)&entry);
		if (res != 0) {
			goto unlock_exit;
		}
	}

	instance = malloc(instance_size);
	if (instance == 0) {
		res = -ENOMEM;
		goto unlock_exit;
	}

	/*
	 * This code makes sure the random number isn't zero
	 * We use 0 to specify an invalid handle out of the 1^64 address space
	 * If we get 0 200 times in a row, the RNG may be broken
	 */
	for (i = 0; i < 200; i++) {
		check = random();

		if (check != 0 && check != 0xffffffff) {
			break;
		}
	}

	memset(instance, 0, instance_size);

	entry->state = QB_HDB_HANDLE_STATE_ACTIVE;
	entry->instance = instance;
	entry->ref_count = 1;
	entry->check = check;

	*handle_id_out = (((uint64_t) (check)) << 32) | handle;

 unlock_exit:

	qb_thread_unlock(hdb->lock);

	return res;
}

int32_t qb_hdb_handle_get(struct qb_hdb * hdb, qb_handle_t handle_in,
			  void **instance)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;
	struct qb_hdb_handle *entry;

	qb_hdb_create_first_run(hdb);
	qb_thread_lock(hdb->lock);

	*instance = NULL;
	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void**)&entry) != 0 ||
	    entry->state != QB_HDB_HANDLE_STATE_ACTIVE) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	if (check != 0xffffffff && check != entry->check) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}
	entry->ref_count++;

	*instance = entry->instance;

	qb_thread_unlock(hdb->lock);

	return (0);
}

int32_t qb_hdb_handle_get_always(struct qb_hdb * hdb, qb_handle_t handle_in,
				 void **instance)
{
	return qb_hdb_handle_get(hdb, handle_in, instance);
}

int32_t qb_hdb_handle_put(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;
	struct qb_hdb_handle *entry;

	qb_hdb_create_first_run(hdb);
	qb_thread_lock(hdb->lock);

	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void**)&entry) != 0 ||
	    (check != 0xffffffff && check != entry->check)) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	entry->ref_count -= 1;
	assert(entry->ref_count >= 0);

	if (entry->ref_count == 0) {
		if (hdb->destructor) {
			hdb->destructor(entry->instance);
		}
		free(entry->instance);
		memset(entry, 0, sizeof(struct qb_hdb_handle));
	}
	qb_thread_unlock(hdb->lock);
	return (0);
}

int32_t qb_hdb_handle_destroy(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;
	int32_t res;
	struct qb_hdb_handle *entry;

	qb_hdb_create_first_run(hdb);
	qb_thread_lock(hdb->lock);

	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void**)&entry) != 0 ||
	    (check != 0xffffffff && check != entry->check)) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	entry->state = QB_HDB_HANDLE_STATE_PENDINGREMOVAL;
	qb_thread_unlock(hdb->lock);
	res = qb_hdb_handle_put(hdb, handle_in);
	return (res);
}

int32_t qb_hdb_handle_refcount_get(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;
	struct qb_hdb_handle *entry;

	int32_t refcount = 0;

	qb_hdb_create_first_run(hdb);
	qb_thread_lock(hdb->lock);

	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void**)&entry) != 0 ||
	    (check != 0xffffffff && check != entry->check)) {
		qb_thread_unlock(hdb->lock);
		return (-EBADF);
	}

	refcount = entry->ref_count;

	qb_thread_unlock(hdb->lock);

	return (refcount);
}

void qb_hdb_iterator_reset(struct qb_hdb *hdb)
{
	hdb->iterator = 0;
}

int32_t qb_hdb_iterator_next(struct qb_hdb *hdb, void **instance,
			     qb_handle_t * handle)
{
	int32_t res = -1;
	uint64_t checker;
	struct qb_hdb_handle *entry;

	while (hdb->iterator < hdb->handle_count) {
		res = qb_array_index(hdb->handles, hdb->iterator, (void**)&entry);
		if (res != 0) {
			break;
		}
		checker = (uint64_t) (entry->check);
		*handle = (checker << 32) | hdb->iterator;
		res = qb_hdb_handle_get(hdb, *handle, instance);

		hdb->iterator += 1;
		if (res == 0) {
			break;
		}
	}
	return (res);
}

uint32_t qb_hdb_base_convert(qb_handle_t handle)
{
	return (handle & 0xffffffff);
}

uint64_t qb_hdb_nocheck_convert(uint32_t handle)
{
	uint64_t retvalue = 0xffffffffULL << 32 | handle;

	return (retvalue);
}
