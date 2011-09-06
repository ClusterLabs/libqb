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

struct qb_map;

typedef void (*qb_map_put_func)(struct qb_map *map, const char* key, const void* value);
typedef void* (*qb_map_get_func)(struct qb_map *map, const char* key);
typedef int32_t (*qb_map_rm_func)(struct qb_map *map, const char* key);
typedef size_t (*qb_map_count_get_func)(struct qb_map *map);
typedef void (*qb_map_foreach_func)(struct qb_map *map, qb_transverse_func func, void* user_data);
typedef void (*qb_map_destroy_func)(struct qb_map *map);

struct qb_map {
	/* user provided
	 */
        qb_destroy_notifier_func key_destroy_func;
        qb_destroy_notifier_func value_destroy_func;

	/* data type provided
	 */
	qb_map_put_func put;
	qb_map_get_func get;
	qb_map_rm_func rm;
	qb_map_count_get_func count_get;
	qb_map_foreach_func foreach;
	qb_map_destroy_func destroy;
};

#endif /* _QB_MAP_INT_H_ */
