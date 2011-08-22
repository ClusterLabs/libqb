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

typedef void (*qb_destroy_notifier_func)(void* data);
typedef int32_t (*qb_compare_func)(const void* a, const void* b, void* data);
typedef int32_t (*qb_transverse_func)(void* key, void* value, void* data);

typedef uint32_t (*qb_hash_func)(const void* key, uint32_t order);

uint32_t qb_hash_string(const void *key, uint32_t order);
uint32_t qb_hash_char(const void *key, uint32_t order);
uint32_t qb_hash_pointer(const void *key, uint32_t order);

/**
 * Create an unsorted map based on a hashtable.
 *
 * @param key_compare_func a user function to compare keys
 * @param key_compare_data a user pointer to be passed into the compare function
 * @param key_destroy_func function to free the key
 * @param value_destroy_func function to free the data
 * @param max_size maximum size of the hashtable
 * @param hash_fn hash function
 *
 * @see qb_hash_pointer, qb_hash_char, qb_hash_string
 *
 * @return the map instance
 */
qb_map_t* qb_hashtable_create(qb_compare_func key_compare_func,
			      void *key_compare_data,
			      qb_destroy_notifier_func key_destroy_func,
			      qb_destroy_notifier_func value_destroy_func,
			      size_t max_size,
			      qb_hash_func hash_fn);

/**
 * Create a sorted map using a skiplist.
 *
 * @param key_compare_func a user function to compare keys
 * @param key_compare_data a user pointer to be passed into the compare function
 * @param key_destroy_func function to free the key
 * @param value_destroy_func function to free the data
 *
 * @return the map instance
 */
qb_map_t* qb_skiplist_create(qb_compare_func key_compare_func,
			     void* key_compare_data,
                             qb_destroy_notifier_func key_destroy_func,
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
void qb_map_put(qb_map_t *map, const void* key, const void* value);

/**
 * Gets the value corresponding to the given key.
 */
void* qb_map_get(qb_map_t *map, const void* key);

/**
 * Removes a key/value pair from a map.
 *
 * The key and value are freed using the supplied destroy functions,
 * otherwise you have to make sure that any dynamically allocated
 * values are freed yourself. If the key does not exist in the map,
 * the function does nothing.
 */
int32_t qb_map_rm(qb_map_t *map, const void* key);

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
 * Destroy the map, removes all the items from the map.
 */
void qb_map_destroy(qb_map_t *map);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_MAP_H_DEFINED */
