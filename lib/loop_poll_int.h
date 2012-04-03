/*
 * Copyright (C) 2012 Red Hat, Inc.
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
#ifndef QB_LOOP_POLL_INT_DEFINED
#define QB_LOOP_POLL_INT_DEFINED

#include "os_base.h"

#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif /* HAVE_SYS_POLL_H */

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbarray.h>
#include <qb/qbutil.h>

#include "loop_int.h"
#include "util_int.h"

struct qb_poll_entry;

typedef int32_t(*qb_poll_add_to_jobs_fn) (struct qb_loop * l,
					  struct qb_poll_entry * pe);

struct qb_poll_entry {
	struct qb_loop_item item;
	qb_loop_poll_dispatch_fn poll_dispatch_fn;
	enum qb_loop_priority p;
	uint32_t install_pos;
	struct pollfd ufd;
	qb_poll_add_to_jobs_fn add_to_jobs;
	uint32_t runs;
	enum qb_poll_entry_state state;
	uint32_t check;
};

struct qb_poll_source;

struct qb_loop_driver {
	int32_t (*poll)(struct qb_loop_source* s, int32_t ms_timeout);
	void (*fini)(struct qb_poll_source *s);
	int32_t (*add)(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events);
	int32_t (*mod)(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events);
	int32_t (*del)(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t arr_index);
};

struct qb_poll_source {
	struct qb_loop_source s;
	int32_t poll_entry_count;
	qb_array_t *poll_entries;
	qb_loop_poll_low_fds_event_fn low_fds_event_fn;
	int32_t not_enough_fds;
#if defined(HAVE_EPOLL) || defined(HAVE_KQUEUE)
	int32_t epollfd;
#else
	struct pollfd *ufds;
#endif /* HAVE_EPOLL */
	struct qb_loop_driver driver;
};

void
qb_poll_fds_usage_check_(struct qb_poll_source *s);

int32_t
qb_epoll_init(struct qb_poll_source *s);

int32_t
qb_poll_init(struct qb_poll_source *s);

int32_t
qb_kqueue_init(struct qb_poll_source *s);

#endif /* QB_LOOP_POLL_INT_DEFINED */
