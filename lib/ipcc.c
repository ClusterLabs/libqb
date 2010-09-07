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

qb_ipcc_connection_t *qb_ipcc_connect(const char *name)
{
	int32_t res;
	int32_t usock;
	qb_ipcc_connection_t *c = NULL;
	struct mar_req_initial_setup init_req;
	struct mar_res_initial_setup init_res;

	res = qb_ipcc_us_connect(name, &usock);
	if (res != 0) {
		errno = -res;
		perror("qb_ipcc_us_connect");
		return NULL;
	}

	init_req.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	init_req.hdr.size = sizeof(init_req);
	res = qb_ipc_us_send(usock, &init_req, init_req.hdr.size);
	if (res < 0) {
		perror("qb_ipc_us_send");
		qb_ipcc_us_disconnect(usock);
		errno = -res;
		return NULL;
	}

	res = qb_ipc_us_recv(usock, &init_res, sizeof(init_res));
	if (res < 0) {
		perror("qb_ipc_us_recv");
		qb_ipcc_us_disconnect(usock);
		errno = -res;
		return NULL;
	}

	if (init_res.hdr.error != 0) {
		errno = -init_res.hdr.error;
		perror("recv:message");
		return NULL;
	}
	c = malloc(sizeof(struct qb_ipcc_connection));
	if (c == NULL) {
		perror("malloc:connection");
		return NULL;
	}
	strcpy(c->name, name);
	c->type = init_res.connection_type;
	c->sock = usock;
	c->session_id = init_res.session_id;
	c->max_msg_size = init_res.max_msg_size;
	c->receive_buf = malloc(c->max_msg_size);

	switch (c->type) {
	case QB_IPC_SHM:
		res = qb_ipcc_shm_connect(c);
		break;
	case QB_IPC_POSIX_MQ:
		res = qb_ipcc_pmq_connect(c);
		break;
	case QB_IPC_SYSV_MQ:
		res = qb_ipcc_smq_connect(c);
		break;
	case QB_IPC_SOCKET:
		c->needs_sock_for_poll = QB_FALSE;
		break;
	default:
		res = -EINVAL;
		break;
	}
	if (res != 0) {
		free(c);
		c = NULL;
		errno = -res;
	}
	return c;
}

int32_t qb_ipcc_send(struct qb_ipcc_connection * c, const void *msg_ptr,
		     size_t msg_len)
{
	struct qb_ipc_request_header *hdr = NULL;
	ssize_t res;

	if (msg_len > c->max_msg_size) {
		return -EINVAL;
	}

	hdr = (struct qb_ipc_request_header *)msg_ptr;
	hdr->session_id = c->session_id;
	res = c->funcs.send(c, msg_ptr, msg_len);
	if (res > 0 && c->needs_sock_for_poll) {
		qb_ipc_us_send(c->sock, msg_ptr, 1);
	}
	return res;
}

ssize_t qb_ipcc_recv(struct qb_ipcc_connection * c, const void *msg_ptr,
		     size_t msg_len)
{
	return c->funcs.recv(c, msg_ptr, msg_len);
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
