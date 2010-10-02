/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
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
#include "os_base.h"

#include <pthread.h>
#include <sys/poll.h>

#include <qb/qbhdb.h>
#include <qb/qbpoll.h>
#include <qb/qblist.h>
#include "tlist.h"
#include "util_int.h"
#include <sys/resource.h>

typedef int32_t (*dispatch_fn_t) (qb_handle_t hdb_handle, int32_t fd, int32_t revents,
			      void *data);

struct qb_poll_entry {
	struct pollfd ufd;
	dispatch_fn_t dispatch_fn;
	void *data;
};

struct qb_poll_job {
	qb_poll_job_execute_fn_t execute_fn;
	void *data;
	struct qb_list_head list;
};

struct qb_poll_instance {
	struct qb_poll_entry *poll_entries;
	struct pollfd *ufds;
	int32_t poll_entry_count;
	struct timerlist timerlist;
	int32_t stop_requested;
	int32_t pipefds[2];
	qb_poll_low_fds_event_fn low_fds_event_fn;
	int32_t not_enough_fds;
	struct qb_list_head job_list;
};

QB_HDB_DECLARE(poll_instance_database, NULL);

static int dummy_dispatch_fn (qb_handle_t handle, int fd, int revents, void *data) {
	return (0);
}

qb_handle_t qb_poll_create(void)
{
	qb_handle_t handle;
	struct qb_poll_instance *poll_instance;
	int32_t res;

	res = qb_hdb_handle_create(&poll_instance_database,
				   sizeof(struct qb_poll_instance), &handle);
	if (res != 0) {
		goto error_exit;
	}
	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_destroy;
	}

	poll_instance->poll_entries = 0;
	poll_instance->ufds = 0;
	poll_instance->poll_entry_count = 0;
	poll_instance->stop_requested = 0;
	timerlist_init(&poll_instance->timerlist);
	poll_instance->not_enough_fds = 0;
	qb_list_init(&poll_instance->job_list);

	res = pipe (poll_instance->pipefds);
	if (res != 0) {
		goto error_destroy;
	}

	/*
	 * Allow changes in modify to propogate into new poll instance
	 */
	res = qb_poll_dispatch_add (
		handle,
		poll_instance->pipefds[0],
		POLLIN,
		NULL,
		dummy_dispatch_fn);
	if (res != 0) {
		goto error_destroy;
	}
		
	return (handle);

error_destroy:
	qb_hdb_handle_destroy(&poll_instance_database, handle);

error_exit:
	return (res);
}

int32_t qb_poll_destroy(qb_handle_t handle)
{
	struct qb_poll_instance *poll_instance;
	int32_t res = 0;

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	close(poll_instance->pipefds[0]);
	close(poll_instance->pipefds[1]);
	free(poll_instance->poll_entries);
	free(poll_instance->ufds);

	qb_hdb_handle_destroy(&poll_instance_database, handle);

	qb_hdb_handle_put(&poll_instance_database, handle);

error_exit:
	return (res);
}

int32_t qb_poll_dispatch_add(qb_handle_t handle,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 int32_t (*dispatch_fn) (qb_handle_t hdb_handle_t,
					     int32_t fd, int32_t revents, void *data))
{
	struct qb_poll_instance *poll_instance;
	struct qb_poll_entry *poll_entries;
	struct pollfd *ufds;
	int32_t found = 0;
	int32_t install_pos;
	int32_t res = 0;

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	for (found = 0, install_pos = 0;
	     install_pos < poll_instance->poll_entry_count; install_pos++) {
		if (poll_instance->poll_entries[install_pos].ufd.fd == -1) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		/*
		 * Grow pollfd list
		 */
		poll_entries =
		    (struct qb_poll_entry *)realloc(poll_instance->poll_entries,
						    (poll_instance->
						     poll_entry_count +
						     1) *
						    sizeof(struct
							   qb_poll_entry));
		if (poll_entries == NULL) {
			res = -ENOMEM;
			goto error_put;
		}
		poll_instance->poll_entries = poll_entries;

		ufds = (struct pollfd *)realloc(poll_instance->ufds,
						(poll_instance->poll_entry_count
						 + 1) * sizeof(struct pollfd));
		if (ufds == NULL) {
			res = -ENOMEM;
			goto error_put;
		}
		poll_instance->ufds = ufds;

		poll_instance->poll_entry_count += 1;
		install_pos = poll_instance->poll_entry_count - 1;
	}

	/*
	 * Install new dispatch handler
	 */
	poll_instance->poll_entries[install_pos].ufd.fd = fd;
	poll_instance->poll_entries[install_pos].ufd.events = events;
	poll_instance->poll_entries[install_pos].ufd.revents = 0;
	poll_instance->poll_entries[install_pos].dispatch_fn = dispatch_fn;
	poll_instance->poll_entries[install_pos].data = data;

error_put:
	qb_hdb_handle_put(&poll_instance_database, handle);

error_exit:
	return (res);
}

