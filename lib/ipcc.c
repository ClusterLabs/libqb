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

#include "ipc_int.h"
#include "util_int.h"
#include <qb/qbdefs.h>
#include <qb/qbipcc.h>

qb_ipcc_connection_t *
qb_ipcc_connect(const char *name, size_t max_msg_size)
{
	int32_t res;
	qb_ipcc_connection_t *c = NULL;
	struct qb_ipc_connection_response response;

	c = calloc(1, sizeof(struct qb_ipcc_connection));
	if (c == NULL) {
		return NULL;
	}

	c->setup.max_msg_size = QB_MAX(max_msg_size,
				       sizeof(struct qb_ipc_connection_response));
	(void)strlcpy(c->name, name, NAME_MAX);
	res = qb_ipcc_us_setup_connect(c, &response);
	if (res < 0) {
		goto disconnect_and_cleanup;
	}
	c->response.type = response.connection_type;
	c->request.type = response.connection_type;
	c->event.type = response.connection_type;
	c->setup.type = response.connection_type;

	c->response.max_msg_size = response.max_msg_size;
	c->request.max_msg_size = response.max_msg_size;
	c->event.max_msg_size = response.max_msg_size;
	c->receive_buf = calloc(1, response.max_msg_size);
	c->fc_enable_max = 1;
	if (c->receive_buf == NULL) {
		res = -ENOMEM;
		goto disconnect_and_cleanup;
	}

	switch (c->request.type) {
	case QB_IPC_SHM:
		res = qb_ipcc_shm_connect(c, &response);
		break;
	case QB_IPC_SOCKET:
		res = qb_ipcc_us_connect(c, &response);
		break;
	case QB_IPC_POSIX_MQ:
	case QB_IPC_SYSV_MQ:
		res = -ENOTSUP;
		break;
	default:
		res = -EINVAL;
		break;
	}
	if (res != 0) {
		goto disconnect_and_cleanup;
	}
	c->is_connected = QB_TRUE;
	return c;

disconnect_and_cleanup:
	qb_ipcc_us_sock_close(c->setup.u.us.sock);
	free(c->receive_buf);
	free(c);
	errno = -res;
	return NULL;
}

static void
_check_connection_state(struct qb_ipcc_connection * c, int32_t res)
{
	if (res >= 0) return;

	if (qb_ipc_us_sock_error_is_disconnected(res)) {
		errno = -res;
		qb_util_perror(LOG_DEBUG,
			    "interpreting result %d as a disconnect",
			    res);
		c->is_connected = QB_FALSE;
	}
}

static struct qb_ipc_one_way *
_event_sock_one_way_get(struct qb_ipcc_connection * c)
{
	if (c->needs_sock_for_poll) {
		return &c->setup;
	}
	if (c->event.type == QB_IPC_SOCKET) {
		return &c->event;
	}
	return NULL;
}

static struct qb_ipc_one_way *
_response_sock_one_way_get(struct qb_ipcc_connection * c)
{
	if (c->needs_sock_for_poll) {
		return &c->setup;
	}
	if (c->response.type == QB_IPC_SOCKET) {
		return &c->response;
	}
	return NULL;
}

ssize_t
qb_ipcc_send(struct qb_ipcc_connection * c, const void *msg_ptr, size_t msg_len)
{
	ssize_t res;
	ssize_t res2;

	if (c == NULL || msg_len > c->request.max_msg_size) {
		return -EINVAL;
	}
	if (c->funcs.fc_get) {
		res = c->funcs.fc_get(&c->request);
		if (res < 0) {
			return res;
		} else if (res > 0 && res <= c->fc_enable_max) {
			return -EAGAIN;
		} else {
			/*
			 * we can transmit
			 */
		}
	}

	res = c->funcs.send(&c->request, msg_ptr, msg_len);
	if (res == msg_len && c->needs_sock_for_poll) {
		do {
			res2 = qb_ipc_us_send(&c->setup, msg_ptr, 1);
		} while (res2 == -EAGAIN);
		if (res2 == -EPIPE) {
			res2 = -ENOTCONN;
		}
		if (res2 != 1) {
			res = res2;
		}
	}
	_check_connection_state(c, res);
	return res;
}

int32_t
qb_ipcc_fc_enable_max_set(struct qb_ipcc_connection * c, uint32_t max)
{
	if (c == NULL || max > 2) {
		return -EINVAL;
	}
	c->fc_enable_max = max;
	return 0;
}

ssize_t
qb_ipcc_sendv(struct qb_ipcc_connection * c, const struct iovec * iov,
	      size_t iov_len)
{
	int32_t total_size = 0;
	int32_t i;
	int32_t res;
	int32_t res2;

	for (i = 0; i < iov_len; i++) {
		total_size += iov[i].iov_len;
	}
	if (c == NULL || total_size > c->request.max_msg_size) {
		return -EINVAL;
	}

	if (c->funcs.fc_get) {
		res = c->funcs.fc_get(&c->request);
		if (res < 0) {
			return res;
		} else if (res > 0 && res <= c->fc_enable_max) {
			return -EAGAIN;
		} else {
			/*
			 * we can transmit
			 */
		}
	}

	res = c->funcs.sendv(&c->request, iov, iov_len);
	if (res > 0 && c->needs_sock_for_poll) {
		do {
			res2 = qb_ipc_us_send(&c->setup, &res, 1);
		} while (res2 == -EAGAIN);
		if (res2 == -EPIPE) {
			res2 = -ENOTCONN;
		}
		if (res2 != 1) {
			res = res2;
		}
	}
	_check_connection_state(c, res);
	return res;
}

