/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
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
#ifndef QB_IPC_COMMON_H_DEFINED
#define QB_IPC_COMMON_H_DEFINED

#include <stdint.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @file qbipc_common.h
 * common types and definitions
 */

struct qb_ipc_request_header {
	int32_t id __attribute__ ((aligned(8)));
	int32_t size __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

struct qb_ipc_response_header {
	int32_t id __attribute__ ((aligned(8)));
	int32_t size __attribute__ ((aligned(8)));
	int32_t error __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));

enum qb_ipc_type {
	QB_IPC_SOCKET,
	QB_IPC_SHM,
	QB_IPC_POSIX_MQ,
	QB_IPC_SYSV_MQ,
	QB_IPC_NATIVE,
};


#define QB_IPC_MSG_NEW_MESSAGE 0
#define QB_IPC_MSG_USER_START QB_IPC_MSG_NEW_MESSAGE
#define QB_IPC_MSG_AUTHENTICATE -1
#define QB_IPC_MSG_NEW_EVENT_SOCK -2
#define QB_IPC_MSG_DISCONNECT -3

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPC_COMMON_H_DEFINED */
