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

#ifndef QB_LOOP_DEFINED
#define QB_LOOP_DEFINED

/*
 * Main loop functions
 */
enum qb_loop_priority {
	QB_LOOP_LOW = 0,
	QB_LOOP_MED = 1,
	QB_LOOP_HIGH = 2,
};

typedef struct qb_loop qb_loop_t;

qb_loop_t * qb_loop_create(void);
void qb_loop_stop(qb_loop_t *l);
void qb_loop_run(qb_loop_t *l);


/*
 * Job API
 */

typedef void *qb_loop_job_handle;

typedef void (*qb_loop_job_dispatch_fn)(void *data);

int32_t qb_loop_job_add(qb_loop_t *l,
			enum qb_loop_priority p,
			void *data,
			qb_loop_job_dispatch_fn dispatch_fn);

/*
 * Timer API
 */

typedef void *qb_loop_timer_handle;
#define qb_poll_timer_handle qb_loop_timer_handle

typedef void (*qb_loop_timer_dispatch_fn)(void *data);

int32_t qb_loop_timer_add(qb_loop_t *l,
			  enum qb_loop_priority p,
		      int32_t msec_duration,
		      void *data,
		      qb_loop_timer_dispatch_fn timer_fn,
		      qb_loop_timer_handle * timer_handle_out);

int32_t qb_loop_timer_del(qb_loop_t *l, qb_loop_timer_handle th);

/*
 * Poll API
 */
typedef void (*qb_loop_poll_low_fds_event_fn) (int32_t not_enough, int32_t fds_available);
int32_t qb_loop_poll_low_fds_event_set(
	qb_loop_t *loop,
	qb_loop_poll_low_fds_event_fn fn);

typedef int32_t (*qb_loop_poll_dispatch_fn) (int32_t fd, int32_t revents, void *data);


int32_t qb_loop_poll_add(qb_loop_t *l,
			     enum qb_loop_priority p,
			     int32_t fd,
			     int32_t events,
			     void *data,
			     qb_loop_poll_dispatch_fn dispatch_fn);

int32_t qb_loop_poll_mod(qb_loop_t *l,
			 enum qb_loop_priority p,
			 int32_t fd,
			 int32_t events,
			 void *data,
			 qb_loop_poll_dispatch_fn dispatch_fn);

int32_t qb_loop_poll_del(qb_loop_t *l, int32_t fd);

#endif /* QB_LOOP_DEFINED */

