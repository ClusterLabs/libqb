/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse <jfriesse@redhat.com>
 *         Angus Salkeld <asalkeld@redhat.com>
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
#include "os_base.h"

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbmap.h>

static void
notify_fn(uint32_t event, char* key, void* old_value, void* value, void* user_data)
{
	if (event == QB_MAP_NOTIFY_FREE) {
		fprintf(stderr, "Notify[FREE] %s [%d]\n",
			key, *(int *)old_value);
		free(old_value);
	} else if (event == QB_MAP_NOTIFY_DELETED) {
		fprintf(stderr, "Notify[DELETED] %s [%d]\n",
			key, *(int *)old_value);
	} else if (event == QB_MAP_NOTIFY_REPLACED) {
		fprintf(stderr, "Notify[REPLACED] %s [%d] -> [%d]\n",
			key, *(int *)old_value, *(int *)value);
	} else {
		fprintf(stderr, "Notify[%d] %s \n", event, key);
		if (value != NULL) {
			fprintf(stderr, " value = [%d]\n", *(int *)value);
		}
		if (old_value != NULL) {
			fprintf(stderr, " old value = [%d]\n", *(int *)old_value);
		}
	}
}

int
main(void)
{
	qb_map_t *trie;
	int *i1, *i2, *i3;
	qb_map_iter_t *iter;
	const char *key;
	void *val;
	uint32_t revents = (QB_MAP_NOTIFY_DELETED |
			    QB_MAP_NOTIFY_REPLACED |
			    QB_MAP_NOTIFY_INSERTED |
			    QB_MAP_NOTIFY_RECURSIVE);

	trie = qb_trie_create();
	assert(trie != NULL);

	i1 = malloc(sizeof(int));
	assert(i1 != NULL);
	*i1 = 1;

	i2 = malloc(sizeof(int));
	assert(i2 != NULL);
	*i2 = 2;

	i3 = malloc(sizeof(int));
	assert(i3 != NULL);
	*i3 = 3;

	qb_map_notify_add(trie, NULL, notify_fn, QB_MAP_NOTIFY_FREE, NULL);

	qb_map_put(trie, "test.key1", i1);
	qb_map_put(trie, "test.key2", i2);

	qb_map_notify_add(trie, "test.", notify_fn, revents, NULL);

	qb_map_put(trie, "test.key1", i3);

	iter = qb_map_pref_iter_create(trie, "test.");
	while ((key = qb_map_iter_next(iter, &val)) != NULL) {
		fprintf(stderr,"Iter %s [%d]\n", key, *(int*)val);
		qb_map_rm(trie, key);
	}
	qb_map_iter_free(iter);
	qb_map_notify_del_2(trie, "test.", notify_fn, revents, NULL);
	qb_map_destroy(trie);

	return (0);
}
