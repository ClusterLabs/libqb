/*
 * Copyright (C) 2003-2010 Red Hat, Inc.
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
#ifndef QB_POLL_H_DEFINED
#define QB_POLL_H_DEFINED

#include <qb/qbhdb.h>
#include <pthread.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

typedef void *qb_poll_timer_handle;

qb_hdb_handle_t qb_poll_create(void);

int qb_poll_destroy(qb_hdb_handle_t hdb_handle);

int qb_poll_dispatch_add(qb_hdb_handle_t handle,
			 int fd,
			 int events,
			 void *data,
			 int (*dispatch_fn) (qb_hdb_handle_t handle,
					     int fd, int revents, void *data));

int qb_poll_dispatch_modify(qb_hdb_handle_t handle,
			    int fd,
			    int events,
			    int (*dispatch_fn) (qb_hdb_handle_t hdb_handle_t,
						int fd,
						int revents, void *data));

int qb_poll_dispatch_delete(qb_hdb_handle_t handle, int fd);

int qb_poll_timer_add(qb_hdb_handle_t handle,
		      int msec_in_future, void *data,
		      void (*timer_fn) (void *data),
		      qb_poll_timer_handle * timer_handle_out);

int qb_poll_timer_delete(qb_hdb_handle_t handle,
			 qb_poll_timer_handle timer_handle);

int qb_poll_run(qb_hdb_handle_t handle);

int qb_poll_stop(qb_hdb_handle_t handle);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_POLL_H_DEFINED */
