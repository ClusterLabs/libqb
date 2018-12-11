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
#include <os_base.h>
#include <assert.h>

#include <qb/qbdefs.h>
#include <qb/qbmap.h>
#include "map_int.h"

#define SKIPLIST_LEVEL_MAX 8
#define SKIPLIST_LEVEL_MIN 0
/* The amount of possible levels */
#define SKIPLIST_LEVEL_COUNT (SKIPLIST_LEVEL_MAX - SKIPLIST_LEVEL_MIN + 1)

struct skiplist_iter {
	struct qb_map_iter i;
	struct skiplist_node *n;
};

struct skiplist_node {
	const char *key;
	void *value;
	/* special meaning when lower than SKIPLIST_LEVEL_MIN:
	   indication that @skiplist_node_destroy() needs to skip
	   disposing node->forward (unless it is the termination
	   of the whole list) */
	int8_t level;
	uint32_t refcount;
	struct qb_list_head notifier_head;

	/* An array of @level + 1 node pointers */
	struct skiplist_node **forward;
};

struct skiplist {
	struct qb_map map;

	size_t length;
	/* special meaning when lower than SKIPLIST_LEVEL_MIN:
	   indication that @skiplist_node_destroy() is the terminating
	   one (triggered with @skiplist_destroy()), therefore node->forward
	   needs to be free'd unconditionally */
	int8_t level;
	struct skiplist_node *header;
};

/* An array of nodes that need to be updated after an insert or delete operation
 */
typedef struct skiplist_node *skiplist_update_t[SKIPLIST_LEVEL_COUNT];

static int8_t
skiplist_level_generate(void)
{
	/* This constant is found by 1 / P, where P = 0.25. */
	enum { P_INVERSE = 4 };

	/* The original algorithm's random number is in the range [0, 1), so
	 * max M = 1. Its ceiling C = M * P = 1 * P = P.
	 *
	 * Our random number is in the range [0, UINT16_MAX], so M = UINT16_MAX. Therefore,
	 * C = UINT16_MAX * P = UINT16_MAX / P_INVERSE.
	 */
	enum { P_CEIL = UINT16_MAX / P_INVERSE };
	int8_t level = SKIPLIST_LEVEL_MIN;

	while ((uint16_t) (random()) < P_CEIL)
		level++;

	if (level < SKIPLIST_LEVEL_MAX)
		return level;

	return SKIPLIST_LEVEL_MAX;
}

static struct skiplist_node *
skiplist_node_next(const struct skiplist_node *node)
{
	const struct skiplist_node *n = node;
	do {
		n = n->forward[SKIPLIST_LEVEL_MIN];
	} while (n && n->refcount == 0);
	return (struct skiplist_node *)n;
}

/*
 * Create a new level @level node with value @value. The node should eventually
 * be destroyed with @skiplist_node_destroy.
 *
 * return: a new node on success and NULL otherwise.
 */
static struct skiplist_node *
skiplist_node_new(const int8_t level, const char *key, const void *value)
{
	struct skiplist_node *new_node = (struct skiplist_node *)
	    (malloc(sizeof(struct skiplist_node)));

	if (!new_node)
		return NULL;

	new_node->value = (void *)value;
	new_node->key = key;
	new_node->level = level;
	new_node->refcount = 1;
	qb_list_init(&new_node->notifier_head);

	/* A level 0 node still needs to hold 1 forward pointer, etc.;
	   instead of "level + 1", we need to capture whole possible
	   width because of forward-member-resilience-upon-entry-removal
	   arrangment based on takeover-and-repoint. */
	new_node->forward = (struct skiplist_node **)
	    (calloc(SKIPLIST_LEVEL_MAX + 1, sizeof(struct skiplist_node *)));

	if (new_node->forward == NULL) {
		free(new_node);
		return NULL;
	}

	return new_node;
}

static struct skiplist_node *
skiplist_header_node_new(void)
{
	return skiplist_node_new(SKIPLIST_LEVEL_MAX, NULL, NULL);
}

/* An operation to perform after comparing a user value or search with a
 * node's value
 */
typedef enum {
	OP_GOTO_NEXT_LEVEL,
	OP_GOTO_NEXT_NODE,
	OP_FINISH,
} op_t;

