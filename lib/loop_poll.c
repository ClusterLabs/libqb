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
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbarray.h>
#include <qb/qbloop.h>
#include <qb/qbutil.h>
#include "loop_int.h"
#include "util_int.h"

/*
 * Define this to log slow (>10ms) jobs.
 */
#undef DEBUG_DISPATCH_TIME

/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

enum qb_poll_type {
	QB_UNKNOWN,
	QB_JOB,
	QB_POLL,
	QB_SIGNAL,
	QB_TIMER,
};


struct qb_poll_entry;

typedef int32_t (*qb_poll_add_to_jobs_fn) (struct qb_loop* l, struct qb_poll_entry* pe);

struct qb_poll_entry {
	struct qb_loop_item item;
	enum qb_poll_type type;
	qb_loop_poll_dispatch_fn poll_dispatch_fn;
	qb_loop_timer_dispatch_fn timer_dispatch_fn;
	enum qb_loop_priority p;
	uint32_t install_pos;
	struct pollfd ufd;
	qb_poll_add_to_jobs_fn add_to_jobs;
	uint32_t runs;
	enum qb_poll_entry_state state;
	uint32_t check;
};

struct qb_poll_source {
	struct qb_loop_source s;
#ifdef HAVE_EPOLL
	int32_t epollfd;
#else
	struct pollfd *ufds;
#endif /* HAVE_EPOLL */
	int32_t poll_entry_count;
	qb_array_t *poll_entries;
	qb_loop_poll_low_fds_event_fn low_fds_event_fn;
	int32_t not_enough_fds;
};

static int32_t _qb_signal_add_to_jobs_(struct qb_loop* l,
				       struct qb_poll_entry* pe);

#ifdef HAVE_EPOLL
static int32_t _poll_to_epoll_event_(int32_t event)
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

static int32_t _epoll_to_poll_event_(int32_t event)
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

static void _poll_entry_check_generate_(struct qb_poll_entry *pe)
{
	int32_t i;

	for (i = 0; i < 200; i++) {
		pe->check = random();

		if (pe->check != 0 && pe->check != 0xffffffff) {
			break;
		}
	}
}

#if defined(HAVE_TIMERFD) || defined(HAVE_EPOLL)
static int32_t _poll_entry_from_handle_(struct qb_poll_source *s,
					uint64_t handle_in,
					struct qb_poll_entry **pe_pt)
{
	int32_t res = 0;
	uint32_t check = ((uint32_t) (((uint64_t) handle_in) >> 32));
	uint32_t handle = handle_in & 0xffffffff;
	struct qb_poll_entry *pe;

	res = qb_array_index(s->poll_entries, handle, (void**)&pe);
	if (res != 0) {
		return res;
	}
	if (pe->check != check) {
		return -EINVAL;
	}
	*pe_pt = pe;
	return 0;
}
#endif /* HAVE_TIMERFD or HAVE_EPOLL */

static void _poll_entry_mark_deleted_(struct qb_poll_entry *pe)
{
	pe->ufd.fd = -1;
	pe->state = QB_POLL_ENTRY_DELETED;
	pe->check = 0;
}

static void _poll_entry_empty_(struct qb_poll_entry *pe)
{
	memset(pe, 0, sizeof(struct qb_poll_entry));
	pe->ufd.fd = -1;
}

static void _poll_dispatch_and_take_back_(struct qb_loop_item * item,
					  enum qb_loop_priority p)
{
	struct qb_poll_entry *pe = (struct qb_poll_entry *)item;
	int32_t res;
#ifdef DEBUG_DISPATCH_TIME
	uint64_t start;
	uint64_t stop;
	int32_t log_warn = QB_FALSE;

	start = qb_util_nano_current_get();
#endif /* DEBUG_DISPATCH_TIME */

	assert(pe->state == QB_POLL_ENTRY_JOBLIST);

