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
#ifndef _QB_MAP_INT_H_
#define _QB_MAP_INT_H_

#include <qb/qblist.h>

struct qb_map;

typedef void (*qb_map_put_func)(struct qb_map *map, const char* key,
				const void* value);
typedef void* (*qb_map_get_func)(struct qb_map *map, const char* key);
typedef int32_t (*qb_map_rm_func)(struct qb_map *map, const char* key);
typedef size_t (*qb_map_count_get_func)(struct qb_map *map);
typedef void (*qb_map_destroy_func)(struct qb_map *map);
typedef qb_map_iter_t* (*qb_map_iter_create_func)(struct qb_map *map,
						  const char* prefix);
typedef const char* (*qb_map_iter_next_func)(qb_map_iter_t* i, void** value);
typedef void (*qb_map_iter_free_func)(qb_map_iter_t* i);

typedef int32_t (*qb_map_notify_add_func)(qb_map_t* m, const char* key,
					  qb_map_notify_fn fn, int32_t events,
					  void *user_data);

typedef int32_t (*qb_map_notify_del_func)(qb_map_t * m, const char *key,
					  qb_map_notify_fn fn,
					  int32_t events,
					  int32_t cmp_userdata,
					  void *user_data);

struct qb_map {
	qb_map_put_func put;
	qb_map_get_func get;
	qb_map_rm_func rm;
	qb_map_count_get_func count_get;
	qb_map_destroy_func destroy;
	qb_map_iter_create_func iter_create;
	qb_map_iter_next_func iter_next;
	qb_map_iter_free_func iter_free;
	qb_map_notify_add_func notify_add;
	qb_map_notify_del_func notify_del;
};

struct qb_map_iter {
	struct qb_map *m;
};

struct qb_map_notifier {
	struct qb_list_head list;
	qb_map_notify_fn callback;
	int32_t events;
	void *user_data;
	int32_t refcount;
};


#endif /* _QB_MAP_INT_H_ */
