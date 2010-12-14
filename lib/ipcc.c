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
#include <qb/qbdefs.h>
#include <qb/qbipcc.h>

qb_ipcc_connection_t *qb_ipcc_connect(const char *name, size_t max_msg_size)
{
	int32_t res;
	qb_ipcc_connection_t *c = NULL;
	struct qb_ipc_connection_response response;

	c = calloc(1, sizeof(struct qb_ipcc_connection));
	if (c == NULL) {
		return NULL;
	}

	c->setup.max_msg_size = max_msg_size;
	strcpy(c->name, name);
	res = qb_ipcc_us_setup_connect(c, &response);
	if (res < 0) {
		goto disconnect_and_cleanup;
	}
	c->type = response.connection_type;
	c->response.max_msg_size = response.max_msg_size;
	c->request.max_msg_size = response.max_msg_size;
	c->event.max_msg_size = response.max_msg_size;
	c->receive_buf = malloc(response.max_msg_size);

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
		res = qb_ipcc_us_connect(c, &response);
		break;
	default:
		res = -EINVAL;
		break;
	}
	if (res != 0) {
		goto disconnect_and_cleanup;
	}
	return c;

 disconnect_and_cleanup:
	qb_ipcc_us_sock_close(c->setup.u.us.sock);
	free(c);
	errno = -res;
	return NULL;
}

ssize_t qb_ipcc_send(struct qb_ipcc_connection * c, const void *msg_ptr,
		     size_t msg_len)
{
	ssize_t res;
	ssize_t res2;

	if (msg_len > c->request.max_msg_size) {
		return -EINVAL;
	}
	if (c->funcs.fc_get && c->funcs.fc_get(&c->request)) {
		return -EAGAIN;
	}

	res = c->funcs.send(&c->request, msg_ptr, msg_len);
	if (res == msg_len && c->needs_sock_for_poll) {
		do {
			res2 = qb_ipc_us_send(&c->setup, msg_ptr, 1);
		} while (res2 == -EAGAIN);
		if (res2 != 1) {
			res = res2;
		}
	}
	return res;
}

ssize_t qb_ipcc_sendv(struct qb_ipcc_connection* c, const struct iovec* iov,
		      size_t iov_len)
{
	int32_t total_size = 0;
	int32_t i;
	int32_t res;

	for (i = 0; i < iov_len; i++) {
		total_size += iov[i].iov_len;
	}
	if (total_size > c->request.max_msg_size) {
		return -EINVAL;
	}

	if (c->funcs.fc_get && c->funcs.fc_get(&c->request)) {
		return -EAGAIN;
	}

	res = c->funcs.sendv(&c->request, iov, iov_len);
	if (res > 0 && c->needs_sock_for_poll) {
		do {
			res = qb_ipc_us_send(&c->setup, &res, 1);
		} while (res == -EAGAIN);
	}
	return res;
}

ssize_t qb_ipcc_recv(struct qb_ipcc_connection * c, void *msg_ptr,
		     size_t msg_len)
{
	int32_t res = 0;

	res = c->funcs.recv(&c->response, msg_ptr, msg_len, 1000);
	if (res == -EAGAIN && c->needs_sock_for_poll) {
		res = qb_ipc_us_recv_ready(&c->setup, 10);
		if (res < 0) {
			return res;
		} else {
			return -EAGAIN;
		}
	}
	return res;
}

ssize_t qb_ipcc_sendv_recv (
	qb_ipcc_connection_t *c,
	const struct iovec *iov,
	unsigned int iov_len,
	void *res_msg,
	size_t res_len)
{
	ssize_t res;

	if (c->funcs.fc_get && c->funcs.fc_get(&c->request)) {
		return -EAGAIN;
	}

repeat_send:
	res = qb_ipcc_sendv(c, iov, iov_len);
	if (res < 0) {
		if (res == -EAGAIN) {
			goto repeat_send;
		}
		return res;
	}

repeat_recv:
	res = qb_ipcc_recv(c, res_msg, res_len);
	if (res == -EAGAIN) {
		goto repeat_recv;
	}
	return res;
}

int32_t qb_ipcc_fd_get(struct qb_ipcc_connection * c, int32_t * fd)
{
	if (c->type == QB_IPC_SOCKET) {
		*fd = c->event.u.us.sock;
	} else {
		*fd = c->setup.u.us.sock;
	}
	return 0;
}

ssize_t qb_ipcc_event_recv(struct qb_ipcc_connection * c, void *msg_pt,
			   size_t msg_len, int32_t ms_timeout)
{
	char one_byte = 1;
	int32_t res;
	ssize_t size;
	struct qb_ipc_one_way *ow = NULL;

	if (c->needs_sock_for_poll) {
		ow = &c->setup;
	}
	if (c->type == QB_IPC_SOCKET) {
		ow = &c->event;
	}
	if (ow) {
		res = qb_ipc_us_recv_ready(ow, ms_timeout);
		if (res < 0) {
			return res;
		}
	}
	size = c->funcs.recv(&c->event, msg_pt, msg_len, ms_timeout);
	if (size < 0) {
		return size;
	}
	if (c->needs_sock_for_poll) {
		res = qb_ipc_us_recv(&c->setup, &one_byte, 1, 0);
		if (res < 0) {
			return res;
		}
	}
	return size;
}

void qb_ipcc_disconnect(struct qb_ipcc_connection *c)
{
	qb_util_log(LOG_DEBUG, "%s()", __func__);

	qb_ipcc_us_sock_close(c->setup.u.us.sock);
	if (c->funcs.disconnect) {
		c->funcs.disconnect(c);
	}
	free(c->receive_buf);
	free(c);
}