	if (pe->type == QB_POLL) {
		res = pe->poll_dispatch_fn(pe->ufd.fd, pe->ufd.revents, pe->item.user_data);
		if (res < 0) {
			_poll_entry_mark_deleted_(pe);
		} else {
			pe->state = QB_POLL_ENTRY_ACTIVE;
			pe->ufd.revents = 0;
		}
	} else if (pe->type == QB_TIMER) {
		_poll_entry_mark_deleted_(pe);
		pe->timer_dispatch_fn(pe->item.user_data);
	} else {
		qb_util_log(LOG_WARNING, "poll entry of unknown type:%d state:%d",
			    pe->type, pe->state);
		return;
	}
	if (pe->state == QB_POLL_ENTRY_ACTIVE) {
#ifdef DEBUG_DISPATCH_TIME
		pe->runs++;
		if ((pe->runs % 50) == 0) {
			log_warn = QB_TRUE;
		}
		stop = qb_util_nano_current_get();
		if ((stop - start) > (10 * QB_TIME_NS_IN_MSEC)) {
			log_warn = QB_TRUE;
		}

		if (log_warn && pe->type == QB_POLL) {
			qb_util_log(LOG_INFO,
				    "[fd:%d] dispatch:%p runs:%d duration:%d ms",
				    pe->ufd.fd, pe->poll_dispatch_fn,
				    pe->runs,
				    (int32_t)((stop - start)/QB_TIME_NS_IN_MSEC));
		} else if (log_warn && pe->type == QB_TIMER) {
			qb_util_log(LOG_INFO,
				    "[timer] dispatch:%p runs:%d duration:%d ms",
				    pe->timer_dispatch_fn,
				    pe->runs,
				    (int32_t)((stop - start)/QB_TIME_NS_IN_MSEC));
		}
#endif /* DEBUG_DISPATCH_TIME */
	}
}

static void _poll_fds_usage_check_(struct qb_poll_source *s)
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
			qb_util_perror(LOG_WARNING, "getrlimit");
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
		if ((pe->state == QB_POLL_ENTRY_ACTIVE ||
		     pe->state == QB_POLL_ENTRY_JOBLIST) &&
		    pe->ufd.fd != -1) {
			socks_used++;
		}
		if (pe->state == QB_POLL_ENTRY_DELETED) {
			_poll_entry_empty_(pe);
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
#define MAX_EVENTS 12
static int32_t _poll_and_add_to_jobs_(struct qb_loop_source* src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t event_count;
	int32_t new_jobs = 0;
	struct qb_poll_entry * pe = NULL;
	struct qb_poll_source * s = (struct qb_poll_source *)src;
	struct epoll_event events[MAX_EVENTS];

	_poll_fds_usage_check_(s);

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
			qb_util_log(LOG_WARNING, "can't find poll entry for new event.");
			continue;
		}
		if (pe->ufd.fd == -1 || pe->state == QB_POLL_ENTRY_DELETED) {
			qb_util_log(LOG_WARNING, "can't post new event to a deleted entry.");
			/*
			 * empty/deleted
			 */
			continue;
		}
		if (events[i].events == pe->ufd.revents ||
		    pe->state == QB_POLL_ENTRY_JOBLIST) {
			/*
			 * entry already in the job queue.
			 */
			continue;
		}
		pe->ufd.revents = _epoll_to_poll_event_(events[i].events);

		new_jobs += pe->add_to_jobs(src->l, pe);
	}

	return new_jobs;
}
#else
static int32_t _poll_and_add_to_jobs_(struct qb_loop_source* src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t new_jobs = 0;
	struct qb_poll_entry * pe;
	struct qb_poll_source * s = (struct qb_poll_source *)src;

	_poll_fds_usage_check_(s);

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
			/*
			 * empty entry
			 */
			continue;
		}
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		if (pe->state != QB_POLL_ENTRY_ACTIVE ||
		    s->ufds[i].revents == pe->ufd.revents) {
			/*
			 * Wrong state to accept an event.
			 */
			continue;
		}
		pe->ufd.revents = s->ufds[i].revents;
		new_jobs += pe->add_to_jobs(src->l, pe);
	}

	return new_jobs;
}
#endif /* HAVE_EPOLL */

