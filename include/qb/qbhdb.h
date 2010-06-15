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

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <qb/qbutil.h>

typedef uint64_t qb_handle_t;

/*
 * Formatting for string printing on 32/64 bit systems
 */
#define QB_HDB_D_FORMAT "%"PRIu64
#define QB_HDB_X_FORMAT "%"PRIx64

struct qb_hdb_handle {
	int32_t state;
	void *instance;
	int32_t check;
	int32_t ref_count;
};

struct qb_hdb {
	uint32_t handle_count;
	struct qb_hdb_handle *handles;
	uint32_t iterator;
	void (*destructor) (void *);
	qb_thread_lock_t *lock;
	uint32_t first_run;
};

#define QB_HDB_DECLARE(database_name,destructor_function)		\
static struct qb_hdb (database_name) = {		\
	.handle_count	= 0,						\
	.handles	= NULL,						\
	.iterator	= 0,						\
	.destructor	= destructor_function,				\
	.first_run	= 1						\
};									\

void qb_hdb_create(struct qb_hdb *hdb);
void qb_hdb_destroy(struct qb_hdb *hdb);
int32_t qb_hdb_handle_create(struct qb_hdb *hdb, int32_t instance_size,
			     qb_handle_t * handle_id_out);
int32_t qb_hdb_handle_get(struct qb_hdb *hdb, qb_handle_t handle_in,
			  void **instance);
int32_t qb_hdb_handle_get_always(struct qb_hdb *hdb, qb_handle_t handle_in,
				 void **instance);
int32_t qb_hdb_handle_put(struct qb_hdb *hdb, qb_handle_t handle_in);
int32_t qb_hdb_handle_destroy(struct qb_hdb *hdb, qb_handle_t handle_in);
int32_t qb_hdb_handle_refcount_get(struct qb_hdb *hdb, qb_handle_t handle_in);
void qb_hdb_iterator_reset(struct qb_hdb *hdb);
int32_t qb_hdb_iterator_next(struct qb_hdb *hdb, void **instance,
			     qb_handle_t * handle);
uint32_t qb_hdb_base_convert(qb_handle_t handle);
uint64_t qb_hdb_nocheck_convert(uint32_t handle);

#endif /* QB_HDB_H_DEFINED */
