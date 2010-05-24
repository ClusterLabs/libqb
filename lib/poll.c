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

#include <config.h>

#include <errno.h>
#include <pthread.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <qb/qbhdb.h>
#include <qb/qbpoll.h>
#include <qb/qblist.h>
#include "tlist.h"

typedef int (*dispatch_fn_t) (qb_hdb_handle_t hdb_handle, int fd, int revents, void *data);

struct qb_poll_entry {
	struct pollfd ufd;
	dispatch_fn_t dispatch_fn;
	void *data;
};

struct qb_poll_instance {
	struct qb_poll_entry *poll_entries;
	struct pollfd *ufds;
	int poll_entry_count;
	struct timerlist timerlist;
	int stop_requested;
};

DECLARE_HDB_DATABASE (poll_instance_database,NULL);

qb_hdb_handle_t qb_poll_create (void)
{
	qb_hdb_handle_t handle;
	struct qb_poll_instance *poll_instance;
	unsigned int res;

	res = qb_hdb_handle_create (&poll_instance_database,
		sizeof (struct qb_poll_instance), &handle);
	if (res != 0) {
		goto error_exit;
	}
	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		goto error_destroy;
	}

	poll_instance->poll_entries = 0;
	poll_instance->ufds = 0;
	poll_instance->poll_entry_count = 0;
	poll_instance->stop_requested = 0;
	timerlist_init (&poll_instance->timerlist);

	return (handle);

error_destroy:
	qb_hdb_handle_destroy (&poll_instance_database, handle);

error_exit:
	return (-1);
}