struct qb_loop_source*
qb_loop_poll_create(struct qb_loop *l)
{
	struct qb_poll_source *s = malloc(sizeof(struct qb_poll_source));
	s->s.l = l;
	s->s.dispatch_and_take_back = _poll_dispatch_and_take_back_;
	s->s.poll = _poll_and_add_to_jobs_;

	s->poll_entries = qb_array_create(128, sizeof(struct qb_poll_entry));
	s->poll_entry_count = 0;
	s->low_fds_event_fn = NULL;
	s->not_enough_fds = 0;

#ifdef HAVE_EPOLL
	s->epollfd = epoll_create1(EPOLL_CLOEXEC);
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
	if (s->epollfd != -1) {
		close(s->epollfd);
		s->epollfd = -1;
	}
#endif /* HAVE_EPOLL */
	free(s);
}

int32_t qb_loop_poll_low_fds_event_set(struct qb_loop *l,
				       qb_loop_poll_low_fds_event_fn fn)
{
	struct qb_poll_source * s = (struct qb_poll_source *)l->fd_source;
	s->low_fds_event_fn = fn;

	return 0;
}

static int32_t _get_empty_array_position_(struct qb_poll_source * s)
{
	int32_t found = 0;
	uint32_t install_pos;
	int32_t res = 0;
	struct qb_poll_entry *pe;
#ifndef HAVE_EPOLL
	struct pollfd *ufds;
	int32_t new_size = 0;
#endif /* HAVE_EPOLL */

	for (found = 0, install_pos = 0;
	     install_pos < s->poll_entry_count; install_pos++) {
		assert(qb_array_index(s->poll_entries, install_pos, (void**)&pe) == 0);
		if (pe->state == QB_POLL_ENTRY_EMPTY) {
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

#ifndef HAVE_EPOLL
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
	uint32_t install_pos;
	int32_t res = 0;
	struct qb_poll_source * s;
#ifdef HAVE_EPOLL
	struct epoll_event ev;
#endif /* HAVE_EPOLL */

	if (l == NULL) {
		return -EINVAL;
	}

	s = (struct qb_poll_source *)l->fd_source;

	install_pos = _get_empty_array_position_(s);

	assert(qb_array_index(s->poll_entries, install_pos, (void**)&pe) == 0);
	pe->state = QB_POLL_ENTRY_ACTIVE;
	pe->install_pos = install_pos;
	_poll_entry_check_generate_(pe);
	pe->ufd.fd = fd;
	pe->ufd.events = events;
	pe->ufd.revents = 0;
	pe->item.user_data = data;
	pe->item.source = (struct qb_loop_source*)l->fd_source;
	pe->p = p;
	pe->runs = 0;
#ifdef HAVE_EPOLL
	ev.events = _poll_to_epoll_event_(events);
	ev.data.u64 = (((uint64_t) (pe->check)) << 32) | pe->install_pos;
	if (epoll_ctl(s->epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "epoll_ctl(add)");
	}
#endif /* HAVE_EPOLL */
	*pe_pt = pe;

	return (res);
}

static int32_t _qb_poll_add_to_jobs_(struct qb_loop* l, struct qb_poll_entry* pe)
{
	assert(pe->type == QB_POLL);
	qb_loop_level_item_add(&l->level[pe->p], &pe->item);
	pe->state = QB_POLL_ENTRY_JOBLIST;
	return 1;
}

int32_t qb_loop_poll_add(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 qb_loop_poll_dispatch_fn dispatch_fn)
{
	struct qb_poll_entry *pe = NULL;
	int32_t size;
	int32_t new_size;
	int32_t res;

	size = ((struct qb_poll_source *)l->fd_source)->poll_entry_count;
	res = _poll_add_(l, p, fd, events, data, &pe);
	new_size = ((struct qb_poll_source *)l->fd_source)->poll_entry_count;

	pe->poll_dispatch_fn = dispatch_fn;
	pe->type = QB_POLL;
	pe->add_to_jobs = _qb_poll_add_to_jobs_;

	if (new_size > size) {
		qb_util_log(LOG_TRACE,
			    "grown poll array to %d for FD %d",
			    new_size, fd);
	}

	return res;
}

int32_t qb_loop_poll_mod(struct qb_loop *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 qb_loop_poll_dispatch_fn dispatch_fn)
{
	uint32_t i;
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_poll_source * s = (struct qb_poll_source *)l->fd_source;
#ifdef HAVE_EPOLL
	struct epoll_event ev;
#endif /* HAVE_EPOLL */

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		if (pe->ufd.fd != fd) {
			continue;
		}
		if (pe->state == QB_POLL_ENTRY_DELETED || pe->check == 0) {
			qb_util_log(LOG_ERR,
				    "poll_mod : can't modify entry already deleted");
			return -EBADF;
		}
		pe->poll_dispatch_fn = dispatch_fn;
		pe->item.user_data = data;
		pe->p = p;
		if (pe->ufd.events != events) {
#ifdef HAVE_EPOLL
			ev.events = _poll_to_epoll_event_(events);
			ev.data.u64 = (((uint64_t) (pe->check)) << 32) | i;
			if (epoll_ctl(s->epollfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
				res = -errno;
				qb_util_perror(LOG_ERR, "epoll_ctl(mod)");
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

	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void**)&pe) == 0);
		if (pe->ufd.fd != fd || pe->type != QB_POLL) {
			continue;
		}
		if (pe->state == QB_POLL_ENTRY_DELETED ||
		    pe->state == QB_POLL_ENTRY_EMPTY) {
			return 0;
		}
		if (pe->state == QB_POLL_ENTRY_JOBLIST) {
			qb_loop_level_item_del(&l->level[pe->p], &pe->item);
		}
#ifdef HAVE_EPOLL
		if (epoll_ctl(s->epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
			res = -errno;
			qb_util_perror(LOG_WARNING, "epoll_ctl(del)");
		}
#else
		s->ufds[i].fd = -1;
		s->ufds[i].events = 0;
		s->ufds[i].revents = 0;
#endif /* HAVE_EPOLL */
		_poll_entry_mark_deleted_(pe);
		return res;
	}

	return -EBADF;
}

#ifdef HAVE_TIMERFD
static int32_t _qb_timer_add_to_jobs_(struct qb_loop* l, struct qb_poll_entry* pe)
{
	uint64_t expired = 0;
	ssize_t bytes = 0;

	assert(pe->type == QB_TIMER);
	if (pe->ufd.revents == POLLIN) {
		bytes = read(pe->ufd.fd, &expired, sizeof(expired));
		if (bytes != sizeof(expired)) {
			qb_util_perror(LOG_WARNING,
				"couldn't read from timer fd %zd", bytes);
		}
		qb_loop_level_item_add(&l->level[pe->p], &pe->item);
	} else {
		qb_util_log(LOG_ERR, "timer revents: %d expected %d",
			    pe->ufd.revents, POLLIN);
	}
	close(pe->ufd.fd);
	pe->ufd.fd = -1;
	pe->state = QB_POLL_ENTRY_JOBLIST;
	return 1;
}

int32_t qb_loop_timer_msec_duration_to_expire(struct qb_loop_source *timer_source)
{
	return -1;
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
			  uint64_t nsec_duration,
			  void *data,
			  qb_loop_timer_dispatch_fn timer_fn,
			  qb_loop_timer_handle * timer_handle_out)
{
	struct qb_poll_entry *pe;
	int32_t fd;
	int32_t res;
	int32_t size, new_size;
	struct itimerspec its;

	if (l == NULL || timer_fn == NULL) {
		qb_util_log(LOG_ERR,
			    "can't add a timer with either (l == NULL || timer_fn == NULL)");
		return -EINVAL;
	}
	if (timer_handle_out == NULL) {
		qb_util_log(LOG_ERR, "can't add a timer with (timer_handle_out == NULL)");
		return -ENOENT;
	}
	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC|TFD_NONBLOCK);
	if (fd == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "failed to create timer");
		return res;
	}

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = nsec_duration / QB_TIME_NS_IN_SEC;
	its.it_value.tv_nsec = nsec_duration % QB_TIME_NS_IN_SEC;

	res = timerfd_settime(fd, 0, &its, NULL);
	if (res == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "failed to set time on timer");
		goto close_and_return;
	}

	size = ((struct qb_poll_source *)l->fd_source)->poll_entry_count;
	res = _poll_add_(l, p, fd, POLLIN, data, &pe);
	if (res != 0) {
		goto close_and_return;
	}
	new_size = ((struct qb_poll_source *)l->fd_source)->poll_entry_count;
	if (new_size > size) {
		qb_util_log(LOG_TRACE, "grown poll array to %d for timer %d",
			    new_size, fd);
	}
	pe->timer_dispatch_fn = timer_fn;
	pe->type = QB_TIMER;
	pe->add_to_jobs = _qb_timer_add_to_jobs_;

	*timer_handle_out = (((uint64_t) (pe->check)) << 32) | pe->install_pos;

	return res;

 close_and_return:
	close(fd);
	return res;
}

