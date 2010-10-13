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

#include <unistd.h>
#include "config.h"

#include <dirent.h>
#include <mqueue.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qbipcc.h>
#include <qb/qbipcs.h>
#include <qb/qbipc_common.h>
#include <qb/qbrb.h>

/*
 * Darwin claims to support process shared synchronization
 * but it really does not.  The unistd.h header file is wrong.
 */
#if defined(QB_DARWIN) || defined(__UCLIBC__)
#undef _POSIX_THREAD_PROCESS_SHARED
#define _POSIX_THREAD_PROCESS_SHARED -1
#endif

#ifndef _POSIX_THREAD_PROCESS_SHARED
#define _POSIX_THREAD_PROCESS_SHARED -1
#endif

#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#endif

/*
Client		Server
SEND CONN REQ ->
		ACCEPT & CREATE queues
		or DENY
	<-	SEND ACCEPT(with details)/DENY
*/

struct qb_ipc_connection_request {
        struct qb_ipc_request_header hdr __attribute__ ((aligned(8)));
	uint32_t max_msg_size __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct qb_ipc_connection_response {
	struct qb_ipc_response_header hdr __attribute__ ((aligned(8)));
	int32_t connection_type __attribute__ ((aligned(8)));
	uint32_t max_msg_size __attribute__ ((aligned(8)));
        char request[PATH_MAX] __attribute__ ((aligned(8)));
        char response[PATH_MAX] __attribute__ ((aligned(8)));
        char event[PATH_MAX] __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct qb_ipcc_connection;

struct qb_ipc_one_way {
	size_t max_msg_size;
	union {
		struct {
			mqd_t q;
			char name[NAME_MAX];
		} pmq;
		struct {
			int32_t q;
			int32_t key;
		} smq;
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
	enum qb_ipc_type type;
	int32_t needs_sock_for_poll;
	int32_t sock;
	struct qb_ipc_one_way request;
	struct qb_ipc_one_way response;
	struct qb_ipc_one_way event;
	struct qb_ipcc_funcs funcs;
	char *receive_buf;
};


int32_t qb_ipc_us_send(int32_t s, const void *msg, size_t len);

int32_t qb_ipc_us_recv (int32_t s, void *msg, size_t len);
int32_t qb_ipc_us_recv_ready(int32_t s, int32_t ms_timeout);

int32_t qb_ipcc_us_connect(const char *socket_name, int32_t *sock_pt);

void qb_ipcc_us_disconnect (int32_t sock);

int32_t qb_ipcc_pmq_connect(struct qb_ipcc_connection *c, struct qb_ipc_connection_response * response);
int32_t qb_ipcc_soc_connect(struct qb_ipcc_connection *c, struct qb_ipc_connection_response * response);
int32_t qb_ipcc_smq_connect(struct qb_ipcc_connection *c, struct qb_ipc_connection_response * response);
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
	int32_t service_id;
	pid_t pid;
	int32_t needs_sock_for_poll;
	int32_t server_sock;

	struct qb_ipcs_service_handlers serv_fns;
	struct qb_ipcs_poll_handlers poll_fns;
	struct qb_ipcs_funcs funcs;
	enum qb_loop_priority poll_priority;

	struct qb_list_head connections;
};

struct qb_ipcs_connection {
	int32_t refcount;
	pid_t pid;
	uid_t euid;
	gid_t egid;
	int32_t sock;
	struct qb_ipc_one_way request;
	struct qb_ipc_one_way response;
	struct qb_ipc_one_way event;
	struct qb_ipcs_service *service;
	struct qb_list_head list;
	char *receive_buf;
	void *context;
	int32_t fc_enabled;
};

void qb_ipcs_pmq_init(struct qb_ipcs_service *s);
void qb_ipcs_us_init(struct qb_ipcs_service *s);
void qb_ipcs_smq_init(struct qb_ipcs_service *s);
void qb_ipcs_shm_init(struct qb_ipcs_service *s);

int32_t qb_ipcs_us_publish(struct qb_ipcs_service *s);
int32_t qb_ipcs_us_withdraw(struct qb_ipcs_service *s);

int32_t qb_ipcs_dispatch_connection_request(int32_t fd, int32_t revents, void *data);
int32_t qb_ipcs_dispatch_service_request(int32_t fd, int32_t revents, void *data);
struct qb_ipcs_connection* qb_ipcs_connection_alloc(struct qb_ipcs_service *s);

int32_t qb_ipcs_process_request(struct qb_ipcs_service *s,
	struct qb_ipc_request_header *hdr);

void qb_ipcs_disconnect(struct qb_ipcs_connection *c);

#endif /* QB_IPC_INT_H_DEFINED */