static op_t
op_search(const struct skiplist *list,
	  const struct skiplist_node *fwd_node, const void *search)
{
	int32_t cmp;

	if (!fwd_node)
		return OP_GOTO_NEXT_LEVEL;

	cmp = strcmp(fwd_node->key, search);
	if (cmp < 0) {
		return OP_GOTO_NEXT_NODE;
	} else if (cmp == 0) {
		return OP_FINISH;
	}

	return OP_GOTO_NEXT_LEVEL;
}

static struct skiplist_node *
skiplist_lookup(struct skiplist *list, const char *key)
{
	struct skiplist_node *cur_node = list->header;
	int8_t level = list->level;

	while (level >= SKIPLIST_LEVEL_MIN) {
		struct skiplist_node *fwd_node = cur_node->forward[level];

		switch (op_search(list, fwd_node, key)) {
		case OP_FINISH:
			return fwd_node;
		case OP_GOTO_NEXT_NODE:
			cur_node = fwd_node;
			break;
		case OP_GOTO_NEXT_LEVEL:
			level--;
		}
	}

	return NULL;
}

static void
skiplist_notify(struct skiplist *l, struct skiplist_node *n,
		uint32_t event, char *key, void *old_value, void *value)
{
	struct qb_list_head *list;
	struct qb_map_notifier *tn;

	/* node callbacks
	 */
	qb_list_for_each(list, &n->notifier_head) {
		tn = qb_list_entry(list, struct qb_map_notifier, list);

		if (tn->events & event) {
			tn->callback(event, key, old_value, value,
				     tn->user_data);
		}
	}
	/* global callbacks
	 */
	qb_list_for_each(list, &l->header->notifier_head) {
		tn = qb_list_entry(list, struct qb_map_notifier, list);

		if (tn->events & event) {
			tn->callback(event, key, old_value, value,
				     tn->user_data);
		}
		if (((event & QB_MAP_NOTIFY_DELETED) ||
		     (event & QB_MAP_NOTIFY_REPLACED)) &&
		    (tn->events & QB_MAP_NOTIFY_FREE)) {
			tn->callback(QB_MAP_NOTIFY_FREE, (char *)key,
				     old_value, value, tn->user_data);
		}
	}

}

static void
skiplist_node_destroy(struct skiplist_node *node, struct skiplist *list)
{
	struct qb_list_head *pos;
	struct qb_list_head *next;
	struct qb_map_notifier *tn;

	skiplist_notify(list, node,
			QB_MAP_NOTIFY_DELETED,
			(char *)node->key, node->value, NULL);

	qb_list_for_each_safe(pos, next, &node->notifier_head) {
		tn = qb_list_entry(pos, struct qb_map_notifier, list);
		qb_list_del(&tn->list);
		free(tn);
	}

	if (node->level >= SKIPLIST_LEVEL_MIN
	    || list->level < SKIPLIST_LEVEL_MIN) {
		free(node->forward);
	}
	free(node);
}

static void
skiplist_node_deref(struct skiplist_node *node, struct skiplist *list)
{
	node->refcount--;
	if (node->refcount == 0) {
		skiplist_node_destroy(node, list);
	}
}

static int32_t
skiplist_notify_add(qb_map_t * m, const char *key,
		    qb_map_notify_fn fn, int32_t events, void *user_data)
{
	struct skiplist *t = (struct skiplist *)m;
	struct qb_map_notifier *f;
	struct skiplist_node *n;
	struct qb_list_head *list;
	int add_to_tail = QB_FALSE;

	if (key) {
		n = skiplist_lookup(t, key);
	} else {
		n = t->header;
	}
	if (events & QB_MAP_NOTIFY_FREE) {
		add_to_tail = QB_TRUE;
	}
	if (n) {
		qb_list_for_each(list, &n->notifier_head) {
			f = qb_list_entry(list, struct qb_map_notifier, list);

			if (events & QB_MAP_NOTIFY_FREE &&
			    f->events == events) {
				/* only one free notifier */
				return -EEXIST;
			}
			if (f->events == events &&
			    f->callback == fn &&
			    f->user_data == user_data) {
				return -EEXIST;
			}
		}

		f = malloc(sizeof(struct qb_map_notifier));
		if (f == NULL) {
			return -errno;
		}
		f->events = events;
		f->user_data = user_data;
		f->callback = fn;
		qb_list_init(&f->list);
		if (add_to_tail) {
			qb_list_add_tail(&f->list, &n->notifier_head);
		} else {
			qb_list_add(&f->list, &n->notifier_head);
		}
		return 0;
	}
	return -EINVAL;
}