int32_t qb_loop_timer_del(struct qb_loop *l, qb_loop_timer_handle th)
{
	struct qb_poll_entry *pe;
	struct qb_poll_source *s;
	int32_t res;

	if (l == NULL || th == 0) {
		return -EINVAL;
	}
	s = (struct qb_poll_source *)l->fd_source;
	res = _poll_entry_from_handle_(s, th, &pe);
	if (res != 0) {
		return res;
	}

	if (pe->type != QB_TIMER) {
		qb_util_log(LOG_WARNING,
			    "trying to delete timer but handle points to type:%d",
			    pe->type);
		return -EINVAL;
	}
	if (pe->state == QB_POLL_ENTRY_DELETED) {
		return 0;
	}
	if (pe->state == QB_POLL_ENTRY_JOBLIST) {
		qb_loop_level_item_del(&l->level[pe->p], &pe->item);
		qb_util_log(LOG_DEBUG, "trying to delete timer in JOBLIST");
	}
	if (pe->state != QB_POLL_ENTRY_ACTIVE &&
	    pe->state != QB_POLL_ENTRY_JOBLIST) {
		qb_util_log(LOG_WARNING, "trying to delete timer with state:%d",
			    pe->state);
		return -EINVAL;
	}
	if (pe->ufd.fd != -1) {
#ifdef HAVE_EPOLL
		if (epoll_ctl(s->epollfd, EPOLL_CTL_DEL, pe->ufd.fd, NULL) == -1) {
			res = -errno;
			qb_util_perror(LOG_WARNING, "epoll_ctl(del:%d)", pe->ufd.fd);
		}
#else
		s->ufds[pe->install_pos].fd = -1;
		s->ufds[pe->install_pos].events = 0;
		s->ufds[pe->install_pos].revents = 0;
#endif /* HAVE_EPOLL */
		close(pe->ufd.fd);
	}
	_poll_entry_mark_deleted_(pe);

	return 0;
}

