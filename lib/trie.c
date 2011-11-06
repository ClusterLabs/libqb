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
#include <qb/qblist.h>
#include <qb/qbmap.h>
#include "map_int.h"

struct trie_iter {
	struct qb_map_iter i;
	const char *prefix;
	struct trie_node *n;
	struct trie_node *root;
};

struct trie_node {
	uint32_t idx;
	char *key;
	void *value;
	struct trie_node **children;
	uint32_t num_children;
	uint32_t refcount;
	struct trie_node *parent;
	struct qb_list_head notifier_head;
};

struct trie {
	struct qb_map map;

	size_t length;
	struct trie_node *header;
};

static void trie_notify(struct trie_node *n, uint32_t event, const char *key,
			void *old_value, void *value);

/*
 * characters are stored in reverse to make accessing the
 * more common case (non-control chars) more space efficient.
 */
#define TRIE_CHAR2INDEX(ch) (126 - ch)

static struct trie_node *
trie_node_next(struct trie_node *node, struct trie_node *root)
{
	struct trie_node *c = node;
	struct trie_node *n;
	struct trie_node *p;
	int i;

keep_going:
	n = NULL;

	/* child/outward
	 */
	for (i = c->num_children - 1; i >= 0; i--) {
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
	/* sibling/parent
	 */
	if (c == root) {
		return NULL;
	}
	p = c;
	do {
		for (i = p->idx - 1; i >= 0; i--) {
			if (p->parent->children[i]) {
				n = p->parent->children[i];
				break;
			}
		}
		if (n == NULL) {
			p = p->parent;
		}
	} while (n == NULL && p != root);

	if (n) {
		if (n->value) {
			return n;
		}
		if (n == root) {
			return NULL;
		}
		c = n;
		goto keep_going;
	}

	return n;
}

static void
trie_node_destroy(struct trie *t, struct trie_node *n)
{
	trie_notify(n, QB_MAP_NOTIFY_DELETED, n->key, n->value, NULL);

	n->key = NULL;
	n->value = NULL;
}

static void
trie_node_deref(struct trie *t, struct trie_node *node)
{
	node->refcount--;
	if (node->refcount > 0) {
		return;
	}
	trie_node_destroy(t, node);
}

static void
trie_destroy(struct qb_map *map)
{
	struct trie *t = (struct trie *)map;

	struct trie_node *cur_node = t->header;
	struct trie_node *fwd_node;

	do {
		fwd_node = trie_node_next(cur_node, t->header);
		trie_node_destroy(t, cur_node);
	} while ((cur_node = fwd_node));

	free(t);
}

static struct trie_node *
trie_new_node(struct trie *t, struct trie_node *parent)
{
	struct trie_node *new_node = calloc(1, sizeof(struct trie_node));

	if (new_node == NULL) {
		return NULL;
	}

	new_node->parent = parent;

	new_node->num_children = 30;
	new_node->children = calloc(new_node->num_children,
				    sizeof(struct trie_node *));
	if (new_node->children) {
		free(new_node);
		return NULL;
	}
	qb_list_init(&new_node->notifier_head);
	return new_node;
}

static struct trie_node *
trie_lookup(struct trie *t, const char *key, int32_t create_path)
{
	struct trie_node *cur_node = t->header;
	struct trie_node *new_node;
	int old_max_idx;
	int i;
	char *cur = (char *)key;
	int idx = TRIE_CHAR2INDEX(key[0]);

	do {
		if (idx >= cur_node->num_children) {
			if (!create_path) {
				return NULL;
			}
			if (cur_node->num_children == 0) {
				old_max_idx = 0;
			} else {
				old_max_idx = cur_node->num_children;
			}
			cur_node->num_children = idx + 1;
			cur_node->children = realloc(cur_node->children,
						     (cur_node->num_children *
						      sizeof(struct trie_node
							     *)));
			/*
			   printf("%s(%d) %d %d\n", __func__, idx,
			   old_max_idx, cur_node->num_children);
			 */
			for (i = old_max_idx; i < cur_node->num_children; i++) {
				cur_node->children[i] = NULL;
			}
		}
		if (cur_node->children[idx] == NULL) {
			if (!create_path) {
				return NULL;
			}
			new_node = trie_new_node(t, cur_node);
			if (new_node == NULL) {
				return NULL;
			}
			new_node->idx = idx;
			cur_node->children[idx] = new_node;
		}
		cur_node = cur_node->children[idx];
		cur++;
		idx = TRIE_CHAR2INDEX(*cur);
	} while (*cur != '\0');

	return cur_node;
}

static void
trie_put(struct qb_map *map, const char *key, const void *value)
{
	struct trie *t = (struct trie *)map;
	struct trie_node *n = trie_lookup(t, key, QB_TRUE);
	if (n) {
		const char *old_value = n->value;
		const char *old_key = n->key;

		n->key = (char *)key;
		n->value = (void *)value;

		if (old_value == NULL) {
			n->refcount++;
			t->length++;
			trie_notify(n, QB_MAP_NOTIFY_INSERTED,
				    n->key, NULL, n->value);
		} else {
			trie_notify(n, QB_MAP_NOTIFY_REPLACED,
				    (char *)old_key, (void *)old_value,
				    (void *)value);
		}
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

static void
trie_notify(struct trie_node *n,
	    uint32_t event,
	    const char *key, void *old_value, void *value)
{
	struct trie_node *c = n;
	struct qb_list_head *list;
	struct qb_map_notifier *tn;

	do {
		for (list = c->notifier_head.next;
		     list != &c->notifier_head; list = list->next) {
			tn = qb_list_entry(list, struct qb_map_notifier, list);

			if ((tn->events & event) &&
			    ((tn->events & QB_MAP_NOTIFY_RECURSIVE) ||
			     (n == c))) {
				tn->callback(event, (char *)key, old_value,
					     value, tn->user_data);
			}
		}
		c = c->parent;
	} while (c);
}

static int32_t
trie_notify_add(qb_map_t * m, const char *key,
		qb_map_notify_fn fn, int32_t events, void *user_data)
{
	struct trie *t = (struct trie *)m;
	struct qb_map_notifier *f;
	struct trie_node *n;

	if (key) {
		n = trie_lookup(t, key, QB_TRUE);
	} else {
		n = t->header;
	}
	if (n) {
		f = malloc(sizeof(struct qb_map_notifier));
		if (f == NULL) {
			return -errno;
		}
		f->events = events;
		f->user_data = user_data;
		f->callback = fn;
		qb_list_init(&f->list);
		if (events & QB_MAP_NOTIFY_RECURSIVE) {
			qb_list_add(&f->list, &n->notifier_head);
		} else {
			qb_list_add_tail(&f->list, &n->notifier_head);
		}
		return 0;
	}
	return -EINVAL;
}

static int32_t
trie_notify_del(qb_map_t * m, const char *key,
		qb_map_notify_fn fn, int32_t events)
{
	struct trie *t = (struct trie *)m;
	struct qb_map_notifier *f;
	struct trie_node *n;
	struct qb_list_head *list;

	if (key) {
		n = trie_lookup(t, key, QB_TRUE);
	} else {
		n = t->header;
	}
	if (n == NULL) {
		return -ENOENT;
	}
	for (list = n->notifier_head.next;
	     list != &n->notifier_head; list = list->next) {
		f = qb_list_entry(list, struct qb_map_notifier, list);

		if (f->events == events && f->callback == fn) {
			qb_list_del(&f->list);
			free(f);
			return 0;
		}
	}
	return -ENOENT;
}

static qb_map_iter_t *
trie_iter_create(struct qb_map *map, const char *prefix)
{
	struct trie_iter *i = malloc(sizeof(struct trie_iter));
	struct trie *t = (struct trie *)map;
	if (i == NULL) {
		return NULL;
	}
	i->i.m = map;
	i->prefix = prefix;
	i->n = t->header;
	i->root = t->header;
	i->n->refcount++;
	return (qb_map_iter_t *) i;
}

static const char *
trie_iter_next(qb_map_iter_t * i, void **value)
{
	struct trie_iter *si = (struct trie_iter *)i;
	struct trie_node *p = si->n;
	struct trie *t = (struct trie *)(i->m);

	if (p == NULL) {
		return NULL;
	}

	if (p->parent == NULL && si->prefix) {
		si->root = trie_lookup(t, si->prefix, QB_FALSE);
		if (si->root == NULL) {
			si->n = NULL;
		} else if (si->root->value == NULL) {
			si->n = trie_node_next(si->root, si->root);
		} else {
			si->n = si->root;
		}
	} else {
		si->n = trie_node_next(p, si->root);
	}
	if (si->n == NULL) {
		trie_node_deref((struct trie *)i->m, p);
		return NULL;
	}
	si->n->refcount++;
	trie_node_deref((struct trie *)i->m, p);
	*value = si->n->value;
	return si->n->key;
}

static void
trie_iter_free(qb_map_iter_t * i)
{
	free(i);
}

static size_t
trie_count_get(struct qb_map *map)
{
	struct trie *list = (struct trie *)map;
	return list->length;
}

qb_map_t *
qb_trie_create(void)
{
	struct trie *t = malloc(sizeof(struct trie));
	if (t == NULL) {
		return NULL;
	}

	t->map.put = trie_put;
	t->map.get = trie_get;
	t->map.rm = trie_rm;
	t->map.count_get = trie_count_get;
	t->map.iter_create = trie_iter_create;
	t->map.iter_next = trie_iter_next;
	t->map.iter_free = trie_iter_free;
	t->map.destroy = trie_destroy;
	t->map.notify_add = trie_notify_add;
	t->map.notify_del = trie_notify_del;
	t->length = 0;
	t->header = trie_new_node(t, NULL);

	return (qb_map_t *) t;
}
