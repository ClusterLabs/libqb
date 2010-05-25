/*
 * Copyright (C) 2006-2009 Red Hat, Inc.
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

#ifndef QB_IPCS_H_DEFINED
#define QB_IPCS_H_DEFINED

#include <stdlib.h>
#include <qb/qbipc_common.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec;

typedef int (*qb_ipcs_init_fn_lvalue) (void *conn);
typedef int (*qb_ipcs_exit_fn_lvalue) (void *conn);
typedef void (*qb_ipcs_handler_fn_lvalue) (void *conn, const void *msg);

struct qb_ipcs_init_state {
	const char *socket_name;
	int sched_policy;
	const struct sched_param *sched_param;
	void *(*malloc) (size_t size);
	void (*free) (void *ptr);
	int (*service_available)(unsigned int service);
	int (*private_data_size_get)(unsigned int service);
	int (*security_valid)(int uid, int gid);
	void (*serialize_lock)(void);
	void (*serialize_unlock)(void);
	int (*sending_allowed)(unsigned int service, unsigned int id,
		const void *msg, void *sending_allowed_private_data);
	void (*sending_allowed_release)(void *sending_allowed_private_data);
	void (*poll_accept_add)(int fd);
	void (*poll_dispatch_add)(int fd, void *context);
	void (*poll_dispatch_modify)(int fd, int events);
	void (*poll_dispatch_destroy)(int fd, void *context);
	void (*fatal_error)(const char *error_msg);
	qb_ipcs_init_fn_lvalue (*init_fn_get)(unsigned int service);
	qb_ipcs_exit_fn_lvalue (*exit_fn_get)(unsigned int service);
	qb_ipcs_handler_fn_lvalue (*handler_fn_get)(unsigned int service, unsigned int id);
	qb_hdb_handle_t (*stats_create_connection) (const char* name,
		pid_t pid, int fd);
	void (*stats_destroy_connection) (qb_hdb_handle_t handle);
	void (*stats_update_value) (qb_hdb_handle_t handle,
		const char *name, const void *value, size_t value_len);
	void (*stats_increment_value) (qb_hdb_handle_t handle, const char* name);
	void (*stats_decrement_value) (qb_hdb_handle_t handle, const char* name);
};

extern void qb_ipcs_ipc_init (
        struct qb_ipcs_init_state *init_state);

extern void *qb_ipcs_private_data_get (void *conn);

extern int qb_ipcs_response_send (
	void *conn,
	const void *msg,
	size_t mlen);

extern int qb_ipcs_response_iov_send (
	void *conn,
	const struct iovec *iov,
	unsigned int iov_len);

extern int qb_ipcs_dispatch_send (
	void *conn,
	const void *msg,
	size_t mlen);

extern int qb_ipcs_dispatch_iov_send (
	void *conn,
	const struct iovec *iov,
	unsigned int iov_len);

extern void qb_ipcs_refcount_inc (void *conn);

extern void qb_ipcs_refcount_dec (void *conn);

extern void qb_ipcs_ipc_exit (void);

extern int qb_ipcs_ipc_service_exit (unsigned int service);

extern int qb_ipcs_handler_accept (
	int fd,
	int revent,
	void *context);

extern int qb_ipcs_handler_dispatch (
	int fd,
	int revent,
	void *context);

#ifdef __cplusplus
}
#endif

#endif /* QB_IPCS_H_DEFINED */
