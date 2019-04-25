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

#include <qb/qbhdb.h>
#include <qb/qbatomic.h>

enum QB_HDB_HANDLE_STATE {
	QB_HDB_HANDLE_STATE_EMPTY,
	QB_HDB_HANDLE_STATE_PENDINGREMOVAL,
	QB_HDB_HANDLE_STATE_ACTIVE
};

static void
qb_hdb_create_first_run(struct qb_hdb *hdb)
{
	if (hdb->first_run == QB_TRUE) {
		hdb->first_run = QB_FALSE;
		qb_atomic_init();
		hdb->handles = qb_array_create(32, sizeof(struct qb_hdb_handle));
	}
}

void
qb_hdb_create(struct qb_hdb *hdb)
{
	memset(hdb, 0, sizeof(struct qb_hdb));
	hdb->first_run = QB_TRUE;
	qb_hdb_create_first_run(hdb);
}

void
qb_hdb_destroy(struct qb_hdb *hdb)
{
	qb_array_free(hdb->handles);
	memset(hdb, 0, sizeof(struct qb_hdb));
}

int32_t
qb_hdb_handle_create(struct qb_hdb *hdb, int32_t instance_size,
		     qb_handle_t * handle_id_out)
{
	int32_t handle;
	int32_t res = 0;
	int32_t check;
	int32_t found = QB_FALSE;
	void *instance;
	int32_t i;
	struct qb_hdb_handle *entry = NULL;
	int32_t handle_count;

	qb_hdb_create_first_run(hdb);

	handle_count = qb_atomic_int_get(&hdb->handle_count);
	for (handle = 0; handle < handle_count; handle++) {
		if (qb_array_index(hdb->handles, handle, (void**)&entry) == 0 &&
		    entry->state == QB_HDB_HANDLE_STATE_EMPTY) {
			found = QB_TRUE;
			qb_atomic_int_inc(&entry->ref_count);
			break;
		}
	}

	if (found == QB_FALSE) {
		res = qb_array_grow(hdb->handles, handle_count + 1U);
		if (res != 0) {
			return res;
		}
		res = qb_array_index(hdb->handles, handle_count,
				     (void **)&entry);
		if (res != 0) {
			return res;
		}
		/* NB: qb_array_grow above guarantees that handle_count
		       will not overflow INT32_MAX */
		qb_atomic_int_inc((int32_t *)&hdb->handle_count);
	}

	instance = malloc(instance_size);
	if (instance == 0) {
		return -ENOMEM;
	}

	/*
	 * Make sure just positive integers are used for the integrity(?)
	 * checks within 2^32 address space, if we miss 200 times in a row
	 * (just 0 is concerned per specification of random), the PRNG may be
	 * broken -> the value is unspecified, subject of stack allocation.
	 */
	for (i = 0; i < 200; i++) {
		check = random();

		if (check > 0) {
			break;  /* covers also check == UINT32_MAX */
		}
	}

	memset(instance, 0, instance_size);

	entry->state = QB_HDB_HANDLE_STATE_ACTIVE;
	entry->instance = instance;
	entry->ref_count = 1;
	entry->check = check;

	*handle_id_out = (((uint64_t) (check)) << 32) | handle;

	return res;
}

int32_t
qb_hdb_handle_get(struct qb_hdb * hdb, qb_handle_t handle_in, void **instance)
{
	int32_t check = handle_in >> 32;
	int32_t handle = handle_in & UINT32_MAX;
	struct qb_hdb_handle *entry;
	int32_t handle_count;

	qb_hdb_create_first_run(hdb);

	*instance = NULL;
	handle_count = qb_atomic_int_get(&hdb->handle_count);
	if (handle >= handle_count) {
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void **)&entry) != 0 ||
	    entry->state != QB_HDB_HANDLE_STATE_ACTIVE) {
		return (-EBADF);
	}

	if (check != (int32_t) UINT32_MAX && check != entry->check) {
		return (-EBADF);
	}
	qb_atomic_int_inc(&entry->ref_count);

	*instance = entry->instance;

	return (0);
}

int32_t
qb_hdb_handle_get_always(struct qb_hdb * hdb, qb_handle_t handle_in,
			 void **instance)
{
	return qb_hdb_handle_get(hdb, handle_in, instance);
}

int32_t
qb_hdb_handle_put(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	int32_t check = handle_in >> 32;
	int32_t handle = handle_in & UINT32_MAX;
	struct qb_hdb_handle *entry;
	int32_t handle_count;

	qb_hdb_create_first_run(hdb);

	handle_count = qb_atomic_int_get(&hdb->handle_count);
	if (handle >= handle_count) {
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void **)&entry) != 0 ||
	    (check != (int32_t) UINT32_MAX && check != entry->check)) {
		return (-EBADF);
	}

	if (qb_atomic_int_dec_and_test(&entry->ref_count)) {
		if (hdb->destructor) {
			hdb->destructor(entry->instance);
		}
		free(entry->instance);
		memset(entry, 0, sizeof(struct qb_hdb_handle));
	}
	return (0);
}

int32_t
qb_hdb_handle_destroy(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	int32_t check = handle_in >> 32;
	int32_t handle = handle_in & UINT32_MAX;
	int32_t res;
	struct qb_hdb_handle *entry;
	int32_t handle_count;

	qb_hdb_create_first_run(hdb);

	handle_count = qb_atomic_int_get(&hdb->handle_count);
	if (handle >= handle_count) {
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void **)&entry) != 0 ||
	    (check != (int32_t) UINT32_MAX && check != entry->check)) {
		return (-EBADF);
	}

	entry->state = QB_HDB_HANDLE_STATE_PENDINGREMOVAL;
	res = qb_hdb_handle_put(hdb, handle_in);
	return (res);
}

int32_t
qb_hdb_handle_refcount_get(struct qb_hdb * hdb, qb_handle_t handle_in)
{
	int32_t check = handle_in >> 32;
	int32_t handle = handle_in & UINT32_MAX;
	struct qb_hdb_handle *entry;
	int32_t handle_count;
	int32_t refcount = 0;

	qb_hdb_create_first_run(hdb);

	handle_count = qb_atomic_int_get(&hdb->handle_count);
	if (handle >= handle_count) {
		return (-EBADF);
	}

	if (qb_array_index(hdb->handles, handle, (void **)&entry) != 0 ||
	    (check != (int32_t) UINT32_MAX && check != entry->check)) {
		return (-EBADF);
	}

	refcount = qb_atomic_int_get(&entry->ref_count);

	return (refcount);
}

void
qb_hdb_iterator_reset(struct qb_hdb *hdb)
{
	hdb->iterator = 0;
}

int32_t
qb_hdb_iterator_next(struct qb_hdb *hdb, void **instance, qb_handle_t * handle)
{
	int32_t res = -1;
	uint64_t checker;
	struct qb_hdb_handle *entry;
	uint32_t handle_count;

	handle_count = qb_atomic_int_get(&hdb->handle_count);
	while (hdb->iterator < handle_count) {
		res = qb_array_index(hdb->handles,
				     hdb->iterator,
				     (void **)&entry);
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

uint32_t
qb_hdb_base_convert(qb_handle_t handle)
{
	return (handle & UINT32_MAX);
}

uint64_t
qb_hdb_nocheck_convert(uint32_t handle)
{
	uint64_t retvalue = ((uint64_t) UINT32_MAX) << 32 | handle;

	return (retvalue);
}
