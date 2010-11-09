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


/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

enum qb_poll_type {
	QB_POLL,
	QB_TIMER,
	QB_SIGNAL,
	QB_JOB,
};

struct qb_poll_entry;

typedef int32_t (*qb_poll_add_to_jobs_fn) (struct qb_loop* l, struct qb_poll_entry* pe);

struct qb_poll_entry {
	struct qb_loop_item item;
	enum qb_poll_type type;
	qb_loop_poll_dispatch_fn poll_dispatch_fn;
	qb_loop_timer_dispatch_fn timer_dispatch_fn;
	enum qb_loop_priority p;
	int32_t install_pos;
	struct pollfd ufd;
	qb_poll_add_to_jobs_fn add_to_jobs;
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

		new_jobs += pe->add_to_jobs(src->l, pe);
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

	install_pos = _get_empty_array_position_(s);

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

static int32_t _qb_poll_add_to_jobs_(struct qb_loop* l, struct qb_poll_entry* pe)
{
	qb_list_init(&pe->item.list);
	qb_list_add_tail(&pe->item.list, &l->level[pe->p].job_head);
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
	int32_t res = _poll_add_(l, p, fd, events, data, &pe);
	pe->poll_dispatch_fn = dispatch_fn;
	pe->type = QB_POLL;
	pe->add_to_jobs = _qb_poll_add_to_jobs_;

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
	pe->add_to_jobs = _qb_poll_add_to_jobs_;
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
		if (pe->ufd.fd != -1) {
			close(pe->ufd.fd);
		}

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
	if (pipe_fds[1] > 0) {
		(void)write(pipe_fds[1], &sig, sizeof(int32_t));
	}
}

static void signal_dispatch_and_take_back(struct qb_loop_item * item,
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
	struct qb_signal_source *s = calloc(1, sizeof(struct qb_signal_source));
	s->s.l = l;
	s->s.dispatch_and_take_back = signal_dispatch_and_take_back;
	s->s.poll = NULL;
	qb_list_init(&s->sig_head);
	sigemptyset(&s->signal_superset);

	return (struct qb_loop_source *)s;
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
		qb_util_log(LOG_ERR, "failed to read pipe: %s", strerror(errno));
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
			qb_list_init(&new_sig_job->item.list);
			qb_list_add_tail(&new_sig_job->item.list,
					 &l->level[pe->p].job_head);
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
	struct qb_poll_entry *pe;
	struct qb_signal_source *s;
	int32_t res = 0;

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

	if (pipe_fds[0] < 0) {
		pipe(pipe_fds);
		res = _poll_add_(l, QB_LOOP_HIGH,
				 pipe_fds[0], POLLIN,
				 NULL, &pe);
		if (res == 0) {
			pe->poll_dispatch_fn = NULL;
			pe->type = QB_SIGNAL;
			pe->add_to_jobs = _qb_signal_add_to_jobs_;
		} else {
			qb_util_log(LOG_ERR,
				    "failed to setup pipe: %s",
				    strerror(-res));
		}
	}

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


