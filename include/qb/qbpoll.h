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
typedef void *qb_poll_job_handle;

/**
 * return < 0 for removal
 * return == 0 for no-op
 * return > 0 for work done
 */
typedef int32_t (*qb_poll_job_execute_fn_t) (void *data);

qb_handle_t qb_poll_create(void);

int32_t qb_poll_destroy(qb_handle_t hdb_handle);

int32_t qb_poll_dispatch_add(qb_handle_t handle,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 int32_t (*dispatch_fn) (qb_handle_t handle,
					     int32_t fd, int32_t revents, void *data));

int32_t qb_poll_dispatch_modify(qb_handle_t handle,
			    int32_t fd,
			    int32_t events,
			    int32_t (*dispatch_fn) (qb_handle_t hdb_handle_t,
						int32_t fd,
						int32_t revents, void *data));

int32_t qb_poll_dispatch_delete(qb_handle_t handle, int32_t fd);

int32_t qb_poll_timer_add(qb_handle_t handle,
		      int32_t msec_in_future, void *data,
		      void (*timer_fn) (void *data),
		      qb_poll_timer_handle * timer_handle_out);

int32_t qb_poll_timer_delete(qb_handle_t handle, qb_poll_timer_handle timer_handle);

int32_t qb_poll_job_add(qb_handle_t handle,
		      void *data,
		      qb_poll_job_execute_fn_t execute_fn,
		      qb_poll_job_handle * handle_out);
int32_t qb_poll_job_delete(qb_handle_t poll_handle, qb_poll_job_handle job_handle);

int32_t qb_poll_run(qb_handle_t handle);

int32_t qb_poll_stop(qb_handle_t handle);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_POLL_H_DEFINED */
