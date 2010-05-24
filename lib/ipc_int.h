/*
 * Copyright (C) 2009 Red Hat, Inc.
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
#ifndef QB_IPC_IPC_H_DEFINED
#define QB_IPC_IPC_H_DEFINED

#include <unistd.h>
#include "config.h"

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

enum req_init_types {
	MESSAGE_REQ_RESPONSE_INIT = 0,
	MESSAGE_REQ_DISPATCH_INIT = 1
};

#define MESSAGE_REQ_CHANGE_EUID		1
#define MESSAGE_REQ_OUTQ_FLUSH		2

#define MESSAGE_RES_OUTQ_EMPTY         0
#define MESSAGE_RES_OUTQ_NOT_EMPTY     1
#define MESSAGE_RES_ENABLE_FLOWCONTROL 2
#define MESSAGE_RES_OUTQ_FLUSH_NR      3

struct control_buffer {
	unsigned int read;
	unsigned int write;
#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_t sem0;
	sem_t sem1;
	sem_t sem2;
#endif
};

enum res_init_types {
	MESSAGE_RES_INIT
};

typedef struct {
	int service __attribute__((aligned(8)));
	unsigned long long semkey __attribute__((aligned(8)));
	char control_file[64] __attribute__((aligned(8)));
	char request_file[64] __attribute__((aligned(8)));
	char response_file[64] __attribute__((aligned(8)));
	char dispatch_file[64] __attribute__((aligned(8)));
	size_t control_size __attribute__((aligned(8)));
	size_t request_size __attribute__((aligned(8)));
	size_t response_size __attribute__((aligned(8)));
	size_t dispatch_size __attribute__((aligned(8)));
} mar_req_setup_t __attribute__((aligned(8)));

typedef struct {
	int error __attribute__((aligned(8)));
} mar_res_setup_t __attribute__((aligned(8)));

typedef struct {
        uid_t euid __attribute__((aligned(8)));
        gid_t egid __attribute__((aligned(8)));
} mar_req_priv_change __attribute__((aligned(8)));

typedef struct {
	qb_ipc_response_header_t header __attribute__((aligned(8)));
	uint64_t conn_info __attribute__((aligned(8)));
} mar_res_lib_response_init_t __attribute__((aligned(8)));

typedef struct {
	qb_ipc_response_header_t header __attribute__((aligned(8)));
} mar_res_lib_dispatch_init_t __attribute__((aligned(8)));

typedef struct {
	uint32_t nodeid __attribute__((aligned(8)));
	void *conn __attribute__((aligned(8)));
} mar_message_source_t __attribute__((aligned(8)));

typedef struct {
        qb_ipc_request_header_t header __attribute__((aligned(8)));
        size_t map_size __attribute__((aligned(8)));
        char path_to_file[128] __attribute__((aligned(8)));
} mar_req_qb_ipcc_zc_alloc_t __attribute__((aligned(8)));

typedef struct {
        qb_ipc_request_header_t header __attribute__((aligned(8)));
        size_t map_size __attribute__((aligned(8)));
	uint64_t server_address __attribute__((aligned(8)));
} mar_req_qb_ipcc_zc_free_t __attribute__((aligned(8)));

typedef struct {
        qb_ipc_request_header_t header __attribute__((aligned(8)));
	uint64_t server_address __attribute__((aligned(8)));
} mar_req_qb_ipcc_zc_execute_t __attribute__((aligned(8)));

struct qb_ipcs_zc_header {
	int map_size;
	uint64_t server_address;
};

#define SOCKET_SERVICE_INIT	0xFFFFFFFF

#define ZC_ALLOC_HEADER		0xFFFFFFFF
#define ZC_FREE_HEADER		0xFFFFFFFE
#define ZC_EXECUTE_HEADER	0xFFFFFFFD

#endif /* QB_IPC_IPC_H_DEFINED */