uint64_t qb_loop_timer_expire_time_get(struct qb_loop *l, qb_loop_timer_handle th)
{
	struct qb_poll_entry *pe;
	struct qb_poll_source *s;
	int32_t res = 0;
	struct itimerspec its;

	if (l == NULL || th == 0) {
		return 0;
	}
	s = (struct qb_poll_source *)l->fd_source;
	res = _poll_entry_from_handle_(s, th, &pe);
	if (res != 0) {
		return res;
	}

	if (timerfd_gettime(pe->ufd.fd, &its) == -1) {
		return 0;
	}
	return (its.it_value.tv_sec * QB_TIME_NS_IN_SEC) + its.it_value.tv_nsec;
}

#endif /* HAVE_TIMERFD */


static int32_t pipe_fds[2] = {-1, -1};

struct qb_signal_source {
	struct qb_loop_source s;
	struct qb_list_head sig_head;
	sigset_t signal_superset;
};

struct qb_loop_sig {
	struct qb_loop_item item;
	int32_t signal;
	enum qb_loop_priority p;
	qb_loop_signal_dispatch_fn dispatch_fn;
	struct qb_loop_sig *cloned_from;
};

static void _handle_real_signal_(int signal_num, siginfo_t * si, void *context)
{
	int32_t sig = signal_num;
	int32_t res = 0;

	if (pipe_fds[1] > 0) {
 try_again:
		res = write(pipe_fds[1], &sig, sizeof(int32_t));
		if (res == -1 && errno == EAGAIN) {
			goto try_again;
		} else if (res != sizeof(int32_t)) {
			qb_util_log(LOG_ERR, "failed to write signal to pipe [%d]",
				    res);
		}
	}
}

