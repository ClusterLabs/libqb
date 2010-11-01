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

#include <sys/resource.h>
#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#endif /* HAVE_SYS_EPOLL_H */
#include <sys/poll.h>
#ifndef S_SPLINT_S
#ifdef HAVE_SYS_TIMERFD_H
#include <sys/timerfd.h>
#endif /* HAVE_SYS_TIMERFD_H */
#endif /* S_SPLINT_S */

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbarray.h>
#include <qb/qbloop.h>
#include <qb/qbutil.h>
#include "loop_int.h"
#include "util_int.h"


/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

enum qb_poll_type {
	QB_POLL,
	QB_TIMER,
};


struct qb_poll_entry {
	struct qb_loop_item item;
	struct pollfd ufd;
	qb_loop_poll_dispatch_fn poll_dispatch_fn;
	qb_loop_timer_dispatch_fn timer_dispatch_fn;
	enum qb_loop_priority p;
	int32_t install_pos;
	enum qb_poll_type type;
};

struct qb_poll_source {
	struct qb_loop_source s;
#ifdef HAVE_EPOLL
	struct epoll_event *events;
	int32_t epollfd;
#else
	struct pollfd *ufds;
#endif /* HAVE_EPOLL */
	int32_t poll_entry_count;
	qb_array_t *poll_entries;
	qb_loop_poll_low_fds_event_fn low_fds_event_fn;
	int32_t not_enough_fds;
};


#ifdef HAVE_EPOLL
static int32_t poll_to_epoll_event(int32_t event)
{
	int32_t out = 0;
	if (event & POLLIN) out |= EPOLLIN;
	if (event & POLLOUT) out |= EPOLLOUT;
	if (event & POLLPRI) out |= EPOLLPRI;
	if (event & POLLERR) out |= EPOLLERR;
	if (event & POLLHUP) out |= EPOLLHUP;
	if (event & POLLNVAL) out |= EPOLLERR;
	return out;
}

static int32_t epoll_to_poll_event(int32_t event)
{
	int32_t out = 0;
	if (event & EPOLLIN)   out |= POLLIN;
	if (event & EPOLLOUT)  out |= POLLOUT;
	if (event & EPOLLPRI)  out |= POLLPRI;
	if (event & EPOLLERR)  out |= POLLERR;
	if (event & EPOLLHUP)  out |= POLLHUP;
	return out;
}
#endif /* HAVE_EPOLL */

static void poll_dispatch_and_take_back(struct qb_loop_item * item,
		enum qb_loop_priority p)
{
	struct qb_poll_entry *pe = (struct qb_poll_entry *)item;
	int32_t res;

	if (pe->type == QB_POLL) {
		res = pe->poll_dispatch_fn(pe->ufd.fd, pe->ufd.revents, pe->item.user_data);
		if (res < 0) {
			pe->ufd.fd = -1; /* empty entry */
		}
		pe->ufd.revents = 0;
	} else {
		pe->timer_dispatch_fn(pe->item.user_data);
		if (pe->ufd.fd != -1) {
			close(pe->ufd.fd);
			pe->ufd.fd = -1; /* empty entry */
			pe->ufd.revents = 0;
		}
	}
}