ssize_t
qb_ipcc_recv(struct qb_ipcc_connection * c, void *msg_ptr,
	     size_t msg_len, int32_t ms_timeout)
{
	int32_t res = 0;
	int32_t res2 = 0;
	int32_t poll_ms = 0;
	if (c == NULL) {
		return -EINVAL;
	}

	res = c->funcs.recv(&c->response, msg_ptr, msg_len, ms_timeout);
	if (res == -EAGAIN || res == -ETIMEDOUT) {
		struct qb_ipc_one_way *ow = _response_sock_one_way_get(c);

		if (ow == NULL) return res;
                if (res == -EAGAIN) poll_ms = ms_timeout;

		res2 = qb_ipc_us_ready(ow, poll_ms, POLLIN);
		if (res2 < 0) {
			res = res2;
		}
	}
	_check_connection_state(c, res);
	return res;
}

ssize_t
qb_ipcc_sendv_recv(qb_ipcc_connection_t * c,
		   const struct iovec * iov, uint32_t iov_len,
		   void *res_msg, size_t res_len, int32_t ms_timeout)
{
	ssize_t res = 0;
	int32_t timeout_now;
	int32_t timeout_rem = ms_timeout;

	if (c == NULL) {
		return -EINVAL;
	}

	if (c->funcs.fc_get) {
		res = c->funcs.fc_get(&c->request);
		if (res < 0) {
			return res;
		} else if (res > 0 && res <= c->fc_enable_max) {
			return -EAGAIN;
		} else {
			/*
			 * we can transmit
			 */
		}
	}

	res = qb_ipcc_sendv(c, iov, iov_len);
	if (res < 0) {
		return res;
	}

	do {
		if (timeout_rem > QB_IPC_MAX_WAIT_MS || ms_timeout == -1) {
			timeout_now = QB_IPC_MAX_WAIT_MS;
		} else {
			timeout_now = timeout_rem;
		}

		res = qb_ipcc_recv(c, res_msg, res_len, timeout_now);
		if (res == -ETIMEDOUT) {
			if (ms_timeout < 0) {
				res = -EAGAIN;
			} else {
				timeout_rem -= timeout_now;
				if (timeout_rem > 0) {
					res = -EAGAIN;
				}
			}
		} else	if (res < 0 && res != -EAGAIN) {
			errno = -res;
			qb_util_perror(LOG_DEBUG,
				       "qb_ipcc_recv %d timeout:(%d/%d)",
				       res, timeout_now, timeout_rem);
		}
	} while (res == -EAGAIN && c->is_connected);

	return res;
}

int32_t
qb_ipcc_fd_get(struct qb_ipcc_connection * c, int32_t * fd)
{
	if (c == NULL) {
		return -EINVAL;
	}
	if (c->event.type == QB_IPC_SOCKET) {
		*fd = c->event.u.us.sock;
	} else {
		*fd = c->setup.u.us.sock;
	}
	return 0;
}

ssize_t
qb_ipcc_event_recv(struct qb_ipcc_connection * c, void *msg_pt,
		   size_t msg_len, int32_t ms_timeout)
{
	char one_byte = 1;
	int32_t res;
	ssize_t size;
	struct qb_ipc_one_way *ow = NULL;

	if (c == NULL) {
		return -EINVAL;
	}
	ow = _event_sock_one_way_get(c);
	if (ow) {
		res = qb_ipc_us_ready(ow, ms_timeout, POLLIN);
		if (res < 0) {
			_check_connection_state(c, res);
			return res;
		}
	}
	size = c->funcs.recv(&c->event, msg_pt, msg_len, ms_timeout);
	if (size < 0) {
		_check_connection_state(c, size);
		return size;
	}
	if (c->needs_sock_for_poll) {
		res = qb_ipc_us_recv(&c->setup, &one_byte, 1, -1);
		if (res < 0) {
			_check_connection_state(c, res);
			return res;
		}
	}
	return size;
}

void
qb_ipcc_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_one_way *ow = NULL;
	int32_t res = 0;

	qb_util_log(LOG_DEBUG, "%s()", __func__);

	if (c == NULL) {
		return;
	}

	ow = _event_sock_one_way_get(c);
	if (ow) {
		res = qb_ipc_us_ready(ow, 0, POLLIN);
		_check_connection_state(c, res);
		qb_ipcc_us_sock_close(ow->u.us.sock);
	}

	if (c->funcs.disconnect) {
		c->funcs.disconnect(c);
	}
	free(c->receive_buf);
	free(c);
}

void
qb_ipcc_context_set(struct qb_ipcc_connection *c, void *context)
{
	if (c == NULL) {
		return;
	}
	c->context = context;
}

void *qb_ipcc_context_get(struct qb_ipcc_connection *c)
{
	if (c == NULL) {
		return NULL;
	}
	return c->context;
}

int32_t
qb_ipcc_is_connected(qb_ipcc_connection_t *c)
{
	struct qb_ipc_one_way *ow;

	if (c == NULL) {
		return QB_FALSE;
	}

	ow = _response_sock_one_way_get(c);
	if (ow) {
		_check_connection_state(c, qb_ipc_us_ready(ow, 0, POLLIN));
	}

	return c->is_connected;
}