static void _signal_dispatch_and_take_back_(struct qb_loop_item * item,
					    enum qb_loop_priority p)
{
	struct qb_loop_sig *sig = (struct qb_loop_sig *)item;
	int32_t res;

	res = sig->dispatch_fn(sig->signal, sig->item.user_data);
	if (res != 0) {
		qb_list_del(&sig->cloned_from->item.list);
		free(sig->cloned_from);
	}
	free(sig);
}


struct qb_loop_source *
qb_loop_signals_create(struct qb_loop *l)
{
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_signal_source *s = calloc(1, sizeof(struct qb_signal_source));

	s->s.l = l;
	s->s.dispatch_and_take_back = _signal_dispatch_and_take_back_;
	s->s.poll = NULL;
	qb_list_init(&s->sig_head);
	sigemptyset(&s->signal_superset);

	if (pipe_fds[0] < 0) {
		res = pipe(pipe_fds);
		if (res == -1) {
			res = -errno;
			qb_util_perror(LOG_ERR, "Can't light pipe");
			goto error_exit;
		}
		(void)qb_util_fd_nonblock_cloexec_set(pipe_fds[0]);
		(void)qb_util_fd_nonblock_cloexec_set(pipe_fds[1]);

		res = _poll_add_(l, QB_LOOP_HIGH,
				 pipe_fds[0], POLLIN,
				 NULL, &pe);
		if (res == 0) {
			pe->poll_dispatch_fn = NULL;
			pe->type = QB_SIGNAL;
			pe->add_to_jobs = _qb_signal_add_to_jobs_;
		} else {
			qb_util_perror(LOG_ERR, "Can't smoke pipe");
			goto error_exit;
		}
	}

	return (struct qb_loop_source *)s;

 error_exit:
	free(s);
	errno = -res;
	return NULL;
}

void qb_loop_signals_destroy(struct qb_loop *l)
{
	close(pipe_fds[0]);
	pipe_fds[0] = -1;
	close(pipe_fds[1]);
	pipe_fds[1] = -1;
	free(l->signal_source);
}

static int32_t _qb_signal_add_to_jobs_(struct qb_loop* l,
				       struct qb_poll_entry* pe)
{
	struct qb_signal_source *s = (struct qb_signal_source *)l->signal_source;
	struct qb_list_head *list;
	struct qb_loop_sig *sig;
	struct qb_loop_item *item;
	struct qb_loop_sig *new_sig_job;
	int32_t the_signal;
	ssize_t res;
	int32_t jobs_added = 0;