static int32_t
skiplist_notify_del(qb_map_t * m, const char *key,
		    qb_map_notify_fn fn, int32_t events,
		    int32_t cmp_userdata, void *user_data)
{
	struct skiplist *t = (struct skiplist *)m;
	struct skiplist_node *n;
	struct qb_map_notifier *f;
	struct qb_list_head *head = NULL;
	struct qb_list_head *list;
	struct qb_list_head *next;
	int32_t found = QB_FALSE;

	if (key) {
		n = skiplist_lookup(t, key);
		if (n) {
			head = &n->notifier_head;
		}
	} else {
		head = &t->header->notifier_head;
	}
	if (head == NULL) {
		return -ENOENT;
	}
	qb_list_for_each_safe(list, next, head) {
		f = qb_list_entry(list, struct qb_map_notifier, list);

		if (f->events == events && f->callback == fn) {
			if (cmp_userdata && (f->user_data == user_data)) {
				found = QB_TRUE;
				qb_list_del(&f->list);
				free(f);
			} else if (!cmp_userdata) {
				found = QB_TRUE;
				qb_list_del(&f->list);
				free(f);
			}
		}
	}
	if (found) {
		return 0;
	} else {
		return -ENOENT;
	}
}

static void
skiplist_destroy(struct qb_map *map)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *cur_node;
	struct skiplist_node *fwd_node;

	list->level = SKIPLIST_LEVEL_MIN - 1;  /* indicate teardown */
	for (cur_node = skiplist_node_next(list->header);
	     cur_node; cur_node = fwd_node) {
		fwd_node = skiplist_node_next(cur_node);
		skiplist_node_destroy(cur_node, list);
	}
	skiplist_node_destroy(list->header, list);
	free(list);
}

static void
skiplist_put(struct qb_map *map, const char *key, const void *value)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *new_node;
	int8_t level = list->level;
	skiplist_update_t update;
	int8_t update_level;
	int8_t new_node_level;
	struct skiplist_node *cur_node = list->header;
	char *old_k;
	char *old_v;

	while ((update_level = level) >= SKIPLIST_LEVEL_MIN) {
		struct skiplist_node *fwd_node = cur_node->forward[level];

		switch (op_search(list, fwd_node, key)) {
		case OP_FINISH:
			old_k = (char *)fwd_node->key;
			old_v = (char *)fwd_node->value;
			fwd_node->value = (void *)value;
			fwd_node->key = (void *)key;
			skiplist_notify(list, fwd_node,
					QB_MAP_NOTIFY_REPLACED,
					old_k, old_v, fwd_node->value);
			return;

		case OP_GOTO_NEXT_NODE:
			cur_node = fwd_node;
			break;
		case OP_GOTO_NEXT_LEVEL:
			level--;
		}

		update[update_level] = cur_node;
	}

	new_node_level = skiplist_level_generate();

	if (new_node_level > list->level) {
		for (level = list->level + 1; level <= new_node_level; level++)
			update[level] = list->header;

		list->level = new_node_level;
	}

	new_node = skiplist_node_new(new_node_level, key, value);

	assert(new_node != NULL);
	skiplist_notify(list, new_node,
			QB_MAP_NOTIFY_INSERTED,
			(char*)new_node->key, NULL, new_node->value);

	/* Drop @new_node into @list. */
	for (level = SKIPLIST_LEVEL_MIN; level <= new_node_level; level++) {
		new_node->forward[level] = update[level]->forward[level];
		update[level]->forward[level] = new_node;
	}

	list->length++;
}

