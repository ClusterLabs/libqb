/*
 * Copyright (C) 2006-2010, 2009 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>
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
#ifndef QB_LIST_H_DEFINED
#define QB_LIST_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file qblist.h
 * @author Steven Dake <sdake@redhat.com>
 *
 * This is a kernel style list implementation.
 */


struct qb_list_head {
	struct qb_list_head *next;
	struct qb_list_head *prev;
};

/**
 * @def QB_DECLARE_LIST_INIT()
 * Declare and initialize a list head.
 */
#define QB_DECLARE_LIST_INIT(name) \
    struct qb_list_head name = { &(name), &(name) }

/**
 * Initialize the list entry.
 * Points next and prev pointers to head.
 */
static void inline qb_list_init (struct qb_list_head *head)
{
	head->next = head;
	head->prev = head;
}

/**
 * Add this element to the list.
 *
 * @param element the new element to insert.
 * @param head the list head to add to.
 */
static void inline qb_list_add (struct qb_list_head *element, struct qb_list_head *head)
{
	head->next->prev = element;
	element->next = head->next;
	element->prev = head;
	head->next = element;
}

/**
 * Add to the list (but at the end of the list).
 * @see qb_list_add()
 */
static void inline qb_list_add_tail (struct qb_list_head *element, struct qb_list_head *head)
{
	head->prev->next = element;
	element->next = head;
	element->prev = head->prev;
	head->prev = element;
}

/**
 * Delete an entry from the list.
 *
 * The code below shows howto delete an entry safely from within a list.
 * @code
 *	struct my_struct *mine;
 *	struct list_head *iter, *iter_next;
 *
 *	for (iter = my_list_head.next;
 *		iter != &my_list_head;
 *		iter = iter_next) {
 *
 *		iter_next = iter->next;
 *
 *		mine = qb_list_entry(iter, struct my_struct, list);
 *		qb_list_del (&mine->list);
 *		free (mine);
 *	}
 *
 * @endcode
 */
static void inline qb_list_del (struct qb_list_head *_remove)
{
	_remove->next->prev = _remove->prev;
	_remove->prev->next = _remove->next;
#ifdef DEBUG
	_remove->next = (struct qb_list_head *)0xdeadb33f;
	_remove->prev = (struct qb_list_head *)0xdeadb33f;
#endif
}

/**
 * @def qb_list_entry(ptr,type,member)
 * Get the user data from the list entry.
 *
 * The code below shows how to use qb_list_entry() from within a loop.
 * @code
 *	struct my_struct *mine = NULL;
 *	struct qb_list_head *iter;
 *
 *	for (iter = my_list_head.next;
 *		iter != &my_list_head;
 *		iter = iter->next) {
 *		mine = qb_list_entry(iter, struct my_struct, list);
 *		if (mine == NULL) {
 *			continue;
 *		}
 *		do_some_thing (mine);
 *	}
 *
 * @endcode
 */
#define qb_list_entry(ptr,type,member)\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * A quick test to see if the list is empty (pointing to it's self).
 */
static inline int qb_list_empty(const struct qb_list_head *l)
{
	return l->next == l;
}

static inline void qb_list_splice (struct qb_list_head *list, struct qb_list_head *head)
{
	struct qb_list_head *first;
	struct qb_list_head *last;
	struct qb_list_head *current;

	first = list->next;
	last = list->prev;
	current = head->next;

	first->prev = head;
	head->next = first;
	last->next = current;
	current->prev = last;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* QB_LIST_H_DEFINED */