int32_t qb_poll_dispatch_modify(qb_handle_t handle,
			    int32_t fd,
			    int32_t events,
			    int32_t (*dispatch_fn) (qb_handle_t hdb_handle_t,
						int32_t fd,
						int32_t revents, void *data))
{
	struct qb_poll_instance *poll_instance;
	int32_t i;
	int32_t res = 0;

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	res = -EBADF;
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			int change_notify = 0;

			if (poll_instance->poll_entries[i].ufd.events != events) {
				change_notify = 1;
			}
			poll_instance->poll_entries[i].ufd.events = events;
			poll_instance->poll_entries[i].dispatch_fn =
			    dispatch_fn;
			if (change_notify) {
				char buf = 1;
				write (poll_instance->pipefds[1], &buf, 1);
			}

			res = 0;
			break;
		}
	}

	qb_hdb_handle_put(&poll_instance_database, handle);

error_exit:
	return (res);
}

int32_t qb_poll_dispatch_delete(qb_handle_t handle, int32_t fd)
{
	struct qb_poll_instance *poll_instance;
	int32_t i;
	int32_t res = 0;

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	/*
	 * Find dispatch fd to delete
	 */
	res = -EBADF;
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			poll_instance->ufds[i].fd = -1;
			poll_instance->poll_entries[i].ufd.fd = -1;
			poll_instance->poll_entries[i].ufd.revents = 0;

			res = 0;
			break;
		}
	}

	qb_hdb_handle_put(&poll_instance_database, handle);

error_exit:
	return (res);
}

int32_t qb_poll_timer_add(qb_handle_t handle,
		      int32_t msec_duration, void *data,
		      void (*timer_fn) (void *data),
		      qb_poll_timer_handle * timer_handle_out)
{
	struct qb_poll_instance *poll_instance;
	int32_t res = 0;

	if (timer_handle_out == NULL) {
		res = -ENOENT;
		goto error_exit;
	}

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	timerlist_add_duration(&poll_instance->timerlist,
			       timer_fn, data,
			       ((uint64_t)msec_duration) * 1000000ULL,
			       timer_handle_out);

	qb_hdb_handle_put(&poll_instance_database, handle);
error_exit:
	return (res);
}

int32_t qb_poll_timer_delete(qb_handle_t handle, qb_poll_timer_handle th)
{
	struct qb_poll_instance *poll_instance;
	int32_t res = 0;

	if (th == 0) {
		return (0);
	}
	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	timerlist_del(&poll_instance->timerlist, (void *)th);

	qb_hdb_handle_put(&poll_instance_database, handle);

error_exit:
	return (res);
}

int32_t qb_poll_job_add(qb_handle_t poll_handle,
		      void *data,
		      qb_poll_job_execute_fn_t execute_fn,
		      qb_poll_job_handle * handle_out)
{
	struct qb_poll_instance *poll_instance;
	int32_t res = 0;
	struct qb_poll_job *job;

	if (handle_out == NULL) {
		res = -ENOENT;
		goto error_exit;
	}

	res = qb_hdb_handle_get(&poll_instance_database, poll_handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}
	job = malloc(sizeof(struct qb_poll_job));
	job->execute_fn = execute_fn;
	job->data = data;
	qb_list_init(&job->list);
	qb_list_add(&job->list, &poll_instance->job_list);
	handle_out = (qb_poll_job_handle)job;

	qb_hdb_handle_put(&poll_instance_database, poll_handle);
error_exit:
	return (res);
}

int32_t qb_poll_job_delete(qb_handle_t poll_handle, qb_poll_job_handle job_handle)
{
	struct qb_poll_instance *poll_instance;
	int32_t res = 0;
	struct qb_poll_job *job = (struct qb_poll_job *)job_handle;

	if (job_handle == NULL) {
		res = -ENOENT;
		goto error_exit;
	}

	res = qb_hdb_handle_get(&poll_instance_database, poll_handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}
	qb_list_del(&job->list);
	free(job);

	qb_hdb_handle_put(&poll_instance_database, poll_handle);
error_exit:
	return (res);
}

int32_t qb_poll_stop(qb_handle_t handle)
{
	struct qb_poll_instance *poll_instance;
	int32_t res;

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	poll_instance->stop_requested = 1;

	qb_hdb_handle_put(&poll_instance_database, handle);
error_exit:
	return (res);
}