	res = read(pipe_fds[0], &the_signal, sizeof(int32_t));
	if (res != sizeof(int32_t)) {
		res = -errno;
		qb_util_perror(LOG_ERR, "failed to read pipe");
		return 0;
	}
	pe->ufd.revents = 0;

	qb_list_for_each(list, &s->sig_head) {
		item = qb_list_entry(list, struct qb_loop_item, list);
		sig = (struct qb_loop_sig *)item;
		if (sig->signal == the_signal) {
			new_sig_job = calloc(1, sizeof(struct qb_loop_sig));
			memcpy(new_sig_job, sig, sizeof(struct qb_loop_sig));

			new_sig_job->cloned_from = sig;
			qb_loop_level_item_add(&l->level[pe->p], &new_sig_job->item);
			jobs_added++;
		}
	}
	return jobs_added;
}

static void _adjust_sigactions_(struct qb_signal_source *s)
{
	struct qb_loop_sig *sig;
	struct qb_loop_item *item;
	struct sigaction sa;
	int32_t i;
	int32_t needed;

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = _handle_real_signal_;
	sigemptyset(&s->signal_superset);
	sigemptyset(&sa.sa_mask);

	/* re-set to default */
	for (i = 0; i < 30; i++) {
		needed = QB_FALSE;
		qb_list_for_each_entry(item, &s->sig_head, list) {
			sig = (struct qb_loop_sig *)item;
			if (i == sig->signal) {
				needed = QB_TRUE;
				break;
			}
		}
		if (needed) {
			sigaddset(&s->signal_superset, i);
			sigaction(i, &sa, NULL);
		} else {
			(void)signal(i, SIG_DFL);
		}
	}
}

int32_t qb_loop_signal_add(qb_loop_t *l,
			   enum qb_loop_priority p,
			   int32_t the_sig,
			   void *data,
			   qb_loop_signal_dispatch_fn dispatch_fn,
			   qb_loop_signal_handle *handle)
{
	struct qb_loop_sig *sig;
	struct qb_signal_source *s;

	if (l == NULL || dispatch_fn == NULL) {
		return -EINVAL;
	}
	if (p < QB_LOOP_LOW || p > QB_LOOP_HIGH) {
		return -EINVAL;
	}
	s = (struct qb_signal_source *)l->signal_source;
	sig = calloc(1, sizeof(struct qb_loop_sig));

	sig->dispatch_fn = dispatch_fn;
	sig->p = p;
	sig->signal = the_sig;
	sig->item.user_data = data;
	sig->item.source = l->signal_source;

	qb_list_init(&sig->item.list);
	qb_list_add_tail(&sig->item.list, &s->sig_head);

	if (sigismember(&s->signal_superset, the_sig) != 1) {
		_adjust_sigactions_(s);
	}
	if (handle) {
		*handle = sig;
	}

	return 0;
}

int32_t qb_loop_signal_mod(qb_loop_t *l,
			   enum qb_loop_priority p,
			   int32_t the_sig,
			   void *data,
			   qb_loop_signal_dispatch_fn dispatch_fn,
			   qb_loop_signal_handle handle)
{
	struct qb_signal_source *s;
	struct qb_loop_sig *sig = (struct qb_loop_sig *)handle;

	if (l == NULL || dispatch_fn == NULL || handle == NULL) {
		return -EINVAL;
	}
	if (p < QB_LOOP_LOW || p > QB_LOOP_HIGH) {
		return -EINVAL;
	}
	s = (struct qb_signal_source *)l->signal_source;

	sig->item.user_data = data;
	sig->dispatch_fn = dispatch_fn;
	sig->p = p;

	if (sig->signal != the_sig) {
		sig->signal = the_sig;
		_adjust_sigactions_(s);
	}

	return 0;
}

int32_t qb_loop_signal_del(qb_loop_t *l,
			   qb_loop_signal_handle handle)
{
	struct qb_loop_sig *sig = (struct qb_loop_sig *)handle;

	qb_list_del(&sig->item.list);
	free(sig);
	return 0;
}


