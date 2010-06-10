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

#ifndef QB_HASH_H_DEFINED
#define QB_HASH_H_DEFINED

#include <stdlib.h>
#include <qb/qbhdb.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

int32_t qb_hash_initialize(qb_hdb_handle_t * handle,
			   uint32_t order, uint32_t context_size);

int32_t qb_hash_key_set(qb_hdb_handle_t handle,
			const char *key, const void *value, uint32_t value_len);

int32_t qb_hash_key_get(qb_hdb_handle_t handle,
			const char *key, void **value, uint64_t * value_len);

int32_t qb_hash_key_context_get(qb_hdb_handle_t handle,
				const char *key, void **context);

int32_t qb_hash_key_delete(qb_hdb_handle_t handle, const char *key);

int32_t qb_hash_edge_create(qb_hdb_handle_t handle,
			    const char *source_key,
			    const char *dest_key, const char *edge_name);

int32_t qb_hash_edge_destroy(qb_hdb_handle_t handle,
			     const char *source_key,
			     const char *dest_key, const char *edge_name);

int32_t qb_hash_edge_follow(qb_hdb_handle_t handle,
			    const char *source_key,
			    const char *edge_name, char **dest_key);

int32_t qb_hash_edge_value_set(qb_hdb_handle_t handle,
			       const char *source_key,
			       const char *dest_key,
			       const char *edge_name,
			       const void *edge_value, uint64_t edge_value_len);

int32_t qb_hash_edge_value_get(qb_hdb_handle_t handle,
			       const char *source_key,
			       const char *dest_key,
			       const char *edge_name,
			       const void **edge_value,
			       uint64_t * edge_value_len);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_HASH_H_DEFINED */