static int32_t _qb_poll_job_run(struct qb_poll_instance *poll_instance)
{
	struct qb_poll_job *job = NULL;
	struct qb_list_head *iter;
	size_t jobs_run = 0;

	for (iter = poll_instance->job_list.next;
			iter != &poll_instance->job_list;
			iter = iter->next) {
		job = qb_list_entry(iter, struct qb_poll_job, list);
		if (job == NULL) {
			continue;
		}
		jobs_run += job->execute_fn(job->data);
		if (jobs_run > 10) {
			break;
		}
	}
	return (jobs_run > 0);
}

int32_t qb_poll_low_fds_event_set(
	qb_handle_t handle,
	qb_poll_low_fds_event_fn fn)
{
	struct qb_poll_instance *poll_instance;

	if (qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance) != 0) {
		return -ENOENT;
	}

	poll_instance->low_fds_event_fn = fn;

	qb_hdb_handle_put (&poll_instance_database, handle);
	return 0;
}

/* logs, std(in|out|err), pipe */
#define POLL_FDS_USED_MISC 50

static void poll_fds_usage_check(struct qb_poll_instance *poll_instance)
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

	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd != -1) {
			socks_used++;
		}
	}
	socks_avail = socks_limit - socks_used;
	if (socks_avail < 0) {
		socks_avail = 0;
	}
	send_event = 0;
	if (poll_instance->not_enough_fds) {
		if (socks_avail > 2) {
			poll_instance->not_enough_fds = 0;
			send_event = 1;
		}
	} else {
		if (socks_avail <= 1) {
			poll_instance->not_enough_fds = 1;
			send_event = 1;
		}
	}
	if (send_event) {
		poll_instance->low_fds_event_fn(poll_instance->not_enough_fds,
			socks_avail);
	}
}


int32_t qb_poll_run(qb_handle_t handle)
{
	struct qb_poll_instance *poll_instance;
	int32_t i;
	uint64_t expire_timeout_msec = -1;
	int32_t res;
	int32_t poll_entry_count;
	int32_t job_executed;

	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	for (;;) {
rebuild_poll:
		for (i = 0; i < poll_instance->poll_entry_count; i++) {
			memcpy(&poll_instance->ufds[i],
			       &poll_instance->poll_entries[i].ufd,
			       sizeof(struct pollfd));
		}
		job_executed = _qb_poll_job_run(poll_instance);

		if (job_executed == QB_TRUE) {
			expire_timeout_msec = 0;
		} else {
			expire_timeout_msec =
				timerlist_msec_duration_to_expire
				(&poll_instance->timerlist);
			if (!qb_list_empty(&poll_instance->job_list) &&
					expire_timeout_msec > 50) {
				expire_timeout_msec = 50;
			}
		}
		poll_fds_usage_check(poll_instance);

		if (expire_timeout_msec != -1
		    && expire_timeout_msec > 0xFFFFFFFF) {
			expire_timeout_msec = 0xFFFFFFFE;
		}

retry_poll:
		if (poll_instance->stop_requested) {
			return (0);
		}
		res = poll(poll_instance->ufds,
			   poll_instance->poll_entry_count,
			   expire_timeout_msec);
		if (poll_instance->stop_requested) {
			return (0);
		}
		if (errno == EINTR && res == -1) {
			goto retry_poll;
		} else if (res == -1) {
			res = -errno;
			goto error_exit;
		}

		if (poll_instance->ufds[0].revents) {
			char buf;
			read (poll_instance->ufds[0].fd, &buf, 1);
			goto rebuild_poll;
		}
		poll_entry_count = poll_instance->poll_entry_count;
		for (i = 0; i < poll_entry_count; i++) {
			if (poll_instance->ufds[i].fd != -1 &&
			    poll_instance->ufds[i].revents) {

				res =
				    poll_instance->poll_entries[i].
				    dispatch_fn(handle,
						poll_instance->ufds[i].fd,
						poll_instance->ufds[i].revents,
						poll_instance->poll_entries[i].
						data);

				/*
				 * Remove dispatch functions that return -1
				 */
				if (res < 0) {
					poll_instance->poll_entries[i].ufd.fd = -1;	/* empty entry */
				}
			}
		}
		timerlist_expire(&poll_instance->timerlist);
	}			/* for (;;) */

	qb_hdb_handle_put(&poll_instance_database, handle);
error_exit:
	return res;
}

#ifdef COMPILE_OUT
void qb_poll_print_state(qb_handle_t handle, int32_t fd)
{
	struct qb_poll_instance *poll_instance;
	int32_t i;
	int32_t res = 0;
	res = qb_hdb_handle_get(&poll_instance_database, handle,
				(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		exit(1);
	}

	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			printf("fd %d\n",
			       poll_instance->poll_entries[i].ufd.fd);
			printf("events %d\n",
			       poll_instance->poll_entries[i].ufd.events);
			printf("dispatch_fn %p\n",
			       poll_instance->poll_entries[i].dispatch_fn);
		}
	}
}

#endif
