/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef QB_LIST_H_DEFINED
#define QB_LIST_H_DEFINED

struct qb_list_head {
	struct qb_list_head *next;
	struct qb_list_head *prev;
};

#define QB_DECLARE_LIST_INIT(name) \
    struct qb_list_head name = { &(name), &(name) }

static void inline qb_list_init (struct qb_list_head *head)
{
	head->next = head;
	head->prev = head;
}

static void inline qb_list_add (struct qb_list_head *element, struct qb_list_head *head)
{
	head->next->prev = element;
	element->next = head->next;
	element->prev = head;
	head->next = element;
}
static void inline qb_list_add_tail (struct qb_list_head *element, struct qb_list_head *head)
{
	head->prev->next = element;
	element->next = head;
	element->prev = head->prev;
	head->prev = element;
}
static void inline qb_list_del (struct qb_list_head *_remove)
{
	_remove->next->prev = _remove->prev;
	_remove->prev->next = _remove->next;
#ifdef DEBUG
	_remove->next = (struct qb_list_head *)0xdeadb33f;
	_remove->prev = (struct qb_list_head *)0xdeadb33f;
#endif
}

#define qb_list_entry(ptr,type,member)\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

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

#endif /* QB_LIST_H_DEFINED */

