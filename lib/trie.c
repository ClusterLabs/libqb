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

struct trie_iter {
	struct qb_map_iter i;
	const char *key;
	struct trie_node *n;
};

struct trie_node {
	char key_ch;
	char *key;
	void *value;
	struct trie_node **children;
	uint32_t num_children;
	int32_t refcount;
	struct trie_node *parent;
};

struct trie {
	struct qb_map map;

	size_t length;
	struct trie_node *header;
};

static int32_t trie_rm(struct qb_map *list, const char *key);

static struct trie_node *
trie_node_next(struct trie_node *node)
{
	struct trie_node *c = node;
	struct trie_node *n;
	struct trie_node *p;
	int i;

keep_going:
	n = NULL;

	// child/outward
	for (i = 0; i < c->num_children; i++) {
		if (c->children[i]) {
			n = c->children[i];
			break;
		}
	}
	if (n) {
		if (n->value) {
			return n;
		} else {
			c = n;
			goto keep_going;
		}
	}
	// sibling/parent
	if (c->parent == NULL) {
		return NULL;
	}
	p = c;
	do {
		for (i = (p->key_ch + 1); i < p->parent->num_children; i++) {
			if (p->parent->children[i]) {
				n = p->parent->children[i];
				break;
			}
		}
		if (n == NULL) {
			p = p->parent;
		}
	} while (n == NULL && p->parent);

	if (n) {
		if (n->value) {
			return n;
		}
		if (n->parent == NULL) {
			return NULL;
		}
		c = n;
		goto keep_going;
	}

	return n;
}

static void
trie_node_destroy(struct trie *t, struct trie_node *node)
{
	if (t->map.value_destroy_func && node != t->header) {
		t->map.value_destroy_func(node->value);
	}

	if (t->map.key_destroy_func && node != t->header) {
		t->map.key_destroy_func((void*)node->key);
	}
	node->key = NULL;
	node->value = NULL;
}

static void
trie_node_deref(struct trie *t, struct trie_node *node)
{
	node->refcount--;
	if (node->refcount == 0) {
		trie_node_destroy(t, node);
	}
}

static void
trie_destroy(struct qb_map *map)
{
	struct trie *list = (struct trie *)map;
#if 0
	struct trie_node *cur_node = list->header;
	struct trie_node *fwd_node;

	do {
		fwd_node = trie_node_next(cur_node);
		trie_node_destroy(cur_node, list);
	} while ((cur_node = fwd_node));
#endif
	free(list);
}

static struct trie_node*
trie_new_node(struct trie * t, struct trie_node* parent)
{
	struct trie_node* new_node = calloc(1, sizeof(struct trie_node));

	new_node->parent = parent;

	return new_node;
}

static struct trie_node*
trie_lookup(struct trie * t, const char *key, int32_t create_path)
{
	struct trie_node* cur_node = t->header;
	struct trie_node* new_node;
	int old_max_idx;
	int i;
	char *cur = (char*)key;
	int idx = key[0];

	do {
		if (idx >= cur_node->num_children) {
			if (!create_path) {
				return NULL;
			}
			if (cur_node->num_children == 0) {
				old_max_idx =  0;
			} else {
				old_max_idx = cur_node->num_children;
			}
			cur_node->num_children = idx + 1;
			cur_node->children = realloc(cur_node->children,
						     (cur_node->num_children *
						      sizeof(struct trie_node*)));
			//printf("%s(%d) %d %d\n", __func__, idx, old_max_idx, cur_node->num_children);
			for (i = old_max_idx; i < cur_node->num_children; i++) {
				cur_node->children[i] = NULL;
			}
		}
		if (cur_node->children[idx] == NULL) {
			if (!create_path) {
				return NULL;
			}
			new_node = trie_new_node(t, cur_node);
			new_node->key_ch = *cur;
			cur_node->children[idx] = new_node;
		}
		cur_node = cur_node->children[idx];
		cur++;
		idx = *cur;
	} while (*cur != '\0');

	return cur_node;
}

static void
trie_put(struct qb_map * map, const char *key, const void *value)
{
	struct trie *t = (struct trie *)map;
	struct trie_node *n = trie_lookup(t, key, QB_TRUE);
	if (n) {
		if (n->value == NULL) {
			t->length++;
		} else {
			if (t->map.value_destroy_func && n != t->header) {
				t->map.value_destroy_func(n->value);
			}

			if (t->map.key_destroy_func && n != t->header) {
				t->map.key_destroy_func((void*)n->key);
			}
		}
		n->key = (char*)key;
		n->value = (void*)value;
		n->refcount++;
	}
}

static int32_t
trie_rm(struct qb_map *map, const char *key)
{
	struct trie *t = (struct trie *)map;
	struct trie_node *n = trie_lookup(t, key, QB_FALSE);
	if (n) {
		trie_node_deref(t, n);
		t->length--;
		return QB_TRUE;
	} else {
		return QB_FALSE;
	}
}

static void *
trie_get(struct qb_map *map, const char *key)
{
	struct trie *t = (struct trie *)map;
	struct trie_node *n = trie_lookup(t, key, QB_FALSE);
	if (n) {
		return n->value;
	}

	return NULL;
}

static qb_map_iter_t*
trie_iter_create(struct qb_map * map)
{
	struct trie_iter *i = malloc(sizeof(struct trie_iter));
	struct trie *list = (struct trie *)map;
	i->i.m = map;
	i->n = list->header;
	i->n->refcount++;
	return (qb_map_iter_t*)i;
}

static const char*
trie_iter_next(qb_map_iter_t* i, void** value)
{
	struct trie_iter *si = (struct trie_iter*)i;
	struct trie_node *p = si->n;

	if (p == NULL) {
		return NULL;
	}
	si->n = trie_node_next(p);
	trie_node_deref((struct trie *)i->m, p);
	if (si->n == NULL) {
		return NULL;
	}
	si->n->refcount++;
	*value = si->n->value;
	return si->n->key;
}

static void
trie_iter_free(qb_map_iter_t* i)
{
	free(i);
}

static size_t
trie_count_get(struct qb_map * map)
{
	struct trie *list = (struct trie *)map;
	return list->length;
}

qb_map_t *
qb_trie_create(qb_destroy_notifier_func key_destroy_func,
		   qb_destroy_notifier_func value_destroy_func)
{
	struct trie *sl = calloc(1, sizeof(struct trie));

	srand(time(NULL));

	sl->map.key_destroy_func = key_destroy_func;
	sl->map.value_destroy_func = value_destroy_func;

	sl->map.put = trie_put;
	sl->map.get = trie_get;
	sl->map.rm = trie_rm;
	sl->map.count_get = trie_count_get;
	sl->map.iter_create = trie_iter_create;
	sl->map.iter_next = trie_iter_next;
	sl->map.iter_free = trie_iter_free;
	sl->map.destroy = trie_destroy;

	sl->length = 0;
	sl->header = trie_new_node(sl, NULL);

	return (qb_map_t *) sl;
}

