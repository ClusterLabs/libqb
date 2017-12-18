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

#include "os_base.h"
#include "loop_poll_int.h"

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#ifndef epoll_create1
int epoll_create1(int flags);
#endif /* workaround a set of sparc and alpha broken headers */
#endif /* HAVE_SYS_EPOLL_H */

#define MAX_EVENTS 12

static int32_t
_poll_to_epoll_event_(int32_t event)
{
	int32_t out = 0;
	if (event & POLLIN)
		out |= EPOLLIN;
	if (event & POLLOUT)
		out |= EPOLLOUT;
	if (event & POLLPRI)
		out |= EPOLLPRI;
	if (event & POLLERR)
		out |= EPOLLERR;
	if (event & POLLHUP)
		out |= EPOLLHUP;
	if (event & POLLNVAL)
		out |= EPOLLERR;
	return out;
}

static int32_t
_epoll_to_poll_event_(int32_t event)
{
	int32_t out = 0;
	if (event & EPOLLIN)
		out |= POLLIN;
	if (event & EPOLLOUT)
		out |= POLLOUT;
	if (event & EPOLLPRI)
		out |= POLLPRI;
	if (event & EPOLLERR)
		out |= POLLERR;
	if (event & EPOLLHUP)
		out |= POLLHUP;
	return out;
}

static void
_fini(struct qb_poll_source *s)
{
	if (s->epollfd != -1) {
		close(s->epollfd);
		s->epollfd = -1;
	}
}

static int32_t
_add(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events)
{
	struct epoll_event ev;
	int32_t res = 0;

	ev.events = _poll_to_epoll_event_(events);
	ev.data.u64 = (((uint64_t) (pe->check)) << 32) | pe->install_pos;
	if (epoll_ctl(s->epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "epoll_ctl(add)");
	}
	return res;
}


static int32_t
_mod(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events)
{
	struct epoll_event ev;
	int32_t res = 0;

	ev.events = _poll_to_epoll_event_(events);
	ev.data.u64 = (((uint64_t) (pe->check)) << 32) | pe->install_pos;
	if (epoll_ctl(s->epollfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
		res = -errno;
		qb_util_perror(LOG_DEBUG, "epoll_ctl(mod)");
	}
	return res;
}

static int32_t
_del(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t arr_index)
{
	int32_t res = 0;

	if (epoll_ctl(s->epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		res = -errno;
		qb_util_perror(LOG_DEBUG, "epoll_ctl(del)");
	}
	return res;
}

static int32_t
_poll_entry_from_handle_(struct qb_poll_source *s,
			 uint64_t handle_in, struct qb_poll_entry **pe_pt)
{
	int32_t res = 0;
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & UINT32_MAX;
	struct qb_poll_entry *pe;

	res = qb_array_index(s->poll_entries, handle, (void **)&pe);
	if (res != 0) {
		return res;
	}
	if (pe->check != check) {
		return -EINVAL;
	}
	*pe_pt = pe;
	return 0;
}

static int32_t
_poll_and_add_to_jobs_(struct qb_loop_source *src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t event_count;
	int32_t new_jobs = 0;
	struct qb_poll_entry *pe = NULL;
	struct qb_poll_source *s = (struct qb_poll_source *)src;
	struct epoll_event events[MAX_EVENTS];

	qb_poll_fds_usage_check_(s);

retry_poll:

	event_count = epoll_wait(s->epollfd, events, MAX_EVENTS, ms_timeout);

	if (errno == EINTR && event_count == -1) {
		goto retry_poll;
	} else if (event_count == -1) {
		return -errno;
	}

	for (i = 0; i < event_count; i++) {
		res = _poll_entry_from_handle_(s, events[i].data.u64, &pe);
		if (res != 0) {
			qb_util_log(LOG_WARNING,
				    "can't find poll entry for new event.");
			usleep(100000);
			continue;
		}
		if (pe->ufd.fd == -1 || pe->state == QB_POLL_ENTRY_DELETED) {
			qb_util_log(LOG_WARNING,
				    "can't post new event to a deleted entry.");
			/*
			 * empty/deleted
			 */
			continue;
		}

		pe->ufd.revents |= _epoll_to_poll_event_(events[i].events);

		if (pe->state != QB_POLL_ENTRY_JOBLIST) {
			new_jobs += pe->add_to_jobs(src->l, pe);
		}
	}

	return new_jobs;
}

int32_t
qb_epoll_init(struct qb_poll_source *s)
{
	s->epollfd = epoll_create1(EPOLL_CLOEXEC);
	if (s->epollfd < 0) {
		return -errno;
	}
	s->driver.fini = _fini;
	s->driver.add = _add;
	s->driver.mod = _mod;
	s->driver.del = _del;
	s->s.poll = _poll_and_add_to_jobs_;
	return 0;
}
