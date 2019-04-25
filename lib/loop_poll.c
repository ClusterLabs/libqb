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

/* due to MinGW/splint emitting "< Location unknown >: Previous use of" */
#if defined(HAVE_SYS_RESOURCE_H) && !defined(S_SPLINT_S)
#include <sys/resource.h>
#endif

#include <signal.h>

#if defined(__DARWIN_NSIG)
#define QB_MAX_NUM_SIGNALS __DARWIN_NSIG
#else
  #if defined(NSIG)
  #define QB_MAX_NUM_SIGNALS NSIG - 1
  #else
  #define QB_MAX_NUM_SIGNALS 31
  #endif
#endif

#include "loop_poll_int.h"

/*
 * Define this to log slow (>10ms) jobs.
 */
#undef DEBUG_DISPATCH_TIME

/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

#ifdef HAVE_EPOLL
#define USE_EPOLL 1
#else
 #ifdef HAVE_KQUEUE
 #define USE_KQUEUE 1
 #else
 #define USE_POLL 1
 #endif /* HAVE_KQUEUE */
#endif /* HAVE_EPOLL */

static int32_t _qb_signal_add_to_jobs_(struct qb_loop *l,
				       struct qb_poll_entry *pe);

static void
_poll_entry_check_generate_(struct qb_poll_entry *pe)
{
	int32_t i;

	for (i = 0; i < 200; i++) {
		pe->check = random();

		if (pe->check != 0 && pe->check != UINT32_MAX) {
			break;
		}
	}
}

static void
_poll_entry_mark_deleted_(struct qb_poll_entry *pe)
{
	pe->ufd.fd = -1;
	pe->state = QB_POLL_ENTRY_DELETED;
	pe->check = 0;
}

static void
_poll_entry_empty_(struct qb_poll_entry *pe)
{
	memset(pe, 0, sizeof(struct qb_poll_entry));
	pe->ufd.fd = -1;
}

static void
_poll_dispatch_and_take_back_(struct qb_loop_item *item,
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
	assert(pe->item.type == QB_LOOP_FD);

	res = pe->poll_dispatch_fn(pe->ufd.fd,
				   pe->ufd.revents,
				   pe->item.user_data);
	if (res < 0) {
		_poll_entry_mark_deleted_(pe);
	} else if (pe->state != QB_POLL_ENTRY_DELETED) {
		pe->state = QB_POLL_ENTRY_ACTIVE;
		pe->ufd.revents = 0;
	}
#ifdef DEBUG_DISPATCH_TIME
	if (pe->state == QB_POLL_ENTRY_ACTIVE) {
		pe->runs++;
		if ((pe->runs % 50) == 0) {
			log_warn = QB_TRUE;
		}
		stop = qb_util_nano_current_get();
		if ((stop - start) > (10 * QB_TIME_NS_IN_MSEC)) {
			log_warn = QB_TRUE;
		}

		if (log_warn && pe->item.type == QB_LOOP_FD) {
			qb_util_log(LOG_INFO,
				    "[fd:%d] dispatch:%p runs:%d duration:%d ms",
				    pe->ufd.fd, pe->poll_dispatch_fn,
				    pe->runs,
				    (int32_t) ((stop -
						start) / QB_TIME_NS_IN_MSEC));
		}
	}
#endif /* DEBUG_DISPATCH_TIME */
}

