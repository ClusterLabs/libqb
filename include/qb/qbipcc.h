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

extern int32_t
qb_ipcc_service_connect(const char *socket_name,
			unsigned int service,
			size_t request_size,
			size_t respnse__size,
			size_t dispatch_size, qb_hdb_handle_t * handle);

extern int32_t qb_ipcc_service_disconnect(qb_hdb_handle_t handle);

extern int32_t qb_ipcc_fd_get(qb_hdb_handle_t handle, int *fd);

extern int32_t
qb_ipcc_dispatch_get(qb_hdb_handle_t handle, void **buf, int timeout);

extern int32_t qb_ipcc_dispatch_put(qb_hdb_handle_t handle);

extern int32_t
qb_ipcc_dispatch_flow_control_get(qb_hdb_handle_t handle,
				  unsigned int *flow_control_state);

extern int32_t
qb_ipcc_msg_send(qb_hdb_handle_t handle,
		 const struct iovec *iov, unsigned int iov_len);

extern int32_t
qb_ipcc_msg_send_reply_receive(qb_hdb_handle_t handle,
			       const struct iovec *iov,
			       unsigned int iov_len,
			       void *res_msg, size_t res_len);

extern int32_t
qb_ipcc_msg_send_reply_receive_in_buf_get(qb_hdb_handle_t handle,
					  const struct iovec *iov,
					  unsigned int iov_len, void **res_msg);

extern int32_t
qb_ipcc_msg_send_reply_receive_in_buf_put(qb_hdb_handle_t handle);

extern int32_t
qb_ipcc_zcb_alloc(qb_hdb_handle_t handle,
		  void **buffer, size_t size, size_t header_size);

extern int32_t qb_ipcc_zcb_free(qb_hdb_handle_t handle, void *buffer);

extern int32_t
qb_ipcc_zcb_msg_send_reply_receive(qb_hdb_handle_t handle,
				   void *msg, void *res_msg, size_t res_len);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCC_H_DEFINED */
