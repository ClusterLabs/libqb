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

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdint.h>
#include <qb/qbdefs.h>

/**
 * @file qblist.h
 * This is a kernel style list implementation.
 *
 * @author Steven Dake <sdake@redhat.com>
 */

struct qb_list_head {
	struct qb_list_head *next;
	struct qb_list_head *prev;
};

/**
 * @def QB_LIST_DECLARE()
 * Declare and initialize a list head.
 */
#define QB_LIST_DECLARE(name) \
    struct qb_list_head name = { &(name), &(name) }

#define QB_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/**
 * Initialize the list entry.
 *
 * Points next and prev pointers to head.
 * @param head pointer to the list head
 */
static inline void qb_list_init(struct qb_list_head *head)
{
	head->next = head;
	head->prev = head;
}

/**
 * Add this element to the list.
 *
 * @param element the new element to insert.
 * @param head pointer to the list head
 */
static inline void qb_list_add(struct qb_list_head *element,
			       struct qb_list_head *head)
{
	head->next->prev = element;
	element->next = head->next;
	element->prev = head;
	head->next = element;
}

/**
 * Add to the list (but at the end of the list).
 *
 * @param element pointer to the element to add
 * @param head pointer to the list head
 * @see qb_list_add()
 */
static inline void qb_list_add_tail(struct qb_list_head *element,
				    struct qb_list_head *head)
{
	head->prev->next = element;
	element->next = head;
	element->prev = head->prev;
	head->prev = element;
}

/**
 * Delete an entry from the list.
 *
 * @param _remove the list item to remove
 */
static inline void qb_list_del(struct qb_list_head *_remove)
{
	_remove->next->prev = _remove->prev;
	_remove->prev->next = _remove->next;
}

/**
 * Replace old entry by new one
 * @param old_one: the element to be replaced
 * @param new_one: the new element to insert
 */
static inline void qb_list_replace(struct qb_list_head *old_one,
		struct qb_list_head *new_one)
{
	new_one->next = old_one->next;
	new_one->next->prev = new_one;
	new_one->prev = old_one->prev;
	new_one->prev->next = new_one;
}

/**
 * Tests whether list is the last entry in list head
 * @param list: the entry to test
 * @param head: the head of the list
 * @return boolean true/false
 */
static inline int qb_list_is_last(const struct qb_list_head *list,
		const struct qb_list_head *head)
{
	return list->next == head;
}

/**
 * A quick test to see if the list is empty (pointing to it's self).
 * @param head pointer to the list head
 * @return boolean true/false
 */
static inline int32_t qb_list_empty(const struct qb_list_head *head)
{
	return head->next == head;
}

/**
 * Join two lists.
 * @param list the new list to add.
 * @param head the place to add it in the first list.
 *
 * @note The "list" is reinitialised
 */
static inline void qb_list_splice(struct qb_list_head *list,
				  struct qb_list_head *head)
{
	struct qb_list_head *first = list->next;
	struct qb_list_head *last = list->prev;
	struct qb_list_head *at = head->next;

	if (!qb_list_empty(list)) {
		first->prev = head;
		head->next = first;

		last->next = at;
		at->prev = last;
	}
}

/**
 * Join two lists, each list being a queue
 * @param list: the new list to add.
 * @param head: the place to add it in the first list.
 */
static inline void qb_list_splice_tail(struct qb_list_head *list,
				struct qb_list_head *head)
{
	struct qb_list_head *first = list->next;
	struct qb_list_head *last = list->prev;
	struct qb_list_head *at = head;

	if (!qb_list_empty(list)) {
		first->prev = head->prev;
		head->prev->next = first;

		last->next = at;
		at->prev = last;
	}
}

/**
 * Get the struct for this entry
 * @param ptr:	the &struct list_head pointer.
 * @param type:	the type of the struct this is embedded in.
 * @param member:	the name of the list_struct within the struct.
 */
#define qb_list_entry(ptr,type,member)\
	((type *)((char *)(ptr)-(char*)(&((type *)0)->member)))

/**
 * Get the first element from a list
 * @param ptr:	the &struct list_head pointer.
 * @param type:	the type of the struct this is embedded in.
 * @param member:	the name of the list_struct within the struct.
 */
#define qb_list_first_entry(ptr, type, member) \
	qb_list_entry((ptr)->next, type, member)

/**
 * Iterate over a list
 * @param pos:	the &struct list_head to use as a loop counter.
 * @param head:	the head for your list.
 */
#define qb_list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * Iterate over a list backwards
 * @param pos:	the &struct list_head to use as a loop counter.
 * @param head:	the head for your list.
 */
#define qb_list_for_each_reverse(pos, head) \
	for (pos = (head)->prev; pos != (head); pos = pos->prev)

/**
 * Iterate over a list safe against removal of list entry
 * @param pos:	the &struct list_head to use as a loop counter.
 * @param n:		another &struct list_head to use as temporary storage
 * @param head:	the head for your list.
 */
#define qb_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * Iterate over list of given type
 * @param pos:	the type * to use as a loop counter.
 * @param head:	the head for your list.
 * @param member:	the name of the list_struct within the struct.
 */
#define qb_list_for_each_entry(pos, head, member)			\
	for (pos = qb_list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = qb_list_entry(pos->member.next, typeof(*pos), member))

/**
 * Iterate backwards over list of given type.
 * @param pos:	the type to use as a loop counter.
 * @param head:	the head for your list.
 * @param member: the name of the list_struct within the struct.
 */
#define qb_list_for_each_entry_reverse(pos, head, member)		\
	for (pos = qb_list_entry((head)->prev, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = qb_list_entry(pos->member.prev, typeof(*pos), member))

/**
 * Iterate over list of given type safe against removal of list entry
 * @param pos:		the type * to use as a loop cursor.
 * @param n:		another type * to use as temporary storage
 * @param head:		the head for your list.
 * @param member:	the name of the list_struct within the struct.
 */
#define qb_list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = qb_list_entry((head)->next, typeof(*pos), member),		\
		n = qb_list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 						\
	     pos = n, n = qb_list_entry(n->member.next, typeof(*n), member))

/**
 * Iterate backwards over list safe against removal
 * @param pos:		the type * to use as a loop cursor.
 * @param n:		another type * to use as temporary storage
 * @param head:		the head for your list.
 * @param member:	the name of the list_struct within the struct.
 */
#define qb_list_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = qb_list_entry((head)->prev, typeof(*pos), member),		\
		n = qb_list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 						\
	     pos = n, n = qb_list_entry(n->member.prev, typeof(*n), member))

/**
 * Iterate over list of given type from the current point
 * @param pos:		the type * to use as a loop cursor.
 * @param head:		the head for your list.
 * @param member:	the name of the list_struct within the struct.
 */
#define qb_list_for_each_entry_from(pos, head, member)				\
	for (; &pos->member != (head);						\
	     pos = qb_list_entry(pos->member.next, typeof(*pos), member))

/**
 * Count the number of items in the list.
 * @param head:	the head for your list.
 * @return length of the list.
 */
static inline int32_t qb_list_length(struct qb_list_head *head)
{
	struct qb_list_head *item;
	int32_t length = 0;

	qb_list_for_each(item, head)
		length++;

	return length;
}

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_LIST_H_DEFINED */