void
qb_poll_fds_usage_check_(struct qb_poll_source *s)
{
	struct rlimit lim;
	static int32_t socks_limit = 0;
	int32_t send_event = QB_FALSE;
	int32_t socks_used = 0;
	int32_t socks_avail = 0;
	struct qb_poll_entry *pe;
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
		assert(qb_array_index(s->poll_entries, i, (void **)&pe) == 0);
		if ((pe->state == QB_POLL_ENTRY_ACTIVE ||
		     pe->state == QB_POLL_ENTRY_JOBLIST) && pe->ufd.fd != -1) {
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
	send_event = QB_FALSE;
	if (s->not_enough_fds) {
		if (socks_avail > 2) {
			s->not_enough_fds = QB_FALSE;
			send_event = QB_TRUE;
		}
	} else {
		if (socks_avail <= 1) {
			s->not_enough_fds = QB_TRUE;
			send_event = QB_TRUE;
		}
	}
	if (send_event && s->low_fds_event_fn) {
		s->low_fds_event_fn(s->not_enough_fds, socks_avail);
	}
}


struct qb_loop_source *
qb_loop_poll_create(struct qb_loop *l)
{
	struct qb_poll_source *s = malloc(sizeof(struct qb_poll_source));
	if (s == NULL) {
		return NULL;
	}
	s->s.l = l;
	s->s.dispatch_and_take_back = _poll_dispatch_and_take_back_;

	s->poll_entries = qb_array_create_2(16, sizeof(struct qb_poll_entry), 16);
	s->poll_entry_count = 0;
	s->low_fds_event_fn = NULL;
	s->not_enough_fds = QB_FALSE;

#ifdef USE_EPOLL
	(void)qb_epoll_init(s);
#endif
#ifdef USE_KQUEUE
	(void)qb_kqueue_init(s);
#endif
#ifdef USE_POLL
	(void)qb_poll_init(s);
#endif /* USE_POLL */

	return (struct qb_loop_source *)s;
}

void
qb_loop_poll_destroy(struct qb_loop *l)
{
	struct qb_poll_source *s = (struct qb_poll_source *)l->fd_source;
	qb_array_free(s->poll_entries);

	s->driver.fini(s);

	free(s);
}

int32_t
qb_loop_poll_low_fds_event_set(struct qb_loop *l,
			       qb_loop_poll_low_fds_event_fn fn)
{
	struct qb_poll_source *s = (struct qb_poll_source *)l->fd_source;
	s->low_fds_event_fn = fn;

	return 0;
}

static int32_t
_get_empty_array_position_(struct qb_poll_source *s)
{
	int32_t found = QB_FALSE;
	uint32_t install_pos;
	int32_t res = 0;
	struct qb_poll_entry *pe;

	for (install_pos = 0;
	     install_pos < s->poll_entry_count; install_pos++) {
		assert(qb_array_index
		       (s->poll_entries, install_pos, (void **)&pe) == 0);
		if (pe->state == QB_POLL_ENTRY_EMPTY) {
			found = QB_TRUE;
			break;
		}
	}

	if (found == QB_FALSE) {
#ifdef USE_POLL
		struct pollfd *ufds;
		int32_t new_size = (s->poll_entry_count + 1) * sizeof(struct pollfd);
		ufds = realloc(s->ufds, new_size);
		if (ufds == NULL) {
			return -ENOMEM;
		}
		s->ufds = ufds;
#endif /* USE_POLL */
		/*
		 * Grow pollfd list
		 */
		res = qb_array_grow(s->poll_entries, s->poll_entry_count + 1);
		if (res != 0) {
			return res;
		}

		s->poll_entry_count += 1;
		install_pos = s->poll_entry_count - 1;
	}
	return install_pos;
}

static int32_t
_poll_add_(struct qb_loop *l,
	   enum qb_loop_priority p,
	   int32_t fd, int32_t events, void *data, struct qb_poll_entry **pe_pt)
{
	struct qb_poll_entry *pe;
	uint32_t install_pos;
	int32_t res = 0;
	struct qb_poll_source *s;

	if (l == NULL) {
		return -EINVAL;
	}

	s = (struct qb_poll_source *)l->fd_source;

	install_pos = _get_empty_array_position_(s);

	assert(qb_array_index(s->poll_entries, install_pos, (void **)&pe) == 0);
	pe->state = QB_POLL_ENTRY_ACTIVE;
	pe->install_pos = install_pos;
	_poll_entry_check_generate_(pe);
	pe->ufd.fd = fd;
	pe->ufd.events = events;
	pe->ufd.revents = 0;
	pe->item.user_data = data;
	pe->item.source = (struct qb_loop_source *)l->fd_source;
	pe->p = p;
	pe->runs = 0;
	res = s->driver.add(s, pe, fd, events);
	if (res == 0) {
		*pe_pt = pe;
		return 0;
	} else {
		pe->state = QB_POLL_ENTRY_EMPTY;
		return res;
	}
}

static int32_t
_qb_poll_add_to_jobs_(struct qb_loop *l, struct qb_poll_entry *pe)
{
	assert(pe->item.type == QB_LOOP_FD);
	qb_loop_level_item_add(&l->level[pe->p], &pe->item);
	pe->state = QB_POLL_ENTRY_JOBLIST;
	return 1;
}

int32_t
qb_loop_poll_add(struct qb_loop * lp,
		 enum qb_loop_priority p,
		 int32_t fd,
		 int32_t events,
		 void *data, qb_loop_poll_dispatch_fn dispatch_fn)
{
	struct qb_poll_entry *pe = NULL;
	int32_t size;
	int32_t new_size;
	int32_t res;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}

	size = ((struct qb_poll_source *)l->fd_source)->poll_entry_count;
	res = _poll_add_(l, p, fd, events, data, &pe);
	if (res != 0) {
		qb_util_perror(LOG_ERR,
			       "couldn't add poll entryfor FD %d", fd);
		return res;
	}
	new_size = ((struct qb_poll_source *)l->fd_source)->poll_entry_count;

	pe->poll_dispatch_fn = dispatch_fn;
	pe->item.type = QB_LOOP_FD;
	pe->add_to_jobs = _qb_poll_add_to_jobs_;

	if (new_size > size) {
		qb_util_log(LOG_TRACE,
			    "grown poll array to %d for FD %d", new_size, fd);
	}

	return res;
}

