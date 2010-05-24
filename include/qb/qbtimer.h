/*
 * Copyright (C) 2006-2007, 2010 Red Hat, Inc.
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

#ifndef QB_TIMER_H_DEFINED
#define QB_TIMER_H_DEFINED

typedef void * qb_timer_handle;

extern int qb_timer_init (
        void (*serialize_lock) (void),
        void (*serialize_unlock) (void),
	int sched_priority);

extern int qb_timer_add_duration (
	unsigned long long nanosec_duration,
	void *data,
	void (*timer_fn) (void *data),
	qb_timer_handle *handle);

extern int qb_timer_add_absolute (
	unsigned long long nanoseconds_from_epoch,
	void *data,
	void (*timer_fn) (void *data),
	qb_timer_handle *handle);

extern void qb_timer_delete (qb_timer_handle handle);

extern void qb_timer_delete_data (qb_timer_handle handle);

extern void qb_timer_lock (void);

extern void qb_timer_unlock (void);

extern unsigned long long qb_timer_time_get (void);

extern unsigned long long qb_timer_expire_time_get (qb_timer_handle handle);

#endif /* QB_TIMER_H_DEFINED */

