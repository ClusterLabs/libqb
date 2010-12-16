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
#include <qb/qbloop.h>
#include <qb/qbrb.h>

/*
 * utility functions
 * --------------------------------------------------------
 */
/*
 * client functions
 * --------------------------------------------------------
 */
static void qb_ipcc_shm_disconnect(struct qb_ipcc_connection *c)
{
	qb_rb_close(c->request.u.shm.rb);
	qb_rb_close(c->response.u.shm.rb);
	qb_rb_close(c->event.u.shm.rb);
}

static ssize_t qb_ipc_shm_send(struct qb_ipc_one_way *one_way,
			       const void *msg_ptr, size_t msg_len)
{
	return qb_rb_chunk_write(one_way->u.shm.rb, msg_ptr, msg_len);
}

static ssize_t qb_ipc_shm_sendv(struct qb_ipc_one_way *one_way,
				const struct iovec* iov,
				size_t iov_len)
{
	char *dest;
	int32_t res = 0;
	int32_t total_size = 0;
	int32_t i;
	char *pt = NULL;

	if (one_way->u.shm.rb == NULL) {
		return -ENOTCONN;
	}

	for (i = 0; i < iov_len; i++) {
		total_size += iov[i].iov_len;
	}
	dest = qb_rb_chunk_alloc(one_way->u.shm.rb, total_size);
	if (dest == NULL) {
		return -errno;
	}
	pt = dest;

	for (i = 0; i < iov_len; i++) {
		memcpy(pt, iov[i].iov_base, iov[i].iov_len);
		pt += iov[i].iov_len;
	}
	res = qb_rb_chunk_commit(one_way->u.shm.rb, total_size);
	if (res < 0) {
		return res;
	}
	return total_size;
}

static ssize_t qb_ipc_shm_recv(struct qb_ipc_one_way *one_way,
			       void *msg_ptr,
			       size_t msg_len,
			       int32_t ms_timeout)
{
	if (one_way->u.shm.rb == NULL) {
		return -ENOTCONN;
	}
	return qb_rb_chunk_read(one_way->u.shm.rb,
			       (void *)msg_ptr,
			       msg_len,
			       ms_timeout);
}

static ssize_t qb_ipc_shm_peek(struct qb_ipc_one_way *one_way, void **data_out, int32_t ms_timeout)
{
	if (one_way->u.shm.rb == NULL) {
		return -ENOTCONN;
	}
	return qb_rb_chunk_peek(one_way->u.shm.rb,
			       data_out,
			       ms_timeout);
}

static void qb_ipc_shm_reclaim(struct qb_ipc_one_way *one_way)
{
	if (one_way->u.shm.rb != NULL) {
		qb_rb_chunk_reclaim(one_way->u.shm.rb);
	}
}

static void qb_ipc_shm_fc_set(struct qb_ipc_one_way *one_way,
			       int32_t fc_enable)
{
	int32_t *fc;
	fc = qb_rb_shared_user_data_get(one_way->u.shm.rb);
	*fc = fc_enable;
}

static int32_t qb_ipc_shm_fc_get(struct qb_ipc_one_way *one_way)
{
	int32_t *fc;
	fc = qb_rb_shared_user_data_get(one_way->u.shm.rb);
	return *fc;
}

static ssize_t qb_ipc_shm_q_len_get(struct qb_ipc_one_way *one_way)
{
	if (one_way->u.shm.rb == NULL) {
		return -ENOTCONN;
	}
	return qb_rb_chunks_used(one_way->u.shm.rb);
}

int32_t qb_ipcc_shm_connect(struct qb_ipcc_connection *c,
			    struct qb_ipc_connection_response *response)
{
	int32_t res = 0;

	c->funcs.send = qb_ipc_shm_send;
	c->funcs.sendv = qb_ipc_shm_sendv;
	c->funcs.recv = qb_ipc_shm_recv;
	c->funcs.fc_get = qb_ipc_shm_fc_get;
	c->funcs.disconnect = qb_ipcc_shm_disconnect;
	c->needs_sock_for_poll = QB_TRUE;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		errno = EINVAL;
		return -errno;
	}

	c->request.u.shm.rb = qb_rb_open(response->request, c->request.max_msg_size,
					 QB_RB_FLAG_SHARED_PROCESS,
					 sizeof(int32_t));
	if (c->request.u.shm.rb == NULL) {
		res = -errno;
		goto return_error;
	}
	c->response.u.shm.rb = qb_rb_open(response->response,
					  c->response.max_msg_size,
					  QB_RB_FLAG_SHARED_PROCESS, 0);

	if (c->response.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}
	c->event.u.shm.rb = qb_rb_open(response->event,
				       c->response.max_msg_size,
				       QB_RB_FLAG_SHARED_PROCESS, 0);

	if (c->event.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:EVENT");
		goto cleanup_request_response;
	}
	return 0;

 cleanup_request_response:
	qb_rb_close(c->response.u.shm.rb);

 cleanup_request:
	qb_rb_close(c->request.u.shm.rb);

 return_error:
	qb_util_log(LOG_ERR, "connection failed %s",
		    strerror(-res));

	return res;
}