int32_t
qb_loop_poll_mod(struct qb_loop * lp,
		 enum qb_loop_priority p,
		 int32_t fd,
		 int32_t events,
		 void *data, qb_loop_poll_dispatch_fn dispatch_fn)
{
	uint32_t i;
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_poll_source *s;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	s = (struct qb_poll_source *)l->fd_source;

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void **)&pe) == 0);
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
			res = s->driver.mod(s, pe, fd, events);
			pe->ufd.events = events;
		}
		return res;
	}

	return -EBADF;
}

int32_t
qb_loop_poll_del(struct qb_loop * lp, int32_t fd)
{
	int32_t i;
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_poll_source *s;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	s = (struct qb_poll_source *)l->fd_source;
	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void **)&pe) == 0);
		if (pe->ufd.fd != fd || pe->item.type != QB_LOOP_FD) {
			continue;
		}
		if (pe->state == QB_POLL_ENTRY_DELETED ||
		    pe->state == QB_POLL_ENTRY_EMPTY) {
			return 0;
		}
		if (pe->state == QB_POLL_ENTRY_JOBLIST) {
			qb_loop_level_item_del(&l->level[pe->p], &pe->item);
		}
		res = s->driver.del(s, pe, fd, i);
		_poll_entry_mark_deleted_(pe);
		return res;
	}

	return -EBADF;
}

static int32_t pipe_fds[2] = { -1, -1 };

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

static void
_handle_real_signal_(int signal_num, siginfo_t * si, void *context)
{
	int32_t sig = signal_num;
	int32_t res = 0;

	if (pipe_fds[1] > 0) {
try_again:
		res = write(pipe_fds[1], &sig, sizeof(int32_t));
		if (res == -1 && errno == EAGAIN) {
			goto try_again;
		} else if (res != sizeof(int32_t)) {
			qb_util_log(LOG_ERR,
				    "failed to write signal to pipe [%d]", res);
		}
	}
	qb_util_log(LOG_TRACE, "got real signal [%d] sent to pipe", sig);
}

