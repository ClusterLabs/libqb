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

#ifndef QB_HDB_H_DEFINED
#define QB_HDB_H_DEFINED

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <qb/qbutil.h>

typedef uint64_t qb_hdb_handle_t;

/*
 * Formatting for string printing on 32/64 bit systems
 */
#define QB_HDB_D_FORMAT "%"PRIu64
#define QB_HDB_X_FORMAT "%"PRIx64

enum QB_HDB_HANDLE_STATE {
	QB_HDB_HANDLE_STATE_EMPTY,
	QB_HDB_HANDLE_STATE_PENDINGREMOVAL,
	QB_HDB_HANDLE_STATE_ACTIVE
};

struct qb_hdb_handle {
	int state;
	void *instance;
	int check;
	int ref_count;
};

struct qb_hdb_handle_database {
	unsigned int handle_count;
	struct qb_hdb_handle *handles;
	unsigned int iterator;
	void (*destructor) (void *);
	qb_thread_lock_t *lock;
	unsigned int first_run;
};

#define DECLARE_HDB_DATABASE(database_name,destructor_function)		\
static struct qb_hdb_handle_database (database_name) = {		\
	.handle_count	= 0,						\
	.handles 	= NULL,						\
	.iterator	= 0,						\
	.destructor	= destructor_function,				\
	.first_run	= 1						\
};									\

static inline void qb_hdb_create(struct qb_hdb_handle_database *handle_database)
{
	memset(handle_database, 0, sizeof(struct qb_hdb_handle_database));
	handle_database->lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
}

static inline void qb_hdb_destroy(struct qb_hdb_handle_database
				  *handle_database)
{
	free(handle_database->handles);
	qb_thread_lock_destroy(handle_database->lock);
	memset(handle_database, 0, sizeof(struct qb_hdb_handle_database));
}

static inline int qb_hdb_handle_create(struct qb_hdb_handle_database
				       *handle_database, int instance_size,
				       qb_hdb_handle_t * handle_id_out)
{
	int handle;
	unsigned int check;
	void *new_handles;
	int found = 0;
	void *instance;
	int i;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		handle_database->lock =
		    qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(handle_database->lock);

	for (handle = 0; handle < handle_database->handle_count; handle++) {
		if (handle_database->handles[handle].state ==
		    QB_HDB_HANDLE_STATE_EMPTY) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		handle_database->handle_count += 1;
		new_handles =
		    (struct qb_hdb_handle *)realloc(handle_database->handles,
						    sizeof(struct qb_hdb_handle)
						    *
						    handle_database->
						    handle_count);
		if (new_handles == NULL) {
			qb_thread_unlock(handle_database->lock);
			errno = ENOMEM;
			return (-1);
		}
		handle_database->handles = new_handles;
	}

	instance = (void *)malloc(instance_size);
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

	handle_database->handles[handle].state = QB_HDB_HANDLE_STATE_ACTIVE;

	handle_database->handles[handle].instance = instance;

	handle_database->handles[handle].ref_count = 1;

	handle_database->handles[handle].check = check;

	*handle_id_out = (((unsigned long long)(check)) << 32) | handle;

	qb_thread_unlock(handle_database->lock);

	return (0);
}

static inline int qb_hdb_handle_get(struct qb_hdb_handle_database
				    *handle_database, qb_hdb_handle_t handle_in,
				    void **instance)
{
	unsigned int check =
	    ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		handle_database->lock =
		    qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(handle_database->lock);

	*instance = NULL;
	if (handle >= handle_database->handle_count) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (handle_database->handles[handle].state !=
	    QB_HDB_HANDLE_STATE_ACTIVE) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
	    check != handle_database->handles[handle].check) {

		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	*instance = handle_database->handles[handle].instance;

	handle_database->handles[handle].ref_count += 1;

	qb_thread_unlock(handle_database->lock);
	return (0);
}

