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

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

struct iovec;

typedef int32_t(*qb_ipcs_init_fn_lvalue) (void *conn);
typedef int32_t(*qb_ipcs_exit_fn_lvalue) (void *conn);
typedef void (*qb_ipcs_handler_fn_lvalue) (void *conn, const void *msg);

struct qb_ipcs_init_state {
	const char *socket_name;
	int32_t sched_policy;
	const struct sched_param *sched_param;
	void *(*malloc) (size_t size);
	void (*free) (void *ptr);
	int32_t(*service_available) (uint32_t service);
	int32_t(*private_data_size_get) (uint32_t service);
	int32_t(*security_valid) (int32_t uid, int32_t gid);
	void (*serialize_lock) (void);
	void (*serialize_unlock) (void);
	int32_t(*sending_allowed) (uint32_t service, uint32_t id,
				   const void *msg,
				   void *sending_allowed_private_data);
	void (*sending_allowed_release) (void *sending_allowed_private_data);
	void (*poll_accept_add) (int32_t fd);
	void (*poll_dispatch_add) (int32_t fd, void *context);
	void (*poll_dispatch_modify) (int32_t fd, int32_t events);
	void (*poll_dispatch_destroy) (int32_t fd, void *context);
	void (*fatal_error) (const char *error_msg);
	qb_ipcs_init_fn_lvalue(*init_fn_get) (uint32_t service);
	qb_ipcs_exit_fn_lvalue(*exit_fn_get) (uint32_t service);
	qb_ipcs_handler_fn_lvalue(*handler_fn_get) (uint32_t service,
						    uint32_t id);
	qb_hdb_handle_t(*stats_create_connection) (const char *name, pid_t pid,
						   int32_t fd);
	void (*stats_destroy_connection) (qb_hdb_handle_t handle);
	void (*stats_update_value) (qb_hdb_handle_t handle,
				    const char *name, const void *value,
				    size_t value_len);
	void (*stats_increment_value) (qb_hdb_handle_t handle,
				       const char *name);
	void (*stats_decrement_value) (qb_hdb_handle_t handle,
				       const char *name);
};

void qb_ipcs_ipc_init(struct qb_ipcs_init_state *init_state);

void *qb_ipcs_private_data_get(void *conn);

int32_t qb_ipcs_response_send(void *conn, const void *msg, size_t mlen);

int32_t qb_ipcs_response_iov_send(void *conn,
				  const struct iovec *iov, uint32_t iov_len);

int32_t qb_ipcs_dispatch_send(void *conn, const void *msg, size_t mlen);

int32_t qb_ipcs_dispatch_iov_send(void *conn,
				  const struct iovec *iov, uint32_t iov_len);

void qb_ipcs_refcount_inc(void *conn);

void qb_ipcs_refcount_dec(void *conn);

void qb_ipcs_ipc_exit(void);

int32_t qb_ipcs_ipc_service_exit(uint32_t service);

int32_t qb_ipcs_handler_accept(int32_t fd, int32_t revent, void *context);

int32_t qb_ipcs_handler_dispatch(int32_t fd, int32_t revent, void *context);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCS_H_DEFINED */