static int32_t
skiplist_rm(struct qb_map *map, const char *key)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *found_node;
	struct skiplist_node *cur_node = list->header;
	int8_t level = list->level;
	int8_t update_level;
	skiplist_update_t update;

	while ((update_level = level) >= SKIPLIST_LEVEL_MIN) {
		struct skiplist_node *fwd_node = cur_node->forward[level];

		switch (op_search(list, fwd_node, key)) {
		case OP_GOTO_NEXT_NODE:
			cur_node = fwd_node;
			break;
		case OP_GOTO_NEXT_LEVEL:
		default:
			level--;
			break;
		}

		update[update_level] = cur_node;
	}

	/* The immediate forward node should be the matching node... */
	found_node = skiplist_node_next(cur_node);

	/* ...unless we're at the end of the list or the value doesn't exist. */
	if (!found_node || strcmp(found_node->key, key) != 0) {
		return QB_FALSE;
	}

	/* Splice found_node out of list. */
	for (level = SKIPLIST_LEVEL_MIN; level <= list->level; level++) {
		if (update[level]->forward[level] == found_node) {
			update[level]->forward[level] =
			    found_node->forward[level];
		}
	}

	/* If @found_node is referenced more than once, it means that it is
	   currently positioned with one or more iterators, therefore it's
	   likely that @qb_map_iter_next() will be called at least once so
	   as to progress pass this @found_node.  The problem is, original
	   @found_node->forward may have become referencing successfully
	   destroyed original successor by the time the progress of such
	   iterator(s) resumes, possibly causing use-after-free in
	   @skiplist_node_next().

	   To solve this, we will grab @cur_node->forward, which has just
	   been updated accordingly in the above statement, copying it
	   to @found_node->forward, and repointing @cur_node->forward to
	   point to @found_node's copy (freeing its original list first).
	   To prevent freeing the pointed memory behind @cur_node's
	   back from the @found_node's context, we use the fact that the
	   iterator can only advance to the next node, without re-examination
	   of the current one, hence we can afford to abuse @found_node->value
	   as a flag field when set to our private "special" value that under
	   no normal circumstance can appear (for being link-time singleton).

	   In addition, we have to special-case the beginning of the list
	   (header) preceding @found_node, which can be distinguished with
	   NULL being used as a key (second allowing condition below). */
	if (found_node->refcount > 1 || cur_node->key == NULL) {
		for (level = SKIPLIST_LEVEL_MIN; level <= found_node->level; level++) {
			found_node->forward[level] = cur_node->forward[level];
		}
		found_node->level = SKIPLIST_LEVEL_MIN - 1;  /* no "forward" drop */
		free(cur_node->forward);
		cur_node->forward = found_node->forward;
	}
	skiplist_node_deref(found_node, list);

	/* Remove unused levels from @list -- stop removing levels as soon as a
	 * used level is found. Unused levels can occur if @found_node had the
	 * highest level.
	 */
	for (level = list->level; level >= SKIPLIST_LEVEL_MIN; level--) {
		if (list->header->forward[level])
			break;

		list->level--;
	}

	list->length--;

	return QB_TRUE;
}

static void *
skiplist_get(struct qb_map *map, const char *key)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *n = skiplist_lookup(list, key);
	if (n) {
		return n->value;
	}

	return NULL;
}

static qb_map_iter_t *
skiplist_iter_create(struct qb_map *map, const char *prefix)
{
	struct skiplist_iter *i = malloc(sizeof(struct skiplist_iter));
	struct skiplist *list = (struct skiplist *)map;
	if (i == NULL) {
		return NULL;
	}
	i->i.m = map;
	i->n = list->header;
	i->n->refcount++;
	return (qb_map_iter_t *) i;
}

static const char *
skiplist_iter_next(qb_map_iter_t * i, void **value)
{
	struct skiplist_iter *si = (struct skiplist_iter *)i;
	struct skiplist_node *p = si->n;

	if (p == NULL) {
		return NULL;
	}
	si->n = skiplist_node_next(p);
	if (si->n == NULL) {
		skiplist_node_deref(p, (struct skiplist *)i->m);
		return NULL;
	}
	si->n->refcount++;
	skiplist_node_deref(p, (struct skiplist *)i->m);
	*value = si->n->value;
	return si->n->key;
}

static void
skiplist_iter_free(qb_map_iter_t * i)
{
	free(i);
}

static size_t
skiplist_count_get(struct qb_map *map)
{
	struct skiplist *list = (struct skiplist *)map;
	return list->length;
}

qb_map_t *
qb_skiplist_create(void)
{
	struct skiplist *sl = malloc(sizeof(struct skiplist));
	if (sl == NULL) {
		return NULL;
	}

	srand(time(NULL));

	sl->map.put = skiplist_put;
	sl->map.get = skiplist_get;
	sl->map.rm = skiplist_rm;
	sl->map.count_get = skiplist_count_get;
	sl->map.iter_create = skiplist_iter_create;
	sl->map.iter_next = skiplist_iter_next;
	sl->map.iter_free = skiplist_iter_free;
	sl->map.destroy = skiplist_destroy;
	sl->map.notify_add = skiplist_notify_add;
	sl->map.notify_del = skiplist_notify_del;
	sl->level = SKIPLIST_LEVEL_MIN;
	sl->length = 0;
	sl->header = skiplist_header_node_new();

	return (qb_map_t *) sl;
}