/*
 * service functions
 * --------------------------------------------------------
 */

static void qb_ipcs_shm_disconnect(struct qb_ipcs_connection *c)
{
	struct qb_ipc_response_header msg;
	int32_t peer_alive = QB_TRUE;

	if (c->setup.u.us.sock == -1) {
		peer_alive = QB_FALSE;
	}

	msg.id = QB_IPC_MSG_DISCONNECT;
	msg.size = sizeof(msg);
	msg.error = 0;

	if (c->response.u.shm.rb) {
		qb_rb_close(c->response.u.shm.rb);
		c->response.u.shm.rb = NULL;
	}
	if (c->event.u.shm.rb) {
		qb_rb_close(c->event.u.shm.rb);
		c->event.u.shm.rb = NULL;
	}
	if (c->request.u.shm.rb) {
		qb_rb_close(c->request.u.shm.rb);
		c->request.u.shm.rb = NULL;
	}
}

static int32_t qb_ipcs_shm_connect(struct qb_ipcs_service *s,
				   struct qb_ipcs_connection *c,
				   struct qb_ipc_connection_response *r)
{
	int32_t res;

	qb_util_log(LOG_DEBUG, "connecting to client [%d]", c->pid);

	snprintf(r->request, NAME_MAX, "qb-%s-request-%d-%d", s->name, c->pid, c->setup.u.us.sock);
	snprintf(r->response, NAME_MAX, "qb-%s-response-%d-%d", s->name, c->pid, c->setup.u.us.sock);
	snprintf(r->event, NAME_MAX, "qb-%s-event-%d-%d", s->name, c->pid, c->setup.u.us.sock);

	c->request.u.shm.rb = qb_rb_open(r->request,
					 c->request.max_msg_size,
					 QB_RB_FLAG_CREATE |
					 QB_RB_FLAG_SHARED_PROCESS,
					 sizeof(int32_t));
	if (c->request.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:REQUEST");
		goto cleanup;
	}
	res = qb_rb_chown(c->request.u.shm.rb, c->euid, c->egid);

	c->response.u.shm.rb = qb_rb_open(r->response,
					  c->response.max_msg_size,
					  QB_RB_FLAG_CREATE |
					  QB_RB_FLAG_SHARED_PROCESS, 0);
	if (c->response.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}
	res = qb_rb_chown(c->response.u.shm.rb, c->euid, c->egid);

	c->event.u.shm.rb = qb_rb_open(r->event,
				       c->event.max_msg_size,
				       QB_RB_FLAG_CREATE |
				       QB_RB_FLAG_SHARED_PROCESS, 0);

	if (c->event.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:EVENT");
		goto cleanup_request_response;
	}
	res = qb_rb_chown(c->event.u.shm.rb, c->euid, c->egid);

	r->hdr.error = 0;
	return 0;

cleanup_request_response:
	qb_rb_close(c->request.u.shm.rb);

cleanup_request:
	qb_rb_close(c->response.u.shm.rb);

cleanup:
	r->hdr.error = res;
	qb_util_log(LOG_ERR, "shm connection FAILED [%s]",
		    strerror(-res));

	return res;
}

void qb_ipcs_shm_init(struct qb_ipcs_service *s)
{
	s->funcs.connect = qb_ipcs_shm_connect;
	s->funcs.disconnect = qb_ipcs_shm_disconnect;

	s->funcs.recv = qb_ipc_shm_recv;
	s->funcs.peek = qb_ipc_shm_peek;
	s->funcs.reclaim = qb_ipc_shm_reclaim;
	s->funcs.send = qb_ipc_shm_send;
	s->funcs.sendv = qb_ipc_shm_sendv;

	s->funcs.fc_set = qb_ipc_shm_fc_set;
	s->funcs.q_len_get = qb_ipc_shm_q_len_get;

	s->needs_sock_for_poll = QB_TRUE;
}
