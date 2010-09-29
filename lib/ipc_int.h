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


struct qb_ipcc_connection;

struct qb_ipcc_funcs {
	int32_t (*send)(struct qb_ipcc_connection* c, const void *msg_ptr,
		size_t msg_len);
	ssize_t (*recv)(struct qb_ipcc_connection* c, void *msg_ptr,
		size_t msg_len);
	void (*disconnect)(struct qb_ipcc_connection* c);
};

struct qb_ipcc_pmq_one_way {
	mqd_t q;
	char name[NAME_MAX];
};

struct qb_ipcc_smq_one_way {
	int32_t q;
	int32_t key;
};

struct qb_ipcc_shm_one_way {
	qb_ringbuffer_t *rb;
	char name[NAME_MAX];
};

struct qb_ipcc_pmq_connection {
	struct qb_ipcc_pmq_one_way request;
	struct qb_ipcc_pmq_one_way response;
	struct qb_ipcc_pmq_one_way event;
};

struct qb_ipcc_smq_connection {
	struct qb_ipcc_smq_one_way request;
	struct qb_ipcc_smq_one_way response;
	struct qb_ipcc_smq_one_way event;
};

struct qb_ipcc_shm_connection {
	struct qb_ipcc_shm_one_way request;
	struct qb_ipcc_shm_one_way response;
	struct qb_ipcc_shm_one_way event;
};

struct qb_ipcc_connection {
	enum qb_ipc_type type;
	char name[NAME_MAX];
	uint64_t session_id;
	int32_t needs_sock_for_poll;
	int32_t sock;
	union {
		struct qb_ipcc_pmq_connection pmq;
		struct qb_ipcc_smq_connection smq;
		struct qb_ipcc_shm_connection shm;
	} u;
	struct qb_ipcc_funcs funcs;
	size_t max_msg_size;
	char *receive_buf;
};


int32_t qb_ipc_us_send(int32_t s, const void *msg, size_t len);

int32_t qb_ipc_us_recv (int32_t s, void *msg, size_t len);

int32_t qb_ipcc_us_connect(const char *socket_name, int32_t *sock_pt);

void qb_ipcc_us_disconnect (int32_t sock);

int32_t qb_ipcc_pmq_connect(struct qb_ipcc_connection *c);
int32_t qb_ipcc_soc_connect(struct qb_ipcc_connection *c);
int32_t qb_ipcc_smq_connect(struct qb_ipcc_connection *c);
int32_t qb_ipcc_shm_connect(struct qb_ipcc_connection *c);

struct qb_ipcs_service;
struct qb_ipcs_connection;

struct qb_ipcs_funcs {
	void (*destroy)(struct qb_ipcs_service *s);
	int32_t (*connect)(struct qb_ipcs_service *s, struct qb_ipcs_connection *c,
		void *data, size_t size);
	void (*disconnect)(struct qb_ipcs_connection *c);
	ssize_t (*request_recv)(struct qb_ipcs_service *s, void *buf, size_t buf_size);
	ssize_t (*response_send)(struct qb_ipcs_connection *c, void *data, size_t size);
};

struct qb_ipcs_service {
	enum qb_ipc_type type;
	char name[NAME_MAX];
	pid_t pid;
	int32_t needs_sock_for_poll;
	int32_t server_sock;
	qb_handle_t poll_handle;

	struct qb_ipcs_service_handlers serv_fns;
	struct qb_ipcs_poll_handlers poll_fns;
	struct qb_ipcs_funcs funcs;

	struct qb_list_head connections;
	union {
		mqd_t q;
		qb_ringbuffer_t *rb;
		struct qb_ipcc_smq_one_way smq;
	} u;
	size_t max_msg_size;
	char *receive_buf;
};

struct qb_ipcs_connection {
	qb_ipcs_connection_handle_t handle;
	pid_t pid;
	uid_t euid;
	gid_t egid;
	int32_t sock;
	union {
		struct qb_ipcc_pmq_connection pmq;
		struct qb_ipcc_smq_connection smq;
		struct qb_ipcc_shm_connection shm;
	} u;
	struct qb_ipcs_service *service;
	struct qb_list_head list;
};

int32_t qb_ipcs_pmq_create(struct qb_ipcs_service *s);
int32_t qb_ipcs_soc_create(struct qb_ipcs_service *s);
int32_t qb_ipcs_smq_create(struct qb_ipcs_service *s);
int32_t qb_ipcs_shm_create(struct qb_ipcs_service *s);

int32_t qb_ipcs_us_publish(struct qb_ipcs_service *s);
int32_t qb_ipcs_us_withdraw(struct qb_ipcs_service *s);

int32_t qb_ipcs_dispatch_connection_request(qb_handle_t hdb_handle_t,
	int32_t fd, int32_t revents, void *data);
int32_t qb_ipcs_dispatch_service_request(qb_handle_t hdb_handle_t,
	int32_t fd, int32_t revents, void *data);
struct qb_ipcs_connection* qb_ipcs_connection_alloc(struct qb_ipcs_service *s);

int32_t qb_ipcs_process_request(struct qb_ipcs_service *s,
	struct qb_ipc_request_header *hdr);

void qb_ipcs_disconnect(struct qb_ipcs_connection *c);

struct mar_req_initial_setup {
        struct qb_ipc_request_header hdr __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct mar_res_initial_setup {
	struct qb_ipc_response_header hdr __attribute__ ((aligned(8)));
	int32_t connection_type __attribute__ ((aligned(8)));
	uint64_t session_id __attribute__ ((aligned(8)));
	uint32_t max_msg_size __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct mar_req_shm_setup {
        struct qb_ipc_request_header hdr __attribute__ ((aligned(8)));
	uint32_t pid __attribute__ ((aligned(8)));
        char request[PATH_MAX] __attribute__ ((aligned(8)));
        char response[PATH_MAX] __attribute__ ((aligned(8)));
        char event[PATH_MAX] __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct mar_req_pmq_setup {
        struct qb_ipc_request_header hdr __attribute__ ((aligned(8)));
	uint32_t pid __attribute__ ((aligned(8)));
        char response_mq[NAME_MAX] __attribute__ ((aligned(8)));
        char event_mq[NAME_MAX] __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct mar_req_smq_setup {
        struct qb_ipc_request_header hdr __attribute__ ((aligned(8)));
	uint32_t pid __attribute__ ((aligned(8)));
        int32_t response_key __attribute__ ((aligned(8)));
        int32_t event_key __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct mar_res_setup {
        struct qb_ipc_response_header hdr __attribute__ ((aligned(8)));
	size_t max_msg_size __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

typedef struct {
	uid_t euid __attribute__ ((aligned(8)));
	gid_t egid __attribute__ ((aligned(8)));
} mar_req_priv_change __attribute__ ((aligned(8)));


#endif /* QB_IPC_INT_H_DEFINED */
