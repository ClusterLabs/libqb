/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
#ifndef QB_MAP_H_DEFINED
#define QB_MAP_H_DEFINED

#include <stdint.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @file qbmap.h
 * This provides a map interface to either a hashtable or a skiplist.
 */

/**
 * This is an opaque data type representing an instance of a map.
 */
typedef struct qb_map qb_map_t;
typedef struct qb_map_iter qb_map_iter_t;

typedef void (*qb_destroy_notifier_func)(void* data);
typedef int32_t (*qb_transverse_func)(const char* key, void* value, void* data);


/**
 * Create an unsorted map based on a hashtable.
 *
 * @param key_destroy_func function to free the key
 * @param value_destroy_func function to free the data
 * @param max_size maximum size of the hashtable
 *
 * @return the map instance
 */
qb_map_t* qb_hashtable_create(qb_destroy_notifier_func key_destroy_func,
			      qb_destroy_notifier_func value_destroy_func,
			      size_t max_size);

/**
 * Create a sorted map using a skiplist.
 *
 * @param key_destroy_func function to free the key
 * @param value_destroy_func function to free the data
 *
 * @return the map instance
 */
qb_map_t* qb_skiplist_create(qb_destroy_notifier_func key_destroy_func,
                             qb_destroy_notifier_func value_destroy_func);

/**
 *
 */
qb_map_t*
qb_trie_create(qb_destroy_notifier_func key_destroy_func,
	       qb_destroy_notifier_func value_destroy_func);


/**
 * Inserts a new key and value into a qb_map_t.
 *
 * If the key already exists in the qb_map_t, it gets replaced by the new key.
 * If you supplied a value_destroy_func when creating the qb_map_t,
 * the old value is freed using that function. If you supplied a
 * key_destroy_func when creating the qb_map_t, the old key is freed using
 * that function.
 */
void qb_map_put(qb_map_t *map, const char* key, const void* value);

/**
 * Gets the value corresponding to the given key.
 *
 * @retval NULL (if the key does not exist)
 * @retval a pointer to the value
 */
void* qb_map_get(qb_map_t *map, const char* key);

/**
 * Removes a key/value pair from a map.
 *
 * The key and value are freed using the supplied destroy functions,
 * otherwise you have to make sure that any dynamically allocated
 * values are freed yourself. If the key does not exist in the map,
 * the function does nothing.
 */
int32_t qb_map_rm(qb_map_t *map, const char* key);

/**
 * Get the number of items in the map.
 */
size_t qb_map_count_get(qb_map_t *map);

/**
 * Calls the given function for each of the key/value pairs in the map.
 *
 * The function is passed the key and value of each pair, and the given data
 * parameter. The map is traversed in sorted order.
 */
void qb_map_foreach(qb_map_t *map, qb_transverse_func func, void* user_data);

/**
 * Create an iterator
 */
qb_map_iter_t* qb_map_iter_create(qb_map_t *map);

/**
 * Get the next item
 *
 * @param i the iterator
 * @param value (out) the next item's value
 *
 * @retval the next key
 * @retval NULL - the end of the iteration
 */
const char* qb_map_iter_next(qb_map_iter_t* i, void** value);

/**
 * free the iterator
 *
 * @param i the iterator
 */
void qb_map_iter_free(qb_map_iter_t* i);

/**
 * Destroy the map, removes all the items from the map.
 */
void qb_map_destroy(qb_map_t *map);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_MAP_H_DEFINED */
