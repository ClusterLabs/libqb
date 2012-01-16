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
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @file qbmap.h
 * This provides a map interface to a Patricia trie, hashtable or skiplist.
 *
 * @par Ordering
 * The hashtable is NOT ordered, but ptrie and skiplist are.
 *
 * @par Iterating
 * Below is a simple example of how to iterate over a map.
 * @code
 * const char *p;
 * void *data;
 * qb_map_iter_t *it = qb_map_iter_create(m);
 * for (p = qb_map_iter_next(it, &data); p; p = qb_map_iter_next(it, &data)) {
 *     printf("%s > %s\n", p, (char*) data);
 * }
 * qb_map_iter_free(it);
 * @endcode
 *
 * Deletion of items within the iterator is supported. But note do not
 * free the item memory in the iterator. If you need to free the data
 * items then register for a notifier and free the memory there. This
 * is required as the items are reference counted.
 * @code
 * qb_map_notify_add(m, NULL, my_map_free_handler,
 *		     QB_MAP_NOTIFY_FREE, NULL);
 * @endcode
 *
 * @par Notifications
 * These allow you to get callbacks when values are inserted/removed or
 * replaced.
 * @note hashtable only supports deletion and replacement notificatins.
 * There is also a special global callback for freeing deleted and replaced
 * values (QB_MAP_NOTIFY_FREE).
 * @see qb_map_notify_add() qb_map_notify_del_2()
 *
 * @par Prefix matching
 * The ptrie supports prefixes in the iterator:
 *
 * @code
 * it = qb_map_pref_iter_create(m, "aa");
 * while ((p = qb_map_iter_next(it, &data)) != NULL) {
 *     printf("%s > %s\n", p, (char*)data);
 * }
 * qb_map_iter_free(it);
 * @endcode
 *
 * The ptrie also supports prefixes in notifications:
 * (remember to pass QB_MAP_NOTIFY_RECURSIVE into the notify_add.
 * @code
 * qb_map_notify_add(m, "root", my_map_notification,
 *		    (QB_MAP_NOTIFY_INSERTED|
 *		     QB_MAP_NOTIFY_DELETED|
 *		     QB_MAP_NOTIFY_REPLACED|
 *		     QB_MAP_NOTIFY_RECURSIVE),
 *		    NULL);
 *
 * @endcode
 */

/**
 * This is an opaque data type representing an instance of a map.
 */
typedef struct qb_map qb_map_t;

/**
 * This is an opaque data type representing an iterator instance.
 */
typedef struct qb_map_iter qb_map_iter_t;

#define	QB_MAP_NOTIFY_DELETED   1
#define	QB_MAP_NOTIFY_REPLACED  2
#define QB_MAP_NOTIFY_INSERTED  4
#define	QB_MAP_NOTIFY_RECURSIVE 8
#define	QB_MAP_NOTIFY_FREE      16

typedef void (*qb_map_notify_fn)(uint32_t event,
				 char* key,
				 void* old_value,
				 void* value,
				 void* user_data);

typedef int32_t (*qb_map_transverse_fn)(const char* key,
					void* value,
					void* user_data);

/**
 * Create an unsorted map based on a hashtable.
 *
 * @param max_size maximum size of the hashtable
 *
 * @return the map instance
 */
qb_map_t* qb_hashtable_create(size_t max_size);

/**
 * Create a sorted map using a skiplist.
 *
 * @return the map instance
 */
qb_map_t* qb_skiplist_create(void);

/**
 * Create a sorted map using a Patricia trie or "Radix tree".
 *
 * @htmlonly
 * See the wikipedia <a href="http://en.wikipedia.org/wiki/Radix_Tree">Radix_tree</a>
 * and <a href="http://en.wikipedia.org/wiki/Trie">Trie</a> pages.
 * @endhtmlonly
 */
qb_map_t* qb_trie_create(void);

/**
 * print out the nodes in the trie
 *
 * (for debug purposes)
 */
void
qb_trie_dump(qb_map_t* m);

/**
 * Add a notifier to the map.
 *
 * @param m the map instance
 * @param key the key (or prefix) to attach the notification to.
 * @param fn the callback
 * @param events the type of events to register for.
 * @param user_data a pointer to be passed into the callback
 *
 * @note QB_MAP_NOTIFY_INSERTED is only valid on tries.
 * @note you can use key prefixes with trie maps.
 *
 * @retval 0 success
 * @retval -errno failure
 */
int32_t qb_map_notify_add(qb_map_t* m, const char* key,
			  qb_map_notify_fn fn, int32_t events,
			  void *user_data);

/**
 * Delete a notifier from the map.
 *
 * @note the key,fn and events must match those you added.
 *
 * @param m the map instance
 * @param key the key (or prefix) to attach the notification to.
 * @param fn the callback
 * @param events the type of events to register for.
 *
 * @retval 0 success
 * @retval -errno failure
 */
int32_t qb_map_notify_del(qb_map_t* m, const char* key,
			  qb_map_notify_fn fn, int32_t events);

/**
 * Delete a notifier from the map (including the userdata).
 *
 * @note the key, fn, events and userdata must match those you added.
 *
 * @param m the map instance
 * @param key the key (or prefix) to attach the notification to.
 * @param fn the callback
 * @param events the type of events to register for.
 * @param user_data a pointer to be passed into the callback
 *
 * @retval 0 success
 * @retval -errno failure
 */
int32_t qb_map_notify_del_2(qb_map_t* m, const char* key,
			    qb_map_notify_fn fn, int32_t events,
			    void *user_data);

/**
 * Inserts a new key and value into a qb_map_t.
 *
 * If the key already exists in the qb_map_t, it gets replaced by the new key.
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
void qb_map_foreach(qb_map_t *map, qb_map_transverse_fn func, void* user_data);

/**
 * Create an iterator
 */
qb_map_iter_t* qb_map_iter_create(qb_map_t *map);

/**
 * Create a prefix iterator.
 *
 * This will iterate over all items with the given
 * prefix.
 * @note this is only supported by the trie.
 */
qb_map_iter_t* qb_map_pref_iter_create(qb_map_t *map, const char* prefix);

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
