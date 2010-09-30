/*
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>,
 *         Angus Salkeld <asalkeld@redhat.com>
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
#include <qb/qbhdb.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

typedef qb_handle_t qb_ipcs_connection_handle_t;
typedef qb_handle_t qb_ipcs_service_pt;

typedef int32_t (*qb_ipcs_dispatch_fn_t) (qb_ipcs_service_pt s, int32_t fd, int32_t revents,
	void *data);
typedef int32_t (*qb_ipcs_dispatch_add_fn)(qb_ipcs_service_pt s, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn);
typedef int32_t (*qb_ipcs_dispatch_rm_fn)(qb_ipcs_service_pt s, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn);


struct qb_ipcs_poll_handlers {
	qb_ipcs_dispatch_add_fn dispatch_add;
	qb_ipcs_dispatch_rm_fn dispatch_rm;
};

typedef int32_t (*qb_ipcs_connection_authenticate_fn) (qb_ipcs_connection_handle_t c, uid_t uid, gid_t gid);
typedef void (*qb_ipcs_connection_created_fn) (qb_ipcs_connection_handle_t c);
typedef void (*qb_ipcs_connection_destroyed_fn) (qb_ipcs_connection_handle_t c);
typedef void (*qb_ipcs_msg_process_fn) (qb_ipcs_connection_handle_t c,
		void *data, size_t size);

struct qb_ipcs_service_handlers {
	qb_ipcs_connection_authenticate_fn connection_authenticate;
	qb_ipcs_connection_created_fn connection_created;
	qb_ipcs_msg_process_fn msg_process;
	qb_ipcs_connection_destroyed_fn connection_destroyed;
};

qb_ipcs_service_pt qb_ipcs_create(const char* name,
				   enum qb_ipc_type type);

void qb_ipcs_service_handlers_set(qb_ipcs_service_pt s,
	struct qb_ipcs_service_handlers *handlers);

void qb_ipcs_poll_handlers_set(qb_ipcs_service_pt s,
	struct qb_ipcs_poll_handlers *handlers);

int32_t qb_ipcs_run(qb_ipcs_service_pt s, qb_handle_t poll);

void qb_ipcs_destroy(qb_ipcs_service_pt s);

ssize_t qb_ipcs_response_send(qb_ipcs_connection_handle_t c, void *data, size_t size);
ssize_t qb_ipcs_event_send(qb_ipcs_connection_handle_t c, void *data, size_t size);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCS_H_DEFINED */