static inline int qb_hdb_handle_get_always(struct qb_hdb_handle_database
					   *handle_database,
					   qb_hdb_handle_t handle_in,
					   void **instance)
{
	unsigned int check =
	    ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		handle_database->lock =
		    qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(handle_database->lock);

	*instance = NULL;
	if (handle >= handle_database->handle_count) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (handle_database->handles[handle].state == QB_HDB_HANDLE_STATE_EMPTY) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
	    check != handle_database->handles[handle].check) {

		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	*instance = handle_database->handles[handle].instance;

	handle_database->handles[handle].ref_count += 1;

	qb_thread_unlock(handle_database->lock);
	return (0);
}

static inline int qb_hdb_handle_put(struct qb_hdb_handle_database
				    *handle_database, qb_hdb_handle_t handle_in)
{
	unsigned int check =
	    ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		handle_database->lock =
		    qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(handle_database->lock);

	if (handle >= handle_database->handle_count) {
		qb_thread_unlock(handle_database->lock);

		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
	    check != handle_database->handles[handle].check) {

		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	handle_database->handles[handle].ref_count -= 1;
	assert(handle_database->handles[handle].ref_count >= 0);

	if (handle_database->handles[handle].ref_count == 0) {
		if (handle_database->destructor) {
			handle_database->destructor(handle_database->
						    handles[handle].instance);
		}
		free(handle_database->handles[handle].instance);
		memset(&handle_database->handles[handle], 0,
		       sizeof(struct qb_hdb_handle));
	}
	qb_thread_unlock(handle_database->lock);
	return (0);
}

static inline int qb_hdb_handle_destroy(struct qb_hdb_handle_database
					*handle_database,
					qb_hdb_handle_t handle_in)
{
	unsigned int check =
	    ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;
	int res;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		handle_database->lock =
		    qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(handle_database->lock);

	if (handle >= handle_database->handle_count) {
		qb_thread_unlock(handle_database->lock);

		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
	    check != handle_database->handles[handle].check) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	handle_database->handles[handle].state =
	    QB_HDB_HANDLE_STATE_PENDINGREMOVAL;
	qb_thread_unlock(handle_database->lock);
	res = qb_hdb_handle_put(handle_database, handle_in);
	return (res);
}

static inline int qb_hdb_handle_refcount_get(struct qb_hdb_handle_database
					     *handle_database,
					     qb_hdb_handle_t handle_in)
{
	unsigned int check =
	    ((unsigned int)(((unsigned long long)handle_in) >> 32));
	unsigned int handle = handle_in & 0xffffffff;

	int refcount = 0;

	if (handle_database->first_run == 1) {
		handle_database->first_run = 0;
		handle_database->lock =
		    qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
	qb_thread_lock(handle_database->lock);

	if (handle >= handle_database->handle_count) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	if (check != 0xffffffff &&
	    check != handle_database->handles[handle].check) {
		qb_thread_unlock(handle_database->lock);
		errno = EBADF;
		return (-1);
	}

	refcount = handle_database->handles[handle].ref_count;

	qb_thread_unlock(handle_database->lock);

	return (refcount);
}

static inline void qb_hdb_iterator_reset(struct qb_hdb_handle_database
					 *handle_database)
{
	handle_database->iterator = 0;
}

static inline int qb_hdb_iterator_next(struct qb_hdb_handle_database
				       *handle_database, void **instance,
				       qb_hdb_handle_t * handle)
{
	int res = -1;

	while (handle_database->iterator < handle_database->handle_count) {
		*handle =
		    ((unsigned long
		      long)(handle_database->handles[handle_database->iterator].
			    check) << 32) | handle_database->iterator;
		res = qb_hdb_handle_get(handle_database, *handle, instance);

		handle_database->iterator += 1;
		if (res == 0) {
			break;
		}
	}
	return (res);
}

static inline unsigned int qb_hdb_base_convert(qb_hdb_handle_t handle)
{
	return (handle & 0xffffffff);
}

static inline unsigned long long qb_hdb_nocheck_convert(unsigned int handle)
{
	unsigned long long retvalue = 0xffffffffULL << 32 | handle;

	return (retvalue);
}

#endif /* QB_HDB_H_DEFINED */
