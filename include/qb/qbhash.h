/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Red Hat nor the names of its contributors may be used
 *   to endorse or promote products derived from this software without specific
 *   prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef QB_HASH_H_DEFINED
#define QB_HASH_H_DEFINED

#include <stdlib.h>
#include <qb/qbhdb.h>

extern int32_t qb_hash_initialize (
	qb_hdb_handle_t *handle,
	uint32_t order,
	uint32_t context_size);

extern int32_t qb_hash_key_set (
	qb_hdb_handle_t handle,
	const char *key,
	const void *value,
	uint32_t value_len);

extern int32_t qb_hash_key_get (
	qb_hdb_handle_t handle,
	const char *key,
	void **value,
	uint64_t *value_len);

extern int32_t qb_hash_key_context_get (
	qb_hdb_handle_t handle,
	const char *key,
	void **context);

extern int32_t qb_hash_key_delete (
	qb_hdb_handle_t handle,
	const char *key);

extern int32_t qb_hash_edge_create (
	qb_hdb_handle_t handle,
	const char *source_key,
	const char *dest_key,
	const char *edge_name);

extern int32_t qb_hash_edge_destroy (
	qb_hdb_handle_t handle,
	const char *source_key,
	const char *dest_key,
	const char *edge_name);

extern int32_t qb_hash_edge_follow (
	qb_hdb_handle_t handle,
	const char *source_key,
	const char *edge_name,
	char **dest_key);

extern int32_t qb_hash_edge_value_set (
	qb_hdb_handle_t handle,
	const char *source_key,
	const char *dest_key,
	const char *edge_name,
	const void *edge_value,
	uint64_t edge_value_len);

extern int32_t qb_hash_edge_value_get (
	qb_hdb_handle_t handle,
	const char *source_key,
	const char *dest_key,
	const char *edge_name,
	const void **edge_value,
	uint64_t *edge_value_len);

#endif /* QB_HASH_H_DEFINED */
