/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include "os_base.h"

#include <mqueue.h>
#include "ipc_int.h"
#include "util_int.h"
#include <qb/qbipcc.h>

qb_ipcc_connection_t *qb_ipcc_connect(const char *name, size_t max_msg_size)
{
	int32_t res;
	int32_t usock;
	qb_ipcc_connection_t *c = NULL;
	struct qb_ipc_connection_request request;
	struct qb_ipc_connection_response response;

	res = qb_ipcc_us_connect(name, &usock);
	if (res != 0) {
		errno = -res;
		perror("qb_ipcc_us_connect");
		return NULL;
	}

	request.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	request.hdr.size = sizeof(request);
	request.max_msg_size = max_msg_size;
	res = qb_ipc_us_send(usock, &request, request.hdr.size);
	if (res < 0) {
		perror("qb_ipc_us_send");
		qb_ipcc_us_disconnect(usock);
		errno = -res;
		return NULL;
	}

	res = qb_ipc_us_recv(usock, &response, sizeof(response));
	if (res < 0) {
		perror("qb_ipc_us_recv");
		qb_ipcc_us_disconnect(usock);
		errno = -res;
		return NULL;
	}

	if (response.hdr.error != 0) {
		errno = -response.hdr.error;
		perror("recv:message");
		return NULL;
	}
	c = malloc(sizeof(struct qb_ipcc_connection));
	if (c == NULL) {
		perror("malloc:connection");
		return NULL;
	}
	strcpy(c->name, name);
	c->type = response.connection_type;
	c->sock = usock;

	qb_util_log(LOG_DEBUG, "%s() max_msg_size:%zu actual:%u", __func__,
		    max_msg_size, response.max_msg_size);

	c->max_msg_size = response.max_msg_size;
	c->receive_buf = malloc(c->max_msg_size);

	switch (c->type) {
	case QB_IPC_SHM:
		res = qb_ipcc_shm_connect(c, &response);
		break;
	case QB_IPC_POSIX_MQ:
		res = qb_ipcc_pmq_connect(c, &response);
		break;
	case QB_IPC_SYSV_MQ:
		res = qb_ipcc_smq_connect(c, &response);
		break;
	case QB_IPC_SOCKET:
		c->needs_sock_for_poll = QB_FALSE;
		break;
	default:
		res = -EINVAL;
		break;
	}
	if (res != 0) {
		qb_ipcc_us_disconnect(usock);
		free(c);
		c = NULL;
		errno = -res;
	}
	return c;
}

int32_t qb_ipcc_send(struct qb_ipcc_connection * c, const void *msg_ptr,
		     size_t msg_len)
{
	ssize_t res;

	if (msg_len > c->max_msg_size) {
		return -EINVAL;
	}

	res = c->funcs.send(c, msg_ptr, msg_len);
	if (res > 0 && c->needs_sock_for_poll) {
		qb_ipc_us_send(c->sock, msg_ptr, 1);
	}
	return res;
}

ssize_t qb_ipcc_recv(struct qb_ipcc_connection * c, void *msg_ptr,
		     size_t msg_len)
{
	return c->funcs.recv(c, msg_ptr, msg_len);
}

int32_t qb_ipcc_fd_get(struct qb_ipcc_connection * c, int32_t * fd)
{
	if (c->needs_sock_for_poll) {
		*fd = c->sock;
	} else {
		*fd = 0;	/*TODO?? */
	}
	return 0;
}

int32_t qb_ipcc_event_recv(struct qb_ipcc_connection * c, void **data_out,
			   int32_t timeout)
{
	char one_byte = 1;

	if (c->needs_sock_for_poll) {
		qb_ipc_us_recv(c->sock, &one_byte, 1);
	}
	return c->funcs.event_recv(c, data_out, timeout);
}

void qb_ipcc_event_release(struct qb_ipcc_connection *c)
{
	c->funcs.event_release(c);
}

void qb_ipcc_disconnect(struct qb_ipcc_connection *c)
{
	qb_util_log(LOG_DEBUG, "%s()", __func__);

	qb_ipcc_us_disconnect(c->sock);
	if (c->funcs.disconnect) {
		c->funcs.disconnect(c);
	}
	free(c->receive_buf);
	free(c);
}