int qb_poll_destroy (qb_hdb_handle_t handle)
{
	struct qb_poll_instance *poll_instance;
	int res = 0;

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	free (poll_instance->poll_entries);
	free (poll_instance->ufds);

	qb_hdb_handle_destroy (&poll_instance_database, handle);

	qb_hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int qb_poll_dispatch_add (
	qb_hdb_handle_t handle,
	int fd,
	int events,
	void *data,
	int (*dispatch_fn) (
		qb_hdb_handle_t hdb_handle_t,
		int fd,
		int revents,
		void *data))
{
	struct qb_poll_instance *poll_instance;
	struct qb_poll_entry *poll_entries;
	struct pollfd *ufds;
	int found = 0;
	int install_pos;
	int res = 0;

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	for (found = 0, install_pos = 0; install_pos < poll_instance->poll_entry_count; install_pos++) {
		if (poll_instance->poll_entries[install_pos].ufd.fd == -1) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		/*
		 * Grow pollfd list
		 */
		poll_entries = (struct qb_poll_entry *)realloc (poll_instance->poll_entries,
			(poll_instance->poll_entry_count + 1) *
			sizeof (struct qb_poll_entry));
		if (poll_entries == NULL) {
			res = -ENOMEM;
			goto error_put;
		}
		poll_instance->poll_entries = poll_entries;

		ufds = (struct pollfd *)realloc (poll_instance->ufds,
			(poll_instance->poll_entry_count + 1) *
			sizeof (struct pollfd));
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
	qb_hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int qb_poll_dispatch_modify (
	qb_hdb_handle_t handle,
	int fd,
	int events,
	int (*dispatch_fn) (
		qb_hdb_handle_t hdb_handle_t,
		int fd,
		int revents,
		void *data))
{
	struct qb_poll_instance *poll_instance;
	int i;
	int res = 0;

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	/*
	 * Find file descriptor to modify events and dispatch function
	 */
	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
			poll_instance->poll_entries[i].ufd.events = events;
			poll_instance->poll_entries[i].dispatch_fn = dispatch_fn;

			goto error_put;
		}
	}

	res = -EBADF;

error_put:
	qb_hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int qb_poll_dispatch_delete (
	qb_hdb_handle_t handle,
	int fd)
{
	struct qb_poll_instance *poll_instance;
	int i;
	int res = 0;

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
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


	qb_hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int qb_poll_timer_add (
	qb_hdb_handle_t handle,
	int msec_duration, void *data,
	void (*timer_fn) (void *data),
	qb_poll_timer_handle *timer_handle_out)
{
	struct qb_poll_instance *poll_instance;
	int res = 0;

	if (timer_handle_out == NULL) {
		res = -ENOENT;
		goto error_exit;
	}

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	timerlist_add_duration (&poll_instance->timerlist,
		timer_fn, data, ((unsigned long long)msec_duration) * 1000000ULL, timer_handle_out);

	qb_hdb_handle_put (&poll_instance_database, handle);
error_exit:
	return (res);
}

int qb_poll_timer_delete (
	qb_hdb_handle_t handle,
	qb_poll_timer_handle th)
{
	struct qb_poll_instance *poll_instance;
	int res = 0;

	if (th == 0) {
		return (0);
	}
	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	timerlist_del (&poll_instance->timerlist, (void *)th);

	qb_hdb_handle_put (&poll_instance_database, handle);

error_exit:
	return (res);
}

int qb_poll_stop (
	qb_hdb_handle_t handle)
{
	struct qb_poll_instance *poll_instance;
	unsigned int res;

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		goto error_exit;
	}

	poll_instance->stop_requested = 1;

	qb_hdb_handle_put (&poll_instance_database, handle);
error_exit:
	return (res);
}


int qb_poll_run (
	qb_hdb_handle_t handle)
{
	struct qb_poll_instance *poll_instance;
	int i;
	unsigned long long expire_timeout_msec = -1;
	int res;
	int poll_entry_count;

	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		goto error_exit;
	}

	for (;;) {
		for (i = 0; i < poll_instance->poll_entry_count; i++) {
			memcpy (&poll_instance->ufds[i],
				&poll_instance->poll_entries[i].ufd,
				sizeof (struct pollfd));
		}
		expire_timeout_msec = timerlist_msec_duration_to_expire (&poll_instance->timerlist);

		if (expire_timeout_msec != -1 && expire_timeout_msec > 0xFFFFFFFF) {
			expire_timeout_msec = 0xFFFFFFFE;
		}

retry_poll:
		res = poll (poll_instance->ufds,
			poll_instance->poll_entry_count, expire_timeout_msec);
		if (poll_instance->stop_requested) {
			return (0);
		}
		if (errno == EINTR && res == -1) {
			goto retry_poll;
		} else
		if (res == -1) {
			goto error_exit;
		}

		poll_entry_count = poll_instance->poll_entry_count;
		for (i = 0; i < poll_entry_count; i++) {
			if (poll_instance->ufds[i].fd != -1 &&
				poll_instance->ufds[i].revents) {

				res = poll_instance->poll_entries[i].dispatch_fn (handle,
					poll_instance->ufds[i].fd,
					poll_instance->ufds[i].revents,
					poll_instance->poll_entries[i].data);

				/*
				 * Remove dispatch functions that return -1
				 */
				if (res == -1) {
					poll_instance->poll_entries[i].ufd.fd = -1; /* empty entry */
				}
			}
		}
		timerlist_expire (&poll_instance->timerlist);
	} /* for (;;) */

	qb_hdb_handle_put (&poll_instance_database, handle);
error_exit:
	return (-1);
}

#ifdef COMPILE_OUT
void qb_poll_print_state (
	qb_hdb_handle_t handle,
	int fd)
{
	struct qb_poll_instance *poll_instance;
	int i;
	int res = 0;
	res = qb_hdb_handle_get (&poll_instance_database, handle,
		(void *)&poll_instance);
	if (res != 0) {
		res = -ENOENT;
		exit (1);
	}

	for (i = 0; i < poll_instance->poll_entry_count; i++) {
		if (poll_instance->poll_entries[i].ufd.fd == fd) {
		printf ("fd %d\n", poll_instance->poll_entries[i].ufd.fd);
		printf ("events %d\n", poll_instance->poll_entries[i].ufd.events);
		printf ("dispatch_fn %p\n", poll_instance->poll_entries[i].dispatch_fn);
		}
	}
}

#endif