static void poll_fds_usage_check(struct qb_poll_source *s)
{
	struct rlimit lim;
	static int32_t socks_limit = 0;
	int32_t send_event = 0;
	int32_t socks_used = 0;
	int32_t socks_avail = 0;
	struct qb_poll_entry * pe;
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
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		if (pe->ufd.fd != -1) {
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

#ifdef HAVE_EPOLL
#define MAX_EVENTS 100
static int32_t poll_and_add_to_jobs(struct qb_loop_source* src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t new_jobs = 0;
	struct qb_poll_entry * pe;
	struct qb_poll_source * s = (struct qb_poll_source *)src;
	struct epoll_event events[MAX_EVENTS];

	poll_fds_usage_check(s);

 retry_poll:

	res = epoll_wait(s->epollfd, events, MAX_EVENTS, ms_timeout);

	if (errno == EINTR && res == -1) {
		goto retry_poll;
	} else if (res == -1) {
		return -errno;
	}

	for (i = 0; i < res; i++) {
		assert(qb_array_index(s->poll_entries, events[i].data.u32, (void**)&pe) == 0);
		if (pe->ufd.fd == -1) {
			// empty
			continue;
		}
		if (events[i].events == pe->ufd.revents) {
			// entry already in the job queue.
			continue;
		}
		pe->ufd.revents = epoll_to_poll_event(events[i].events);
		qb_list_init(&pe->item.list);
		qb_list_add_tail(&pe->item.list, &s->s.l->level[pe->p].job_head);
		new_jobs++;
	}

	return new_jobs;
}
#else
static int32_t poll_and_add_to_jobs(struct qb_loop_source* src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t new_jobs = 0;
	struct qb_poll_entry * pe;
	struct qb_poll_source * s = (struct qb_poll_source *)src;

	poll_fds_usage_check(s);

	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		memcpy(&s->ufds[i], &pe->ufd, sizeof(struct pollfd));
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
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
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
#endif /* HAVE_EPOLL */

struct qb_loop_source*
qb_loop_poll_create(struct qb_loop *l)
{
	struct qb_poll_source *s = malloc(sizeof(struct qb_poll_source));
	s->s.l = l;
	s->s.dispatch_and_take_back = poll_dispatch_and_take_back;
	s->s.poll = poll_and_add_to_jobs;

	s->poll_entries = qb_array_create(128, sizeof(struct qb_poll_entry));
	s->poll_entry_count = 0;
	s->low_fds_event_fn = NULL;
	s->not_enough_fds = 0;

#ifdef HAVE_EPOLL
	s->epollfd = epoll_create1(EPOLL_CLOEXEC);
	s->events = 0;
#else
	s->ufds = 0;
#endif /* HAVE_EPOLL */

	return (struct qb_loop_source*)s;
}

void qb_loop_poll_destroy(struct qb_loop *l)
{
	struct qb_poll_source * s = (struct qb_poll_source *)l->fd_source;
	qb_array_free(s->poll_entries);
#ifdef HAVE_EPOLL
	close(s->epollfd);
#endif /* HAVE_EPOLL */
	free(s);
}

int32_t qb_loop_poll_low_fds_event_set(
	qb_loop_t *l,
	qb_loop_poll_low_fds_event_fn fn)
{
	struct qb_poll_source * s = (struct qb_poll_source *)l->fd_source;
	s->low_fds_event_fn = fn;

	return 0;
}

static int32_t new_array_position_get(struct qb_poll_source * s)
{
	int32_t found = 0;
	int32_t install_pos;
	int32_t res = 0;
	int32_t new_size = 0;
	struct qb_poll_entry *pe;
#ifdef HAVE_EPOLL
	struct epoll_event *ev;
#else
	struct pollfd *ufds;
#endif /* HAVE_EPOLL */

	for (found = 0, install_pos = 0;
	     install_pos < s->poll_entry_count; install_pos++) {
		assert(qb_array_index(s->poll_entries, install_pos, (void**)&pe) == 0);
		if (pe->ufd.fd == -1) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		/*
		 * Grow pollfd list
		 */
		res = qb_array_grow(s->poll_entries,
				     s->poll_entry_count + 1);
		if (res != 0) {
			return res;
		}

#ifdef HAVE_EPOLL
		new_size = (s->poll_entry_count+ 1) * sizeof(struct epoll_event);
		ev = realloc(s->events, new_size);
		if (ev == NULL) {
			return -ENOMEM;
		}
		s->events = ev;
#else
		new_size = (s->poll_entry_count+ 1) * sizeof(struct pollfd);
		ufds = realloc(s->ufds, new_size);
		if (ufds == NULL) {
			return -ENOMEM;
		}
		s->ufds = ufds;
#endif /* HAVE_EPOLL */

		s->poll_entry_count += 1;
		install_pos = s->poll_entry_count - 1;
	}
	return install_pos;
}


static int32_t _poll_add_(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 struct qb_poll_entry **pe_pt)
{
	struct qb_poll_entry *pe;
	int32_t install_pos;
	int32_t res = 0;
	struct qb_poll_source * s;
#ifdef HAVE_EPOLL
	struct epoll_event *ev;
#endif /* HAVE_EPOLL */

	if (l == NULL) {
		return -EINVAL;
	}

	s = (struct qb_poll_source *)l->fd_source;

	install_pos = new_array_position_get(s);

	assert(qb_array_index(s->poll_entries, install_pos, (void**)&pe) == 0);
	pe->install_pos = install_pos;
	pe->ufd.fd = fd;
	pe->ufd.events = events;
	pe->ufd.revents = 0;
	pe->item.user_data = data;
	pe->item.source = (struct qb_loop_source*)l->fd_source;
	pe->p = p;
#ifdef HAVE_EPOLL
	ev = &s->events[install_pos];
	ev->events = poll_to_epoll_event(events);
	ev->data.u64 = 0; /* valgrind */
	ev->data.u32 = install_pos;
	if (epoll_ctl(s->epollfd, EPOLL_CTL_ADD, fd, ev) == -1) {
		res = -errno;
		qb_util_log(LOG_ERR, "epoll_ctl(add) : %s", strerror(-res));
	}
#endif /* HAVE_EPOLL */
	*pe_pt = pe;

	return (res);
}

int32_t qb_loop_poll_add(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 qb_loop_poll_dispatch_fn dispatch_fn)
{
	struct qb_poll_entry *pe = NULL;
	int32_t res = _poll_add_(l, p, fd, events, data, &pe);
	pe->poll_dispatch_fn = dispatch_fn;
	pe->type = QB_POLL;

	return res;
}

int32_t qb_loop_poll_mod(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 qb_loop_poll_dispatch_fn dispatch_fn)
{
	int32_t i;
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_poll_source * s = (struct qb_poll_source *)l->fd_source;

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		if (pe->ufd.fd != fd) {
			continue;
		}
		pe->poll_dispatch_fn = dispatch_fn;
		pe->item.user_data = data;
		pe->p = p;
		if (pe->ufd.events != events) {
#ifdef HAVE_EPOLL
			s->events[i].events = poll_to_epoll_event(events);
			s->events[i].data.u32 = i;
			if (epoll_ctl(s->epollfd, EPOLL_CTL_MOD, fd, &s->events[i]) == -1) {
				res = -errno;
				qb_util_log(LOG_ERR, "epoll_ctl(mod) : %s", strerror(-res));
			}
#endif /* HAVE_EPOLL */
			pe->ufd.events = events;
		}
		return res;
	}

	return -EBADF;
}

int32_t qb_loop_poll_del(struct qb_loop *l, int32_t fd)
{
	int32_t i;
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_poll_source * s = (struct qb_poll_source *)l->fd_source;

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		if (pe->ufd.fd != fd) {
			continue;
		}
		pe->ufd.fd = -1;
		pe->ufd.events = 0;
		pe->ufd.revents = 0;
#ifdef HAVE_EPOLL
		if (epoll_ctl(s->epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
			res = -errno;
			qb_util_log(LOG_ERR, "epoll_ctl(del) : %s",
				    strerror(-res));
		}
#else
		s->ufds[i].fd = -1;
		s->ufds[i].events = 0;
		s->ufds[i].revents = 0;
#endif /* HAVE_EPOLL */
		return res;
	}

	return -EBADF;
}

#ifdef HAVE_TIMERFD
int32_t qb_loop_timer_msec_duration_to_expire(struct qb_loop_source *timer_source)
{
	return 0;
}

struct qb_loop_source*
qb_loop_timer_create(struct qb_loop *l)
{
	return NULL;
}

void qb_loop_timer_destroy(struct qb_loop *l)
{
}

int32_t qb_loop_timer_add(struct qb_loop *l,
			  enum qb_loop_priority p,
			  int32_t msec_duration,
			  void *data,
			  qb_loop_timer_dispatch_fn timer_fn,
			  qb_loop_timer_handle * timer_handle_out)
{
	struct qb_poll_entry *pe;
	int32_t fd;
	int32_t res;
	struct itimerspec its;

	if (l == NULL || timer_fn == NULL) {
		return -EINVAL;
	}
	if (timer_handle_out == NULL) {
		return -ENOENT;
	}
	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC|TFD_NONBLOCK);
	if (fd == -1) {
		res = -errno;
		qb_util_log(LOG_ERR, "timerfd_create() : %s",
				    strerror(-res));
		return res;
	}

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 0;
	qb_timespec_add_ms(&its.it_value, msec_duration);

	res = timerfd_settime(fd, 0, &its, NULL);
	if (res == -1) {
		res = -errno;
		qb_util_log(LOG_ERR, "timerfd_settime() : %s",
				    strerror(-res));
		goto close_and_return;
	}

	res = _poll_add_(l, p, fd, POLLIN, data, &pe);
	if (res == -1) {
		res = -errno;
		qb_util_log(LOG_ERR, "_poll_add_() : %s",
				    strerror(-res));
		goto close_and_return;
	}
	pe->timer_dispatch_fn = timer_fn;
	pe->type = QB_TIMER;
	*timer_handle_out = pe;

	return res;

 close_and_return:
	close(fd);
	return res;
}

int32_t qb_loop_timer_del(struct qb_loop *l, qb_loop_timer_handle th)
{
	struct qb_poll_entry *pe;
	struct qb_poll_source *s;
#ifdef HAVE_EPOLL
	int32_t res;
#endif /* HAVE_EPOLL */

	if (l == NULL || th == NULL) {
		return -EINVAL;
	}
	pe = (struct qb_poll_entry *)th;
	s = (struct qb_poll_source *)l->fd_source;

	if (pe->type != QB_TIMER) {
		return -EINVAL;
	}
	if (pe->ufd.fd != -1) {
#ifdef HAVE_EPOLL
		if (epoll_ctl(s->epollfd, EPOLL_CTL_DEL, pe->ufd.fd, NULL) == -1) {
			res = -errno;
			qb_util_log(LOG_ERR, "epoll_ctl(del:%d) : %s",
				    pe->ufd.fd, strerror(-res));
			return res;
		}
#else
		s->ufds[pe->install_pos].fd = -1;
		s->ufds[pe->install_pos].events = 0;
		s->ufds[pe->install_pos].revents = 0;
#endif /* HAVE_EPOLL */
		close(pe->ufd.fd);

		pe->ufd.fd = -1;
		pe->ufd.events = 0;
		pe->ufd.revents = 0;
	}

	return 0;
}

uint64_t qb_loop_timer_expire_time_get(struct qb_loop *l, qb_loop_timer_handle th)
{
	struct qb_poll_entry *pe;
	struct itimerspec its;

	if (l == NULL || th == NULL) {
		return 0;
	}
	pe = (struct qb_poll_entry *)th;

	if (timerfd_gettime(pe->ufd.fd, &its) == -1) {
		return 0;
	}
	return (its.it_value.tv_sec * QB_TIME_NS_IN_SEC) + its.it_value.tv_nsec;
}

#endif /* HAVE_TIMERFD */

