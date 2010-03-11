/*
 * Copyright (c) 2003-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef QB_POLL_H_DEFINED
#define QB_POLL_H_DEFINED

#include <qb/qbhdb.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void * qb_poll_timer_handle;

qb_hdb_handle_t qb_poll_create (void);

int qb_poll_destroy (qb_hdb_handle_t hdb_handle);

int qb_poll_dispatch_add (
	qb_hdb_handle_t handle,
	int fd,
	int events,
	void *data,

	int (*dispatch_fn) (qb_hdb_handle_t handle,
		int fd,
		int revents,
		void *data));

int qb_poll_dispatch_modify (
	qb_hdb_handle_t handle,
	int fd,
	int events,

	int (*dispatch_fn) (qb_hdb_handle_t hdb_handle_t,
		int fd,
		int revents,
		void *data));


int qb_poll_dispatch_delete (
	qb_hdb_handle_t handle,
	int fd);

int qb_poll_timer_add (
	qb_hdb_handle_t handle,
	int msec_in_future, void *data,
	void (*timer_fn) (void *data),
	qb_poll_timer_handle *timer_handle_out);

int qb_poll_timer_delete (
	qb_hdb_handle_t handle,
	qb_poll_timer_handle timer_handle);

int qb_poll_run (
	qb_hdb_handle_t handle);

int qb_poll_stop (
	qb_hdb_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif	/* QB_POLL_H_DEFINED */

