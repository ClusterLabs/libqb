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

void qb_hdb_create(struct qb_hdb *hdb)
{
	memset(hdb, 0, sizeof(struct qb_hdb));
	hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
}

void qb_hdb_destroy(struct qb_hdb
		    *hdb)
{
	free(hdb->handles);
	qb_thread_lock_destroy(hdb->lock);
	memset(hdb, 0, sizeof(struct qb_hdb));
}

int32_t qb_hdb_handle_create(struct qb_hdb *hdb, int32_t instance_size,
			     qb_handle_t * handle_id_out)
{
	int32_t handle;
	uint32_t check;
	void *new_handles;
	int32_t found = 0;
	void *instance;
	int32_t i;

	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(hdb->lock);

	for (handle = 0; handle < hdb->handle_count; handle++) {
		if (hdb->handles[handle].state == QB_HDB_HANDLE_STATE_EMPTY) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		hdb->handle_count += 1;
		new_handles = realloc(hdb->handles,
				      sizeof(struct qb_hdb_handle) *
				      hdb->handle_count);
		if (new_handles == NULL) {
			qb_thread_unlock(hdb->lock);
			errno = ENOMEM;
			return (-1);
		}
		hdb->handles = new_handles;
	}

	instance = malloc(instance_size);
	if (instance == 0) {
		errno = ENOMEM;
		return (-1);
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

	hdb->handles[handle].state = QB_HDB_HANDLE_STATE_ACTIVE;

	hdb->handles[handle].instance = instance;

	hdb->handles[handle].ref_count = 1;

	hdb->handles[handle].check = check;

	*handle_id_out = (((uint64_t) (check)) << 32) | handle;

	qb_thread_unlock(hdb->lock);

	return (0);
}

int32_t qb_hdb_handle_get(struct qb_hdb * hdb, qb_handle_t handle_in,
			  void **instance)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;

	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(hdb->lock);

	*instance = NULL;
	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	if (hdb->handles[handle].state != QB_HDB_HANDLE_STATE_ACTIVE) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff && check != hdb->handles[handle].check) {

		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	*instance = hdb->handles[handle].instance;

	hdb->handles[handle].ref_count += 1;

	qb_thread_unlock(hdb->lock);
	return (0);
}

int32_t qb_hdb_handle_get_always(struct qb_hdb * hdb, qb_handle_t handle_in,
				 void **instance)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;

	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(hdb->lock);

	*instance = NULL;
	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	if (hdb->handles[handle].state == QB_HDB_HANDLE_STATE_EMPTY) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff && check != hdb->handles[handle].check) {

		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	*instance = hdb->handles[handle].instance;

	hdb->handles[handle].ref_count += 1;

	qb_thread_unlock(hdb->lock);
	return (0);
}

int32_t qb_hdb_handle_put(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;

	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(hdb->lock);

	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);

		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff && check != hdb->handles[handle].check) {

		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	hdb->handles[handle].ref_count -= 1;
	assert(hdb->handles[handle].ref_count >= 0);

	if (hdb->handles[handle].ref_count == 0) {
		if (hdb->destructor) {
			hdb->destructor(hdb->handles[handle].instance);
		}
		free(hdb->handles[handle].instance);
		memset(&hdb->handles[handle], 0, sizeof(struct qb_hdb_handle));
	}
	qb_thread_unlock(hdb->lock);
	return (0);
}

int32_t qb_hdb_handle_destroy(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;
	int32_t res;

	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(hdb->lock);

	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);

		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff && check != hdb->handles[handle].check) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	hdb->handles[handle].state = QB_HDB_HANDLE_STATE_PENDINGREMOVAL;
	qb_thread_unlock(hdb->lock);
	res = qb_hdb_handle_put(hdb, handle_in);
	return (res);
}

int32_t qb_hdb_handle_refcount_get(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;

	int32_t refcount = 0;

	if (hdb->first_run == 1) {
		hdb->first_run = 0;
		hdb->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(hdb->lock);

	if (handle >= hdb->handle_count) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff && check != hdb->handles[handle].check) {
		qb_thread_unlock(hdb->lock);
		errno = EBADF;
		return (-1);
	}

	refcount = hdb->handles[handle].ref_count;

	qb_thread_unlock(hdb->lock);

	return (refcount);
}

void qb_hdb_iterator_reset(struct qb_hdb
			   *hdb)
{
	hdb->iterator = 0;
}

int32_t qb_hdb_iterator_next(struct qb_hdb *hdb, void **instance,
			     qb_handle_t * handle)
{
	int32_t res = -1;
	uint64_t checker;

	while (hdb->iterator < hdb->handle_count) {
		checker = (uint64_t) (hdb->handles[hdb->iterator].check);
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
