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
#include <qb/qbpoll.h>
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
				size_t msg_len)
{
	ssize_t res = qb_rb_chunk_read(one_way->u.shm.rb,
				       (void *)msg_ptr,
				       msg_len,
				       0);
	if (res == -ETIMEDOUT) {
		return -EAGAIN;
	}
	return res;
}

int32_t qb_ipcc_shm_connect(struct qb_ipcc_connection *c,
			    struct qb_ipc_connection_response *response)
{
	int32_t res = 0;

	c->funcs.send = qb_ipc_shm_send;
	c->funcs.sendv = qb_ipc_shm_sendv;
	c->funcs.recv = qb_ipc_shm_recv;
	c->funcs.disconnect = qb_ipcc_shm_disconnect;
	c->needs_sock_for_poll = QB_TRUE;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		errno = EINVAL;
		return -errno;
	}

	c->request.u.shm.rb = qb_rb_open(response->request, c->request.max_msg_size,
				       QB_RB_FLAG_SHARED_PROCESS);
	if (c->request.u.shm.rb == NULL) {
		perror("qb_rb_open:REQUEST");
		return -errno;
	}
	c->response.u.shm.rb = qb_rb_open(response->response,
					c->response.max_msg_size,
					QB_RB_FLAG_SHARED_PROCESS);

	if (c->response.u.shm.rb == NULL) {
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}
	c->event.u.shm.rb = qb_rb_open(response->event,
				     c->response.max_msg_size,
				     QB_RB_FLAG_SHARED_PROCESS);

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

	qb_util_log(LOG_DEBUG, "connection failed %d\n", res);

	return res;
}

/*
 * service functions
 * --------------------------------------------------------
 */

static void qb_ipcs_shm_disconnect(struct qb_ipcs_connection *c)
{
	struct qb_ipc_response_header msg;

	msg.id = QB_IPC_MSG_DISCONNECT;
	msg.size = sizeof(msg);
	msg.error = 0;

	if (c->response.u.shm.rb) {
		qb_ipc_shm_send(&c->event, &msg, msg.size);
		qb_rb_close(c->response.u.shm.rb);
	}
	if (c->event.u.shm.rb) {
		qb_rb_close(c->event.u.shm.rb);
	}
}

static void qb_ipcs_shm_destroy(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_list_head *iter;
	struct qb_list_head *iter_next;

	qb_util_log(LOG_DEBUG, "destroying server\n");

	for (iter = s->connections.next;
	     iter != &s->connections; iter = iter_next) {

		iter_next = iter->next;

		c = qb_list_entry(iter, struct qb_ipcs_connection, list);
		if (c == NULL) {
			continue;
		}
		qb_ipcs_disconnect(c);
	}
}

static int32_t qb_ipcs_shm_connect(struct qb_ipcs_service *s,
				   struct qb_ipcs_connection *c,
				   struct qb_ipc_connection_response *r)
{
	int32_t res;

	qb_util_log(LOG_DEBUG, "connecting to client [%d]\n", c->pid);

	snprintf(r->request, NAME_MAX, "%s-request-%d", s->name, c->pid);
	snprintf(r->response, NAME_MAX, "%s-response-%d", s->name, c->pid);
	snprintf(r->event, NAME_MAX, "%s-event-%d", s->name, c->pid);

	qb_util_log(LOG_DEBUG, "rb_open:%s", r->request);
	c->request.u.shm.rb = qb_rb_open(r->request,
				       c->request.max_msg_size,
				       QB_RB_FLAG_CREATE |
				       QB_RB_FLAG_SHARED_PROCESS);
	if (c->request.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:REQUEST");
		goto cleanup;
	}
	res = qb_rb_chown(c->request.u.shm.rb, c->euid, c->egid);

	c->response.u.shm.rb = qb_rb_open(r->response,
					c->response.max_msg_size,
					QB_RB_FLAG_CREATE |
					QB_RB_FLAG_SHARED_PROCESS);
	if (c->response.u.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}
	res = qb_rb_chown(c->response.u.shm.rb, c->euid, c->egid);

	c->event.u.shm.rb = qb_rb_open(r->event,
				     c->event.max_msg_size,
				     QB_RB_FLAG_CREATE |
				     QB_RB_FLAG_SHARED_PROCESS);

	if (c->event.u.shm.rb == NULL) {
		res = -errno;
		perror("mq_open:EVENT");
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

	return res;
}

int32_t qb_ipcs_shm_create(struct qb_ipcs_service *s)
{
	s->funcs.destroy = qb_ipcs_shm_destroy;
	s->funcs.recv = qb_ipc_shm_recv;
	s->funcs.send = qb_ipc_shm_send;
	s->funcs.sendv = qb_ipc_shm_sendv;
	s->funcs.connect = qb_ipcs_shm_connect;
	s->funcs.disconnect = qb_ipcs_shm_disconnect;
	s->needs_sock_for_poll = QB_TRUE;
	return 0;
}
