/*
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>
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
#ifndef QB_IPC_INT_H_DEFINED
#define QB_IPC_INT_H_DEFINED

#include "os_base.h"

#include <dirent.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qbipcc.h>
#include <qb/qbipcs.h>
#include <qb/qbipc_common.h>
#include <qb/qbrb.h>

#define QB_IPC_MAX_WAIT_MS 2000

/*
Client		Server
SEND CONN REQ ->
		ACCEPT & CREATE queues
		or DENY
	<-	SEND ACCEPT(with details)/DENY
*/

struct qb_ipc_connection_request {
	struct qb_ipc_request_header hdr;
	uint32_t max_msg_size;
} __attribute__ ((aligned(8)));

struct qb_ipc_event_connection_request {
	struct qb_ipc_request_header hdr;
	intptr_t connection;
} __attribute__ ((aligned(8)));

struct qb_ipc_connection_response {
	struct qb_ipc_response_header hdr;
	int32_t connection_type;
	uint32_t max_msg_size;
	intptr_t connection;
	char request[PATH_MAX];
	char response[PATH_MAX];
	char event[PATH_MAX];
} __attribute__ ((aligned(8)));

struct qb_ipcc_connection;

struct qb_ipc_one_way {
	size_t max_msg_size;
	enum qb_ipc_type type;
	union {
		struct {
			int32_t sock;
			char *sock_name;
			void* shared_data;
			char shared_file_name[NAME_MAX];
		} us;
		struct {
			qb_ringbuffer_t *rb;
		} shm;
	} u;
};

struct qb_ipcc_funcs {
	ssize_t (*recv)(struct qb_ipc_one_way *one_way, void *buf, size_t buf_size, int32_t timeout);
	ssize_t (*send)(struct qb_ipc_one_way *one_way, const void *data, size_t size);
	ssize_t (*sendv)(struct qb_ipc_one_way *one_way, const struct iovec *iov, size_t iov_len);
	void (*disconnect)(struct qb_ipcc_connection* c);
	int32_t (*fc_get)(struct qb_ipc_one_way *one_way);
};

struct qb_ipcc_connection {
	char name[NAME_MAX];
	int32_t needs_sock_for_poll;
	gid_t egid;
	pid_t server_pid;
	struct qb_ipc_one_way setup;
	struct qb_ipc_one_way request;
	struct qb_ipc_one_way response;
	struct qb_ipc_one_way event;
	struct qb_ipcc_funcs funcs;
	struct qb_ipc_request_header *receive_buf;
	uint32_t fc_enable_max;
	int32_t is_connected;
	void * context;
	uid_t euid;
};

int32_t qb_ipcc_us_setup_connect(struct qb_ipcc_connection *c,
				   struct qb_ipc_connection_response *r);
ssize_t qb_ipc_us_send(struct qb_ipc_one_way *one_way, const void *msg, size_t len);
ssize_t qb_ipc_us_recv(struct qb_ipc_one_way *one_way, void *msg, size_t len, int32_t timeout);
int32_t qb_ipc_us_ready(struct qb_ipc_one_way *ow_data, struct qb_ipc_one_way *ow_conn,
			int32_t ms_timeout, int32_t events);

void qb_ipcc_us_sock_close(int32_t sock);

int32_t qb_ipcc_us_connect(struct qb_ipcc_connection *c, struct qb_ipc_connection_response * response);
int32_t qb_ipcc_shm_connect(struct qb_ipcc_connection *c, struct qb_ipc_connection_response * response);

struct qb_ipcs_service;
struct qb_ipcs_connection;

struct qb_ipcs_funcs {
	int32_t (*connect)(struct qb_ipcs_service *s, struct qb_ipcs_connection *c,
		struct qb_ipc_connection_response *r);
	void (*disconnect)(struct qb_ipcs_connection *c);
	ssize_t (*recv)(struct qb_ipc_one_way *one_way, void *buf, size_t buf_size, int32_t timeout);
	ssize_t (*peek)(struct qb_ipc_one_way *one_way, void **data_out, int32_t timeout);
	void (*reclaim)(struct qb_ipc_one_way *one_way);
	ssize_t (*send)(struct qb_ipc_one_way *one_way, const void *data, size_t size);
	ssize_t (*sendv)(struct qb_ipc_one_way *one_way, const struct iovec* iov, size_t iov_len);
	void (*fc_set)(struct qb_ipc_one_way *one_way, int32_t fc_enable);
	ssize_t (*q_len_get)(struct qb_ipc_one_way *one_way);
};

struct qb_ipcs_service {
	enum qb_ipc_type type;
	char name[NAME_MAX];
	uint32_t max_buffer_size;
	int32_t service_id;
	int32_t ref_count;
	pid_t pid;
	int32_t needs_sock_for_poll;
	int32_t server_sock;

	struct qb_ipcs_service_handlers serv_fns;
	struct qb_ipcs_poll_handlers poll_fns;
	struct qb_ipcs_funcs funcs;
	enum qb_loop_priority poll_priority;

	struct qb_list_head connections;
	struct qb_list_head list;
	struct qb_ipcs_stats stats;

	void *context;
};

enum qb_ipcs_connection_state {
	QB_IPCS_CONNECTION_INACTIVE,
	QB_IPCS_CONNECTION_ACTIVE,
	QB_IPCS_CONNECTION_ESTABLISHED,
	QB_IPCS_CONNECTION_SHUTTING_DOWN,
};

#define CONNECTION_DESCRIPTION NAME_MAX

struct qb_ipcs_connection_auth {
	uid_t uid;
	gid_t gid;
	mode_t mode;
};

struct qb_ipcs_connection {
	enum qb_ipcs_connection_state state;
	int32_t refcount;
	pid_t pid;
	uid_t euid;
	gid_t egid;
	struct qb_ipcs_connection_auth auth;
	struct qb_ipc_one_way setup;
	struct qb_ipc_one_way request;
	struct qb_ipc_one_way response;
	struct qb_ipc_one_way event;
	struct qb_ipcs_service *service;
	struct qb_list_head list;
	struct qb_ipc_request_header *receive_buf;
	void *context;
	int32_t fc_enabled;
	int32_t poll_events;
	int32_t outstanding_notifiers;
	char description[CONNECTION_DESCRIPTION];
	struct qb_ipcs_connection_stats_2 stats;
};

void qb_ipcs_us_init(struct qb_ipcs_service *s);
void qb_ipcs_shm_init(struct qb_ipcs_service *s);

int32_t qb_ipcs_us_publish(struct qb_ipcs_service *s);
int32_t qb_ipcs_us_withdraw(struct qb_ipcs_service *s);
int32_t qb_ipcc_us_sock_connect(const char *socket_name, int32_t * sock_pt);

int32_t qb_ipcs_dispatch_connection_request(int32_t fd, int32_t revents, void *data);
struct qb_ipcs_connection* qb_ipcs_connection_alloc(struct qb_ipcs_service *s);

int32_t qb_ipcs_process_request(struct qb_ipcs_service *s,
	struct qb_ipc_request_header *hdr);

int32_t qb_ipc_us_sock_error_is_disconnected(int err);

int use_filesystem_sockets(void);

void remove_tempdir(const char *name);

#endif /* QB_IPC_INT_H_DEFINED */
