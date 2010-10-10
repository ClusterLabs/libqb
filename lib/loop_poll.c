/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include "os_base.h"
#include <sys/poll.h>
#include <sys/resource.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include "loop_int.h"

/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

struct qb_poll_entry {
	struct qb_loop_item item;
	struct pollfd ufd;
	qb_loop_poll_dispatch_fn dispatch_fn;
	enum qb_loop_priority p;
	int32_t install_pos;
};

struct qb_poll_source {
	struct qb_loop_source s;
	struct pollfd *ufds;
	int32_t poll_entry_count;
	struct qb_poll_entry *poll_entries;
	qb_loop_poll_low_fds_event_fn low_fds_event_fn;
	int32_t not_enough_fds;
};

static struct qb_poll_source * my_src;

static void poll_dispatch_and_take_back(struct qb_loop_item * item,
		enum qb_loop_priority p)
{
	struct qb_poll_entry *pe = (struct qb_poll_entry *)item;
	int32_t res;
	int32_t idx = pe->install_pos;

	res = pe->dispatch_fn(pe->ufd.fd, pe->ufd.revents, pe->item.user_data);
	pe = &my_src->poll_entries[idx];
	if (res < 0) {
		pe->ufd.fd = -1; /* empty entry */
	}
	pe->ufd.revents = 0;
}

static void poll_fds_usage_check(struct qb_poll_source *s)
{
	struct rlimit lim;
	static int32_t socks_limit = 0;
	int32_t send_event = 0;
	int32_t socks_used = 0;
	int32_t socks_avail = 0;
	int32_t i;

	if (socks_limit == 0) {
		if (getrlimit(RLIMIT_NOFILE, &lim) == -1) {
			char error_str[100];
			strerror_r(errno, error_str, 100);
			printf("getrlimit: %s\n", error_str);
			return;
		}
		socks_limit = lim.rlim_cur;
		socks_limit -= POLL_FDS_USED_MISC;
		if (socks_limit < 0) {
			socks_limit = 0;
		}
	}

	for (i = 0; i < s->poll_entry_count; i++) {
		if (s->poll_entries[i].ufd.fd != -1) {
			socks_used++;
		}
	}
	socks_avail = socks_limit - socks_used;
	if (socks_avail < 0) {
		socks_avail = 0;
	}
	send_event = 0;
	if (s->not_enough_fds) {
		if (socks_avail > 2) {
			s->not_enough_fds = 0;
			send_event = 1;
		}
	} else {
		if (socks_avail <= 1) {
			s->not_enough_fds = 1;
			send_event = 1;
		}
	}
	if (send_event && s->low_fds_event_fn) {
		s->low_fds_event_fn(s->not_enough_fds,
			socks_avail);
	}
}

static int32_t poll_and_add_to_jobs(struct qb_loop_source* src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t new_jobs = 0;
	struct qb_poll_entry * pe;
	struct qb_poll_source * s = (struct qb_poll_source *)src;

	poll_fds_usage_check(s);

	for (i = 0; i < s->poll_entry_count; i++) {
		memcpy(&s->ufds[i], &s->poll_entries[i].ufd, sizeof(struct pollfd));
	}

 retry_poll:
	res = poll(s->ufds, s->poll_entry_count, ms_timeout);
	if (errno == EINTR && res == -1) {
		goto retry_poll;
	} else if (res == -1) {
		return -errno;
	}

	for (i = 0; i < s->poll_entry_count; i++) {
		if (s->ufds[i].fd == -1 || s->ufds[i].revents == 0) {
			// empty
			continue;
		}
		pe = &s->poll_entries[i];
		if (s->ufds[i].revents == pe->ufd.revents) {
			// entry already in the job queue.
			continue;
		}
		pe->ufd.revents = s->ufds[i].revents;
		qb_list_init(&pe->item.list);
		qb_list_add_tail(&pe->item.list, &s->s.l->level[pe->p].job_head);
		new_jobs++;
	}

	return new_jobs;
}


struct qb_loop_source*
qb_loop_poll_init(struct qb_loop *l)
{
	my_src = malloc(sizeof(struct qb_poll_source));
	my_src->s.l = l;
	my_src->s.dispatch_and_take_back = poll_dispatch_and_take_back;
	my_src->s.poll = poll_and_add_to_jobs;

	my_src->poll_entries = 0;
	my_src->ufds = 0;
	my_src->poll_entry_count = 0;
	my_src->low_fds_event_fn = NULL;
	my_src->not_enough_fds = 0;

	qb_list_init(&my_src->s.list);
	qb_list_add_tail(&my_src->s.list, &l->source_head);
	return (struct qb_loop_source*)my_src;
}

int32_t qb_loop_poll_low_fds_event_set(
	qb_loop_t *l,
	qb_loop_poll_low_fds_event_fn fn)
{
	my_src->low_fds_event_fn = fn;

	return 0;
}


int32_t qb_loop_poll_add(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 qb_loop_poll_dispatch_fn dispatch_fn)
{
	struct qb_poll_entry *poll_entries;
	struct qb_poll_entry *pe;
	struct pollfd *ufds;
	int32_t found = 0;
	int32_t install_pos;
	int32_t res = 0;
	int32_t new_size = 0;

	for (found = 0, install_pos = 0;
	     install_pos < my_src->poll_entry_count; install_pos++) {
		if (my_src->poll_entries[install_pos].ufd.fd == -1) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		/*
		 * Grow pollfd list
		 */
		new_size = (my_src->poll_entry_count + 1) * sizeof(struct qb_poll_entry);
		poll_entries = realloc(my_src->poll_entries, new_size);
		if (poll_entries == NULL) {
			return -ENOMEM;
		}
		my_src->poll_entries = poll_entries;

		new_size = (my_src->poll_entry_count+ 1) * sizeof(struct pollfd);
		ufds = realloc(my_src->ufds, new_size);
		if (ufds == NULL) {
			return -ENOMEM;
		}
		my_src->ufds = ufds;

		my_src->poll_entry_count += 1;
		install_pos = my_src->poll_entry_count - 1;
	}

	/*
	 * Install new dispatch handler
	 */
	pe = &my_src->poll_entries[install_pos];
	pe->install_pos = install_pos;
	pe->ufd.fd = fd;
	pe->ufd.events = events;
	pe->ufd.revents = 0;
	pe->dispatch_fn = dispatch_fn;
	pe->item.user_data = data;
	pe->item.source = (struct qb_loop_source*)my_src;
	pe->p = p;

	return (res);
}

int32_t qb_loop_poll_mod(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 qb_loop_poll_dispatch_fn dispatch_fn)
{
	int32_t i;
	struct qb_poll_entry *pe;

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < my_src->poll_entry_count; i++) {
		pe = &my_src->poll_entries[i];
		if (pe->ufd.fd == fd) {
			pe->ufd.events = events;
			pe->dispatch_fn = dispatch_fn;
			pe->p = p;
			return 0;
		}
	}

	return -EBADF;
}

int32_t qb_loop_poll_del(struct qb_loop *l, int32_t fd)
{
	int32_t i;
	struct qb_poll_entry *pe;

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < my_src->poll_entry_count; i++) {
		pe = &my_src->poll_entries[i];
		if (pe->ufd.fd == fd) {
			my_src->ufds[i].fd = -1;
			my_src->ufds[i].events = 0;
			my_src->ufds[i].revents = 0;
			pe->ufd.fd = -1;
			pe->ufd.events = 0;
			pe->ufd.revents = 0;
			return 0;
		}
	}

	return -EBADF;
}


