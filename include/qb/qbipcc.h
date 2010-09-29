/*
 * Copyright (C) 2006-2007, 2009 Red Hat, Inc.
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

#ifndef QB_IPCC_H_DEFINED
#define QB_IPCC_H_DEFINED

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <qb/qbhdb.h>
#include <qb/qbipc_common.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

typedef struct qb_ipcc_connection qb_ipcc_connection_t;

qb_ipcc_connection_t*
qb_ipcc_connect(const char *name);

int32_t qb_ipcc_send(qb_ipcc_connection_t* c, const void *msg_ptr,
                     size_t msg_len);
ssize_t qb_ipcc_recv(qb_ipcc_connection_t* c, void *msg_ptr,
                     size_t msg_len);

void qb_ipcc_disconnect(qb_ipcc_connection_t* c);


/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCC_H_DEFINED */
