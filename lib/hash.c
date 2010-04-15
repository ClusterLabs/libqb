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
#include <stdint.h>
#include <qb/qbhdb.h>

#include <config.h>

#include "os_base.h"

#include <qb/qbhdb.h>
#include <qb/qblist.h>
#include <qb/qbhash.h>

#define FNV_32_PRIME ((uint32_t)0x01000193)

DECLARE_HDB_DATABASE(qb_hash_handle_db,NULL);

struct hash_node {
	struct qb_list_head list;
	void *value;
	uint32_t value_len;
	char key[0];
};
	
struct hash_bucket {
	pthread_mutex_t mutex;
	struct qb_list_head list_head;
};

struct hash_table {
	uint64_t memory_data;
	uint64_t memory_overhead;
	uint32_t order;
	uint32_t hash_buckets_len;
	struct hash_bucket hash_buckets[0];
};

static uint32_t hash_fnv (
	const void *value,
	uint32_t valuelen,
	uint32_t order)
{
	uint8_t *cd = (uint8_t *)value;
	uint8_t *ed = (uint8_t *)value + valuelen;
	uint32_t hash_result = 0x811c9dc5;
	int res;

	while (cd < ed) {
		hash_result ^= *cd;
		hash_result *= FNV_32_PRIME;
		cd++;
	}
	res = ((hash_result >> order) ^ hash_result) & ((1 << order) - 1);
	return (res);
}

int32_t qb_hash_initialize (
	qb_hdb_handle_t *handle,
	uint32_t order,
	uint32_t context_size)
{
	struct hash_table *hash_table;
	int i;
	uint64_t size;
	int32_t res;

	size = sizeof (struct hash_table) +
		(sizeof (struct hash_bucket) * (1 << order));

	res = qb_hdb_handle_create (&qb_hash_handle_db, size, handle);
	if (res != 0) {
		return (res);
	}
	res = qb_hdb_handle_get (&qb_hash_handle_db, *handle, (void *)&hash_table);
	if (res != 0) {
		goto hash_destroy;
	}

	hash_table->memory_data = 0;
	hash_table->memory_overhead = size;

	hash_table->order = order;
	hash_table->hash_buckets_len = 1 << order;
	for (i = 0; i < hash_table->hash_buckets_len; i++) {
		qb_list_init (&hash_table->hash_buckets[i].list_head);
		pthread_mutex_init (&hash_table->hash_buckets[i].mutex, NULL);
	}

	qb_hdb_handle_put (&qb_hash_handle_db, *handle);
	return (0);

hash_destroy:
	res = qb_hdb_handle_destroy (&qb_hash_handle_db,  *handle);
	return (-1);
}

int32_t qb_hash_key_set (
	qb_hdb_handle_t handle,
	const char *key,
	const void *value,
	uint32_t value_len)
{
	struct hash_table *hash_table;
	uint32_t hash_entry;
	struct qb_list_head *list;
	int found = 0;
	struct hash_node *hash_node;
	int res;

	res = qb_hdb_handle_get (&qb_hash_handle_db, handle, (void *)&hash_table);
	if (res != 0) { 
		return (res);
	}
	hash_entry = hash_fnv (key, strlen (key), hash_table->order);
	pthread_mutex_lock (&hash_table->hash_buckets[hash_entry].mutex);
	for (list = hash_table->hash_buckets[hash_entry].list_head.next;
		list != &hash_table->hash_buckets[hash_entry].list_head;
		list = list->next) {

		hash_node = qb_list_entry (list, struct hash_node, list);

		if (strcmp (hash_node->key, key) == 0) {
			hash_table->memory_data -= value_len;
			free (hash_node->value);
			found = 1;
			break;
		}
	}
	if (found == 0) {
		hash_node = malloc (sizeof (struct hash_node) + strlen (key) + 1);
		if (hash_node == 0) {
			goto error_exit;
		}

		hash_table->memory_overhead += sizeof (struct hash_node);
		hash_table->memory_data += strlen (key) + 1;

		memcpy (&hash_node->key, key, strlen (key) + 1);
		qb_list_add_tail (&hash_node->list,
			&hash_table->hash_buckets[hash_entry].list_head);
	}
	if (value_len) {
		hash_node->value = malloc (value_len);
		hash_table->memory_data += value_len;
	} else {
		hash_node->value = NULL;
	}
	hash_node->value_len = value_len;
	if (value) {
		memcpy (hash_node->value, value, value_len);
	}

error_exit:
	pthread_mutex_unlock (&hash_table->hash_buckets[hash_entry].mutex);
	qb_hdb_handle_put (&qb_hash_handle_db, handle);

	return (0);
}


int32_t qb_hash_key_get (
	qb_hdb_handle_t handle,
	const char *key,
	void **value,
	uint64_t *value_len)
{
	struct hash_table *hash_table;
	uint32_t hash_entry;
	uint32_t res = -1;
	struct qb_list_head *list;
	struct hash_node *hash_node;

	res = qb_hdb_handle_get (&qb_hash_handle_db, handle, (void *)&hash_table);
	if (res != 0) {
		return (res);
	}
	res = -1;

	hash_entry = hash_fnv (key, strlen (key), hash_table->order);

	pthread_mutex_lock (&hash_table->hash_buckets[hash_entry].mutex);
	for (list = hash_table->hash_buckets[hash_entry].list_head.next;
		list != &hash_table->hash_buckets[hash_entry].list_head;
		list = list->next) {

		hash_node = qb_list_entry (list, struct hash_node, list);
		if (strcmp (hash_node->key, key) == 0) {
			*value = hash_node->value;
			*value_len = hash_node->value_len;
			res = 0;
			goto unlock_exit;
		}
	}

unlock_exit:
	pthread_mutex_unlock (&hash_table->hash_buckets[hash_entry].mutex);
	qb_hdb_handle_put (&qb_hash_handle_db, handle);
	if (res == -1) {
		errno = ENOENT;
	}
	return (res);
}

int32_t qb_hash_key_delete (
	qb_hdb_handle_t handle,
	const char *key)
{
	struct hash_table *hash_table;
	struct qb_list_head *list;
	uint32_t hash_entry;
	uint32_t res = ENOENT;
	struct hash_node *hash_node;

	res = qb_hdb_handle_get (&qb_hash_handle_db, handle, (void *)&hash_table);
	if (res != 0) {
		return (res);
	}
	res = -1;

	hash_entry = hash_fnv (key, strlen (key), hash_table->order);
	pthread_mutex_lock (&hash_table->hash_buckets[hash_entry].mutex);
	for (list = hash_table->hash_buckets[hash_entry].list_head.next;
		list != &hash_table->hash_buckets[hash_entry].list_head;
		list = list->next) {

		hash_node = qb_list_entry (list, struct hash_node, list);
		if (strcmp (hash_node->key, key) == 0) {
			free (hash_node->value);
			qb_list_del (&hash_node->list);
			free (hash_node);
			res = 0;
			goto unlock_exit;
		}
	}

unlock_exit:
	pthread_mutex_unlock (&hash_table->hash_buckets[hash_entry].mutex);
	qb_hdb_handle_put (&qb_hash_handle_db, handle);
	if (res == -1)  {
		errno = ENOENT;
	}
	return (res);
}

int32_t qb_hash_key_context_get (
	qb_hdb_handle_t handle,
	const char *key,
	void **context)
{
	struct hash_table *hash_table;
	int res = 0;

	qb_hdb_handle_get (&qb_hash_handle_db, handle, (void *)&hash_table);
	qb_hdb_handle_put (&qb_hash_handle_db, handle);
	return (res);
}