static void
_signal_dispatch_and_take_back_(struct qb_loop_item *item,
				enum qb_loop_priority p)
{
	struct qb_loop_sig *sig = (struct qb_loop_sig *)item;
	int32_t res;

	res = sig->dispatch_fn(sig->signal, sig->item.user_data);
	if (res != 0) {
		(void)qb_loop_signal_del(sig->cloned_from->item.source->l,
					 sig->cloned_from);
	}
	free(sig);
}

struct qb_loop_source *
qb_loop_signals_create(struct qb_loop *l)
{
	int32_t res = 0;
	struct qb_poll_entry *pe;
	struct qb_signal_source *s = calloc(1, sizeof(struct qb_signal_source));

	if (s == NULL) {
		return NULL;
	}
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
		(void)qb_sys_fd_nonblock_cloexec_set(pipe_fds[0]);
		(void)qb_sys_fd_nonblock_cloexec_set(pipe_fds[1]);

		res = _poll_add_(l, QB_LOOP_HIGH,
				 pipe_fds[0], POLLIN, NULL, &pe);
		if (res == 0) {
			pe->poll_dispatch_fn = NULL;
			pe->item.type = QB_LOOP_SIG;
			pe->add_to_jobs = _qb_signal_add_to_jobs_;
		} else {
			qb_util_perror(LOG_ERR, "Can't smoke pipe");
			goto error_exit;
		}
	}

	return (struct qb_loop_source *)s;

error_exit:
	errno = -res;
	free(s);
	if (pipe_fds[0] >= 0) {
		close(pipe_fds[0]);
	}
	if (pipe_fds[1] >= 0) {
		close(pipe_fds[1]);
	}
	return NULL;
}

void
qb_loop_signals_destroy(struct qb_loop *l)
{
	struct qb_signal_source *s =
	    (struct qb_signal_source *)l->signal_source;
	struct qb_list_head *list;
	struct qb_list_head *n;
	struct qb_loop_item *item;

	close(pipe_fds[0]);
	pipe_fds[0] = -1;
	close(pipe_fds[1]);
	pipe_fds[1] = -1;

	qb_list_for_each_safe(list, n, &s->sig_head) {
		item = qb_list_entry(list, struct qb_loop_item, list);
		qb_list_del(&item->list);
		free(item);
	}

	free(l->signal_source);
}

static int32_t
_qb_signal_add_to_jobs_(struct qb_loop *l, struct qb_poll_entry *pe)
{
	struct qb_signal_source *s =
	    (struct qb_signal_source *)l->signal_source;
	struct qb_list_head *list;
	struct qb_loop_sig *sig;
	struct qb_loop_item *item;
	struct qb_loop_sig *new_sig_job;
	int32_t the_signal;
	ssize_t res;
	int32_t jobs_added = 0;

	res = read(pipe_fds[0], &the_signal, sizeof(int32_t));
	if (res != sizeof(int32_t)) {
		qb_util_perror(LOG_WARNING, "failed to read pipe");
		return 0;
	}
	pe->ufd.revents = 0;

	qb_list_for_each(list, &s->sig_head) {
		item = qb_list_entry(list, struct qb_loop_item, list);
		sig = (struct qb_loop_sig *)item;
		if (sig->signal == the_signal) {
			new_sig_job = calloc(1, sizeof(struct qb_loop_sig));
			if (new_sig_job == NULL) {
				return jobs_added;
			}
			memcpy(new_sig_job, sig, sizeof(struct qb_loop_sig));

			qb_util_log(LOG_TRACE,
				    "adding signal [%d] to job queue %p",
				    the_signal, sig);

			new_sig_job->cloned_from = sig;
			qb_loop_level_item_add(&l->level[sig->p],
					       &new_sig_job->item);
			jobs_added++;
		}
	}
	return jobs_added;
}

