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
#include <qb/qbpoll.h>
#include <qb/qbrb.h>

static ssize_t qb_ipcs_shm_event_send(struct qb_ipcs_connection *c,
				      void *data, size_t size);

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
	qb_rb_close(c->request.shm.rb);
	qb_rb_close(c->response.shm.rb);
	qb_rb_close(c->event.shm.rb);
}

static int32_t qb_ipcc_shm_send(struct qb_ipcc_connection *c,
				const void *msg_ptr, size_t msg_len)
{
	return qb_rb_chunk_write(c->request.shm.rb, msg_ptr, msg_len);
}

static ssize_t qb_ipcc_shm_recv(struct qb_ipcc_connection *c,
				void *msg_ptr, size_t msg_len)
{
	ssize_t res =
	    qb_rb_chunk_read(c->response.shm.rb, (void *)msg_ptr, msg_len, 0);
	if (res == -ETIMEDOUT) {
		return -EAGAIN;
	}
	return res;
}

static ssize_t qb_ipcc_shm_event_recv(struct qb_ipcc_connection *c,
				      void **data_out, int32_t timeout)
{
	ssize_t res = qb_rb_chunk_peek(c->event.shm.rb, data_out, timeout);
	if (res == -ETIMEDOUT) {
		return -EAGAIN;
	}
	return res;
}

static void qb_ipcc_shm_event_release(struct qb_ipcc_connection *c)
{
	qb_rb_chunk_reclaim(c->event.shm.rb);
}

int32_t qb_ipcc_shm_connect(struct qb_ipcc_connection *c,
			    struct qb_ipc_connection_response *response)
{
	int32_t res = 0;

	c->funcs.send = qb_ipcc_shm_send;
	c->funcs.recv = qb_ipcc_shm_recv;
	c->funcs.event_recv = qb_ipcc_shm_event_recv;
	c->funcs.event_release = qb_ipcc_shm_event_release;
	c->funcs.disconnect = qb_ipcc_shm_disconnect;
	c->needs_sock_for_poll = QB_TRUE;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		errno = EINVAL;
		return -errno;
	}

	c->request.shm.rb = qb_rb_open(response->request, c->max_msg_size,
				       QB_RB_FLAG_SHARED_PROCESS);
	if (c->request.shm.rb == NULL) {
		perror("qb_rb_open:REQUEST");
		return -errno;
	}
	c->response.shm.rb = qb_rb_open(response->response,
					c->max_msg_size,
					QB_RB_FLAG_SHARED_PROCESS);

	if (c->response.shm.rb == NULL) {
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}
	c->event.shm.rb = qb_rb_open(response->event,
				     c->max_msg_size,
				     QB_RB_FLAG_SHARED_PROCESS);

	if (c->event.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:EVENT");
		goto cleanup_request_response;
	}
	return 0;

cleanup_request_response:
	qb_rb_close(c->response.shm.rb);

cleanup_request:
	qb_rb_close(c->request.shm.rb);

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

	if (c->response.shm.rb) {
		qb_ipcs_shm_event_send(c, &msg, msg.size);
		qb_rb_close(c->response.shm.rb);
	}
	if (c->event.shm.rb) {
		qb_rb_close(c->event.shm.rb);
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
	c->request.shm.rb = qb_rb_open(r->request,
				       c->max_msg_size,
				       QB_RB_FLAG_CREATE |
				       QB_RB_FLAG_SHARED_PROCESS);
	if (c->request.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:REQUEST");
		goto cleanup;
	}
	res = qb_rb_chown(c->request.shm.rb, c->euid, c->egid);

	c->response.shm.rb = qb_rb_open(r->response,
					c->max_msg_size,
					QB_RB_FLAG_CREATE |
					QB_RB_FLAG_SHARED_PROCESS);
	if (c->response.shm.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}
	res = qb_rb_chown(c->response.shm.rb, c->euid, c->egid);

	c->event.shm.rb = qb_rb_open(r->event,
				     c->max_msg_size,
				     QB_RB_FLAG_CREATE |
				     QB_RB_FLAG_SHARED_PROCESS);

	if (c->event.shm.rb == NULL) {
		res = -errno;
		perror("mq_open:EVENT");
		goto cleanup_request_response;
	}
	res = qb_rb_chown(c->event.shm.rb, c->euid, c->egid);

	r->hdr.error = 0;
	return 0;

cleanup_request_response:
	qb_rb_close(c->request.shm.rb);

cleanup_request:
	qb_rb_close(c->response.shm.rb);

cleanup:
	r->hdr.error = res;

	return res;
}

static ssize_t qb_ipcs_shm_request_recv(struct qb_ipcs_connection *c, void *buf,
					size_t buf_size)
{
	int32_t res = qb_rb_chunk_read(c->request.shm.rb, buf, buf_size, 0);
	if (res == -ETIMEDOUT) {
		return -EAGAIN;
	}
	return res;
}

static ssize_t qb_ipcs_shm_response_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	return qb_rb_chunk_write(c->response.shm.rb, (const char *)data, size);
}

static ssize_t qb_ipcs_shm_event_send(struct qb_ipcs_connection *c,
				      void *data, size_t size)
{
	return qb_rb_chunk_write(c->event.shm.rb, (const char *)data, size);
}

int32_t qb_ipcs_shm_create(struct qb_ipcs_service *s)
{
	s->funcs.destroy = qb_ipcs_shm_destroy;
	s->funcs.request_recv = qb_ipcs_shm_request_recv;
	s->funcs.response_send = qb_ipcs_shm_response_send;
	s->funcs.connect = qb_ipcs_shm_connect;
	s->funcs.disconnect = qb_ipcs_shm_disconnect;
	s->funcs.event_send = qb_ipcs_shm_event_send;
	s->needs_sock_for_poll = QB_TRUE;
	return 0;
}
