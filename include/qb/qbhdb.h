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

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <qb/qbarray.h>

/**
 * @file qbhdb.h
 * The handle database is for reference counting objects.
 *
 * @note
 * Historically, handle database implementation also served internal needs
 * of libqb (e.g. for IPC services tracking), which was eventually replaced
 * with indirection-less reference counters and their direct modifications.
 */

/**
 * Generic handle type is 64 bits.
 */
typedef uint64_t qb_handle_t;

/*
 * Formatting for string printing on 32/64 bit systems
 */
#define QB_HDB_D_FORMAT "%" PRIu64
#define QB_HDB_X_FORMAT "%" PRIx64

struct qb_hdb_handle {
	int32_t state;
	void *instance;
	int32_t check;
	int32_t ref_count;
};

struct qb_hdb {
	uint32_t handle_count;
	qb_array_t *handles;
	uint32_t iterator;
	void (*destructor) (void *);
	uint32_t first_run;
};

/**
 * Convience macro for declaring a file scoped handle database.
 * @code
 * QB_HDB_DECLARE(my_handle_database, NULL);
 * @endcode
 */
#define QB_HDB_DECLARE(database_name,destructor_function)		\
static struct qb_hdb (database_name) = {				\
	.handle_count	= 0,						\
	.handles	= NULL,						\
	.iterator	= 0,						\
	.destructor	= destructor_function,				\
	.first_run	= QB_TRUE					\
};									\

/**
 * Create a new database.
 * @param hdb the database to init.
 */
void qb_hdb_create(struct qb_hdb *hdb);

/**
 * Destroy a handle database.
 * @param hdb the database to destroy.
 */
void qb_hdb_destroy(struct qb_hdb *hdb);

/**
 * Create a new handle.
 * @param hdb the database instance
 * @param instance_size size of the object to malloc
 * @param handle_id_out new handle
 * @return (0 == ok, -errno failure)
 */
int32_t qb_hdb_handle_create(struct qb_hdb *hdb, int32_t instance_size,
			     qb_handle_t * handle_id_out);
/**
 * Get the instance associated with this handle and increase it's refcount.
 * @param handle_in the handle
 * @param hdb the database instance
 * @param instance (out) pointer to the desired object.
 * @return (0 == ok, -errno failure)
 */
int32_t qb_hdb_handle_get(struct qb_hdb *hdb, qb_handle_t handle_in,
			  void **instance);
/**
 * Get the instance associated with this handle and increase it's refcount.
 * @param handle_in the handle
 * @param hdb the database instance
 * @param instance (out) pointer to the desired object.
 * @return (0 == ok, -errno failure)
 * @note This is currently an alias to @ref qb_hdb_handle_get.
 */
int32_t qb_hdb_handle_get_always(struct qb_hdb *hdb, qb_handle_t handle_in,
				 void **instance);
/**
 * Put the instance associated with this handle and decrease it's refcount.
 * @param handle_in the handle
 * @param hdb the database instance
 * @return (0 == ok, -errno failure)
 */
int32_t qb_hdb_handle_put(struct qb_hdb *hdb, qb_handle_t handle_in);

/**
 * Request the destruction of the object.
 * 
 * When the refcount is 0, it will be destroyed.
 *
 * @param handle_in the handle
 * @param hdb the database instance
 * @return (0 == ok, -errno failure)
 */
int32_t qb_hdb_handle_destroy(struct qb_hdb *hdb, qb_handle_t handle_in);

/**
 * Get the current refcount.
 * @param handle_in the handle
 * @param hdb the database instance
 * @return (>= 0 is the refcount, -errno failure)
 */
int32_t qb_hdb_handle_refcount_get(struct qb_hdb *hdb, qb_handle_t handle_in);

/**
 * Reset the iterator.
 * @param hdb the database instance
 */
void qb_hdb_iterator_reset(struct qb_hdb *hdb);

/**
 * Get the next object and increament it's refcount.
 *
 * Remember to call qb_hdb_handle_put()
 *
 * @param hdb the database instance
 * @param handle (out) the handle
 * @param instance (out) pointer to the desired object.
 * @return (0 == ok, -errno failure)
 */
int32_t qb_hdb_iterator_next(struct qb_hdb *hdb, void **instance,
			     qb_handle_t * handle);

uint32_t qb_hdb_base_convert(qb_handle_t handle);
uint64_t qb_hdb_nocheck_convert(uint32_t handle);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
#endif /* QB_HDB_H_DEFINED */