static void
_adjust_sigactions_(struct qb_signal_source *s)
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
	for (i = 0; i < QB_MAX_NUM_SIGNALS; i++) {
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
		}
	}
}

int32_t
qb_loop_signal_add(qb_loop_t * lp,
		   enum qb_loop_priority p,
		   int32_t the_sig,
		   void *data,
		   qb_loop_signal_dispatch_fn dispatch_fn,
		   qb_loop_signal_handle * handle)
{
	struct qb_loop_sig *sig;
	struct qb_signal_source *s;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	if (l == NULL || dispatch_fn == NULL) {
		return -EINVAL;
	}
	if (p < QB_LOOP_LOW || p > QB_LOOP_HIGH) {
		return -EINVAL;
	}
	s = (struct qb_signal_source *)l->signal_source;
	sig = calloc(1, sizeof(struct qb_loop_sig));
	if (sig == NULL) {
		return -errno;
	}

	sig->dispatch_fn = dispatch_fn;
	sig->p = p;
	sig->signal = the_sig;
	sig->item.user_data = data;
	sig->item.source = l->signal_source;
	sig->item.type = QB_LOOP_SIG;

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

int32_t
qb_loop_signal_mod(qb_loop_t * lp,
		   enum qb_loop_priority p,
		   int32_t the_sig,
		   void *data,
		   qb_loop_signal_dispatch_fn dispatch_fn,
		   qb_loop_signal_handle handle)
{
	struct qb_signal_source *s;
	struct qb_loop_sig *sig = (struct qb_loop_sig *)handle;
	struct qb_loop *l = lp;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	if (l == NULL || dispatch_fn == NULL || handle == NULL) {
		return -EINVAL;
	}
	if (p < QB_LOOP_LOW || p > QB_LOOP_HIGH) {
		return -EINVAL;
	}
	s = (struct qb_signal_source *)l->signal_source;

	sig->item.user_data = data;
	sig->item.type = QB_LOOP_SIG;
	sig->dispatch_fn = dispatch_fn;
	sig->p = p;

	if (sig->signal != the_sig) {
		(void)signal(sig->signal, SIG_DFL);
		sig->signal = the_sig;
		_adjust_sigactions_(s);
	}

	return 0;
}

int32_t
qb_loop_signal_del(qb_loop_t * lp, qb_loop_signal_handle handle)
{
	struct qb_signal_source *s;
	struct qb_loop_sig *sig = (struct qb_loop_sig *)handle;
	struct qb_loop_sig *sig_clone;
	struct qb_loop *l = lp;
	struct qb_loop_item *item;

	if (l == NULL) {
		l = qb_loop_default_get();
	}
	if (l == NULL || handle == NULL) {
		return -EINVAL;
	}
	s = (struct qb_signal_source *)l->signal_source;

	qb_list_for_each_entry(item, &l->level[sig->p].wait_head, list) {
		if (item->type != QB_LOOP_SIG) {
			continue;
		}
		sig_clone = (struct qb_loop_sig *)item;
		if (sig_clone->cloned_from == sig) {
			qb_util_log(LOG_TRACE, "deleting sig in WAITLIST");
			qb_list_del(&sig_clone->item.list);
			free(sig_clone);
			break;
		}
	}

	qb_list_for_each_entry(item, &l->level[sig->p].job_head, list) {
		if (item->type != QB_LOOP_SIG) {
			continue;
		}
		sig_clone = (struct qb_loop_sig *)item;
		if (sig_clone->cloned_from == sig) {
			qb_loop_level_item_del(&l->level[sig->p], item);
			qb_util_log(LOG_TRACE, "deleting sig in JOBLIST");
			break;
		}
	}

	qb_list_del(&sig->item.list);
	(void)signal(sig->signal, SIG_DFL);
	free(sig);
	_adjust_sigactions_(s);
	return 0;
}
