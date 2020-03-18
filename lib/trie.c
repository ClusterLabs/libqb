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
	char *segment;
	uint32_t num_segments;
	char *key;
	void *value;
	struct trie_node **children;
	uint32_t num_children;
	uint32_t refcount;
	struct trie_node *parent;
	struct qb_list_head *notifier_head;
};

struct trie {
	struct qb_map map;

	size_t length;
	uint32_t num_nodes;
	uint32_t mem_used;
	struct trie_node *header;
};

static void trie_notify(struct trie_node *n, uint32_t event, const char *key,
			void *old_value, void *value);
static struct trie_node *trie_new_node(struct trie *t, struct trie_node *parent);
static void trie_destroy_node(struct trie_node *node);

/*
 * characters are stored in reverse to make accessing the
 * more common case (non-control chars) more space efficient.
 */
#define TRIE_CHAR2INDEX(ch) (127 - (signed char)ch)
#define TRIE_INDEX2CHAR(idx) (127 - (signed char)idx)


static int32_t
trie_node_alive(struct trie_node *node)
{
	if (node->value == NULL ||
	    node->refcount <= 0) {
		return QB_FALSE;
	}
	return QB_TRUE;
}

static struct trie_node *
trie_node_next(struct trie_node *node, struct trie_node *root, int all)
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
		if (all || trie_node_alive(n)) {
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
		if (all || trie_node_alive(n)) {
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

static struct trie_node *
new_child_node(struct trie *t, struct trie_node * parent, char ch)
{
	struct trie_node *new_node;
	int old_max_idx;
	int i;
	int idx = TRIE_CHAR2INDEX(ch);

	if (idx >= parent->num_children) {
		old_max_idx = parent->num_children;
		parent->num_children = QB_MAX(idx + 1, 30);
		t->mem_used += (sizeof(struct trie_node*) * (parent->num_children - old_max_idx));
		parent->children = realloc(parent->children,
				(parent->num_children * sizeof(struct trie_node*)));
		if (parent->children == NULL) {
			return NULL;
		}
		for (i = old_max_idx; i < parent->num_children; i++) {
			parent->children[i] = NULL;
		}
	}
	new_node = trie_new_node(t, parent);
	if (new_node == NULL) {
		return NULL;
	}
	new_node->idx = idx;
	parent->children[idx] = new_node;
	return new_node;
}


static struct trie_node *
trie_node_split(struct trie *t, struct trie_node *cur_node, int seg_cnt)
{
	struct trie_node *split_node;
	struct trie_node ** children = cur_node->children;
	uint32_t num_children = cur_node->num_children;
	struct qb_list_head *tmp;
	int i;
	int s;

	cur_node->children = NULL;
	cur_node->num_children = 0;
	split_node = new_child_node(t, cur_node, cur_node->segment[seg_cnt]);
	if (split_node == NULL) {
		return NULL;
	}
	split_node->children = children;
	split_node->num_children = num_children;
	for (i = 0; i < split_node->num_children; i++) {
		if (split_node->children[i]) {
			split_node->children[i]->parent = split_node;
		}
	}
	split_node->value = cur_node->value;
	split_node->key = cur_node->key;
	split_node->refcount = cur_node->refcount;
	cur_node->value = NULL;
	cur_node->key = NULL;
	cur_node->refcount = 0;
	/* move notifier list to split */
	tmp = split_node->notifier_head;
	split_node->notifier_head = cur_node->notifier_head;
	cur_node->notifier_head = tmp;
	qb_list_init(cur_node->notifier_head);

	if (seg_cnt < cur_node->num_segments) {
		split_node->num_segments = cur_node->num_segments - seg_cnt - 1;
		split_node->segment = malloc(split_node->num_segments * sizeof(char));
		if (split_node->segment == NULL) {
			trie_destroy_node(split_node);
			return NULL;
		}
		for (i = (seg_cnt + 1); i < cur_node->num_segments; i++) {
			s = i - seg_cnt - 1;
			split_node->segment[s] = cur_node->segment[i];
			cur_node->segment[i] = '\0';
		}
		cur_node->num_segments = seg_cnt;
	}
	return cur_node;
}

static struct trie_node *
trie_insert(struct trie *t, const char *key)
{
	struct trie_node *cur_node = t->header;
	struct trie_node *new_node;
	char *cur = (char *)key;
	int idx = TRIE_CHAR2INDEX(key[0]);
	int seg_cnt = 0;

	do {
		new_node = NULL;
		if (cur_node->num_segments > 0 &&
		    seg_cnt < cur_node->num_segments) {
			if (cur_node->segment[seg_cnt] == *cur) {
				/* we found the char in the segment */
				seg_cnt++;
			} else {
				cur_node = trie_node_split(t, cur_node, seg_cnt);
				if (cur_node == NULL) {
					return NULL;
				}
				new_node = new_child_node(t, cur_node, *cur);
				if (new_node == NULL) {
					return NULL;
				}
			}
		} else if (idx < cur_node->num_children &&
		    cur_node->children[idx]) {
			/* the char can be found on the next node */
			new_node = cur_node->children[idx];
		} else if (cur_node == t->header) {
			/* the root node is empty so make it on the next node */
			new_node = new_child_node(t, cur_node, *cur);
			if (new_node == NULL) {
				return NULL;
			}
		} else if (cur_node->value == NULL &&
			   qb_list_empty(cur_node->notifier_head) &&
			   cur_node->num_children == 0 &&
			   seg_cnt == cur_node->num_segments) {
			/* we are on a leaf (with no value) so just add it as a segment */
			cur_node->segment = realloc(cur_node->segment, cur_node->num_segments + 1);
			cur_node->segment[cur_node->num_segments] = *cur;
			t->mem_used += sizeof(char);
			cur_node->num_segments++;
			seg_cnt++;
		} else if (seg_cnt == cur_node->num_segments) {
			/* on the last segment need to make a new node */
			new_node = new_child_node(t, cur_node, *cur);
			if (new_node == NULL) {
				return NULL;
			}
		} else /* need_to_split */ {
			cur_node = trie_node_split(t, cur_node, seg_cnt);
			if (cur_node == NULL) {
				return NULL;
			}
			new_node = new_child_node(t, cur_node, *cur);
			if (new_node == NULL) {
				return NULL;
			}
		}
		if (new_node) {
			seg_cnt = 0;
			cur_node = new_node;
		}
		cur++;
		idx = TRIE_CHAR2INDEX(*cur);
	} while (*cur != '\0');

	if (cur_node->num_segments > 0 &&
	    seg_cnt < cur_node->num_segments) {
		/* we need to split */
		cur_node = trie_node_split(t, cur_node, seg_cnt);
		if (cur_node == NULL) {
			return NULL;
		}
		new_node = new_child_node(t, cur_node, *cur);
		if (new_node == NULL) {
			return NULL;
		}
	}

	return cur_node;
}

static struct trie_node *
trie_lookup(struct trie *t, const char *key, int exact_match)
{
	struct trie_node *cur_node = t->header;
	char *cur = (char *)key;
	int idx = TRIE_CHAR2INDEX(key[0]);
	int seg_cnt = 0;

	do {
		if (cur_node->num_segments > 0 &&
		    seg_cnt < cur_node->num_segments) {
			if (cur_node->segment[seg_cnt] == *cur) {
				/* we found the char in the segment */
				seg_cnt++;
			} else {
				return NULL;
			}
		} else if (idx < cur_node->num_children &&
		    cur_node->children[idx]) {
			/* the char can be found on the next node */
			cur_node = cur_node->children[idx];
			seg_cnt = 0;
		} else {
			return NULL;
		}
		cur++;
		idx = TRIE_CHAR2INDEX(*cur);
	} while (*cur != '\0');

	if (exact_match &&
	    cur_node->num_segments > 0 &&
	    seg_cnt < cur_node->num_segments) {
		return NULL;
	}

	return cur_node;
}

static void
trie_node_release(struct trie *t, struct trie_node *node)
{
	int i;
	int empty = QB_FALSE;

	if (node->key == NULL &&
	    node->parent != NULL &&
	    qb_list_empty(node->notifier_head)) {
		struct trie_node *p = node->parent;

		if (node->num_children == 0) {
			empty = QB_TRUE;
		} else {
			empty = QB_TRUE;
			for (i = node->num_children - 1; i >= 0; i--) {
				if (node->children[i]) {
					empty = QB_FALSE;
					break;
				}
			}
		}
		if (!empty) {
			return;
		}

		/*
		 * unlink the node from the parent
		 */
		p->children[node->idx] = NULL;
		trie_destroy_node(node);
		t->num_nodes--;
		t->mem_used -= sizeof(struct trie_node);

		trie_node_release(t, p);
	}
}

static void
trie_node_destroy(struct trie *t, struct trie_node *n)
{
	if (n->value == NULL) {
		return;
	}
	trie_notify(n, QB_MAP_NOTIFY_DELETED, n->key, n->value, NULL);

	n->key = NULL;
	n->value = NULL;

	trie_node_release(t, n);
}

static void
trie_print_node(struct trie_node *n, struct trie_node *r, const char *suffix)
{
	int i;

	if (n->parent) {
		trie_print_node(n->parent, n, suffix);
	}
	if (n->idx == 0) {
		return;
	}

	printf("[%c", (char) TRIE_INDEX2CHAR(n->idx));
	for (i = 0; i < n->num_segments; i++) {
		printf("%c", n->segment[i]);
	}
	if (n == r) {
#ifndef S_SPLINT_S
		printf("] (%" PRIu32 ") %s\n", n->refcount, suffix);
#endif /* S_SPLINT_S */
	} else {
		printf("] ");
	}
}

static void
trie_node_ref(struct trie *t, struct trie_node *node)
{
	if (t->header == node) {
		return;
	}
	node->refcount++;
}

static void
trie_node_deref(struct trie *t, struct trie_node *node)
{
	if (!trie_node_alive(node)) {
		return;
	}
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
		fwd_node = trie_node_next(cur_node, t->header, QB_FALSE);
		trie_node_destroy(t, cur_node);
	} while ((cur_node = fwd_node));

	free(t);
}

static void
trie_destroy_node(struct trie_node *node)
{
	free(node->segment);
	free(node->children);
	free(node->notifier_head);
	free(node);
}

static struct trie_node *
trie_new_node(struct trie *t, struct trie_node *parent)
{
	struct trie_node *new_node = calloc(1, sizeof(struct trie_node));

	if (new_node == NULL) {
		return NULL;
	}

	new_node->notifier_head = calloc(1, sizeof(struct qb_list_head));
	if (new_node->notifier_head == NULL) {
		free(new_node);
		return NULL;
	}

	new_node->parent = parent;
	new_node->num_children = 0;
	new_node->children = NULL;
	new_node->num_segments = 0;
	new_node->segment = NULL;
	t->num_nodes++;
	t->mem_used += sizeof(struct trie_node);
	qb_list_init(new_node->notifier_head);
	return new_node;
}

void
qb_trie_dump(qb_map_t* m)
{
	struct trie * t = (struct trie*)m;
	struct trie_node *n;

	if (t == NULL) {
		return;
	}

#ifndef S_SPLINT_S
	printf("nodes: %" PRIu32 ", bytes: %" PRIu32 "\n", t->num_nodes, t->mem_used);
#endif /* S_SPLINT_S */

	n = t->header;
	do {
		if (n->num_children == 0) {
			trie_print_node(n, n, " ");
		}
		n = trie_node_next(n, t->header, QB_FALSE);
	} while (n);
}

static void
trie_put(struct qb_map *map, const char *key, const void *value)
{
	struct trie *t = (struct trie *)map;
	struct trie_node *n = trie_insert(t, key);
	if (n) {
		const char *old_value = n->value;
		const char *old_key = n->key;

		n->key = (char *)key;
		n->value = (void *)value;

		if (old_value == NULL) {
			trie_node_ref(t, n);
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
	struct trie_node *n = trie_lookup(t, key, QB_TRUE);
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
	struct trie_node *n = trie_lookup(t, key, QB_TRUE);
	if (n) {
		return n->value;
	}

	return NULL;
}

static void
trie_notify_deref(struct qb_map_notifier *f)
{
	f->refcount--;
	if (f->refcount == 0) {
		qb_list_del(&f->list);
		free(f);
	}
}

static void
trie_notify_ref(struct qb_map_notifier *f)
{
	f->refcount++;
}

static void
trie_notify(struct trie_node *n,
	    uint32_t event, const char *key, void *old_value, void *value)
{
	struct trie_node *c = n;
	struct qb_list_head *list;
	struct qb_list_head *next;
	struct qb_list_head *head;
	struct qb_map_notifier *tn;

	do {
		head = c->notifier_head;
		qb_list_for_each_safe(list, next, head) {
			tn = qb_list_entry(list, struct qb_map_notifier, list);
			trie_notify_ref(tn);

			if ((tn->events & event) &&
			    ((tn->events & QB_MAP_NOTIFY_RECURSIVE) ||
			     (n == c))) {
				tn->callback(event, (char *)key, old_value,
					     value, tn->user_data);
			}
			if (((event & QB_MAP_NOTIFY_DELETED) ||
			     (event & QB_MAP_NOTIFY_REPLACED)) &&
			    (tn->events & QB_MAP_NOTIFY_FREE)) {
				tn->callback(QB_MAP_NOTIFY_FREE, (char *)key,
					     old_value, value, tn->user_data);
			}

			trie_notify_deref(tn);
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
	struct qb_list_head *list;
	int add_to_tail = QB_FALSE;

	if (key) {
		n = trie_lookup(t, key, QB_TRUE);
		if (n == NULL) {
			n = trie_insert(t, key);
		}
	} else {
		n = t->header;
	}
	if (n) {
		qb_list_for_each(list, n->notifier_head) {
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
		f->refcount = 1;
		qb_list_init(&f->list);
		if (key) {
			if (events & QB_MAP_NOTIFY_RECURSIVE) {
				add_to_tail = QB_TRUE;
			}
		} else {
			if (events & QB_MAP_NOTIFY_FREE) {
				add_to_tail = QB_TRUE;
			}
		}
		if (add_to_tail) {
			qb_list_add_tail(&f->list, n->notifier_head);
		} else {
			qb_list_add(&f->list, n->notifier_head);
		}
		return 0;
	}
	return -EINVAL;
}

static int32_t
trie_notify_del(qb_map_t * m, const char *key,
		qb_map_notify_fn fn, int32_t events,
		int32_t cmp_userdata, void *user_data)
{
	struct trie *t = (struct trie *)m;
	struct trie_node *n;
	struct qb_list_head *list;
	struct qb_list_head *next;
	int32_t found = QB_FALSE;

	if (key) {
		n = trie_lookup(t, key, QB_FALSE);
	} else {
		n = t->header;
	}
	if (n == NULL) {
		return -ENOENT;
	}
	qb_list_for_each_safe(list, next, n->notifier_head) {
		struct qb_map_notifier *f = qb_list_entry(list, struct qb_map_notifier, list);

		if (f->events == events && f->callback == fn) {
			if (cmp_userdata && (f->user_data == user_data)) {
				trie_notify_deref(f);
				found = QB_TRUE;
			} else if (!cmp_userdata) {
				trie_notify_deref(f);
				found = QB_TRUE;
			}
		}

	}
	if (found) {
		trie_node_release(t, n);
		return 0;
	} else {
		return -ENOENT;
	}
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
			si->n = trie_node_next(si->root, si->root, QB_FALSE);
		} else {
			si->n = si->root;
		}
	} else {
		si->n = trie_node_next(p, si->root, QB_FALSE);
	}
	if (si->n == NULL) {
		trie_node_deref(t, p);
		return NULL;
	}
	trie_node_ref(t, si->n);
	trie_node_deref(t, p);
	*value = si->n->value;
	return si->n->key;
}

static void
trie_iter_free(qb_map_iter_t * i)
{
	struct trie_iter *si = (struct trie_iter *)i;
	struct trie *t = (struct trie *)(i->m);

	if (si->n != NULL) {
		/* if free'ing the iterator before getting to the last
		 * node make sure we de-ref the current node.
		 */
		trie_node_deref(t, si->n);
	}
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
	t->num_nodes = 0;
	t->mem_used = sizeof(struct trie);
	t->header = trie_new_node(t, NULL);

	return (qb_map_t *) t;
}
