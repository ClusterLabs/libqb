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

static ssize_t qb_ipcs_shm_dispatch_send(struct qb_ipcs_connection *c,
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
	qb_rb_close(c->u.shm.request.rb);
	qb_rb_close(c->u.shm.response.rb);
	qb_rb_close(c->u.shm.dispatch.rb);
}

static int32_t qb_ipcc_shm_send(struct qb_ipcc_connection *c,
				const void *msg_ptr, size_t msg_len)
{
	return qb_rb_chunk_write(c->u.shm.request.rb, msg_ptr, msg_len);
}

static ssize_t qb_ipcc_shm_recv(struct qb_ipcc_connection *c,
				void *msg_ptr, size_t msg_len)
{
	ssize_t res =
	    qb_rb_chunk_read(c->u.shm.response.rb, (void *)msg_ptr, msg_len, 0);
	if (res == -ETIMEDOUT) {
		return -EAGAIN;
	}
	return res;
}

static int32_t _ipcc_shm_connect_to_service_(struct qb_ipcc_connection *c)
{
	int32_t res;
	ssize_t size;
	struct mar_req_shm_setup start;
	struct mar_res_setup *msg_res;

	start.hdr.id = QB_IPC_MSG_CONNECT;
	start.hdr.session_id = c->session_id;
	start.pid = getpid();
	start.hdr.size = sizeof(struct mar_req_shm_setup);
	strcpy(start.response, qb_rb_name_get(c->u.shm.response.rb));
	strcpy(start.dispatch, qb_rb_name_get(c->u.shm.dispatch.rb));

	c->needs_sock_for_poll = QB_TRUE;

	res = qb_rb_chunk_write(c->u.shm.request.rb,
				(const char *)&start,
				start.hdr.size);
	if (res < 0) {
		return res;
	}

	if (c->needs_sock_for_poll) {
		qb_ipc_us_send(c->sock, &start, 1);
	}
	qb_util_log(LOG_DEBUG, "sent request to server %d\n", res);

	size = qb_rb_chunk_read(c->u.shm.response.rb, c->receive_buf,
				c->max_msg_size, 100000);

	if (size < 0) {
		res = size;
		perror("_ipcc_shm_connect_to_service_:qb_rb_chunk_read");
		goto cleanup;
	}
	qb_util_log(LOG_DEBUG, "received response from server %zd\n", size);
	msg_res = (struct mar_res_setup *)c->receive_buf;
	res = msg_res->hdr.error;
	if (res == 0) {
		c->max_msg_size = msg_res->max_msg_size;
	}

cleanup:
	return res;
}

int32_t qb_ipcc_shm_connect(struct qb_ipcc_connection * c)
{
	int32_t res = 0;

	c->funcs.send = qb_ipcc_shm_send;
	c->funcs.recv = qb_ipcc_shm_recv;
	c->funcs.disconnect = qb_ipcc_shm_disconnect;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		errno = EINVAL;
		return -1;
	}

	/* Connect to the service's request message queue.
	 */
	c->u.shm.request.rb = qb_rb_open(c->name, c->max_msg_size,
					 QB_RB_FLAG_SHARED_PROCESS);
	if (c->u.shm.request.rb == NULL) {
		perror("qb_rb_open:REQUEST");
		return -1;
	}

	/* Create the response message queue.
	 */
	res = snprintf(c->u.shm.response.name,
		       NAME_MAX, "%s-response-%d", c->name, getpid());

	c->u.shm.response.rb = qb_rb_open(c->u.shm.response.name,
					  c->max_msg_size,
					  QB_RB_FLAG_CREATE |
					  QB_RB_FLAG_SHARED_PROCESS);

	if (c->u.shm.response.rb == NULL) {
		perror("qb_rb_open:RESPONSE");
		goto cleanup_request;
	}

	res =
	    snprintf(c->u.shm.dispatch.name, NAME_MAX, "%s-dispatch-%d",
		     c->name, getpid());

	c->u.shm.dispatch.rb = qb_rb_open(c->u.shm.dispatch.name,
					  c->max_msg_size,
					  QB_RB_FLAG_CREATE |
					  QB_RB_FLAG_SHARED_PROCESS);

	if (c->u.shm.dispatch.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:DISPATCH");
		goto cleanup_request_response;
	}

	res = _ipcc_shm_connect_to_service_(c);
	if (res == 0) {
		return 0;
	}

	qb_util_log(LOG_DEBUG, "connection failed %d\n", res);

	qb_rb_close(c->u.shm.dispatch.rb);

cleanup_request_response:
	qb_rb_close(c->u.shm.response.rb);

cleanup_request:
	qb_rb_close(c->u.shm.request.rb);

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

	if (c->u.shm.response.rb) {
		qb_ipcs_shm_dispatch_send(c, &msg, msg.size);
		qb_rb_close(c->u.shm.response.rb);
	}
	if (c->u.shm.dispatch.rb) {
		qb_rb_close(c->u.shm.dispatch.rb);
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

	qb_rb_close(s->u.rb);
}

static int32_t qb_ipcs_shm_connect(struct qb_ipcs_service *s,
				   struct qb_ipcs_connection *c, void *data,
				   size_t size)
{
	int32_t res;
	struct mar_req_shm_setup *init = (struct mar_req_shm_setup *)data;
	struct mar_res_setup accept_msg;

	c->pid = init->pid;
	c->service = s;
	qb_util_log(LOG_DEBUG, "connecting to client [%d]\n", c->pid);

	/* setup the response message queue
	 */
	strcpy(c->u.shm.response.name, init->response);
	qb_util_log(LOG_DEBUG, "%s:%s", __func__, c->u.shm.response.name);
	c->u.shm.response.rb = qb_rb_open(c->u.shm.response.name,
					  s->max_msg_size,
					  QB_RB_FLAG_SHARED_PROCESS);
	if (c->u.shm.response.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:RESPONSE");
		return res;
	}

	/* setup the dispatch message queue
	 */
	strcpy(c->u.shm.dispatch.name, init->dispatch);
	qb_util_log(LOG_DEBUG, "%s:%s", __func__, c->u.shm.dispatch.name);
	c->u.shm.dispatch.rb = qb_rb_open(c->u.shm.dispatch.name,
					  s->max_msg_size,
					  QB_RB_FLAG_SHARED_PROCESS);

	if (c->u.shm.dispatch.rb == NULL) {
		res = -errno;
		perror("mq_open:DISPATCH");
		goto cleanup_response;
	}

	/* send the "connection accepted" message back.
	 */
	accept_msg.hdr.id = QB_IPC_MSG_CONNECT;
	accept_msg.hdr.size = sizeof(struct mar_res_setup);
	accept_msg.hdr.error = 0;
	accept_msg.max_msg_size = s->max_msg_size;

	qb_util_log(LOG_DEBUG, "%s:sending response", __func__);
	res = qb_rb_chunk_write(c->u.shm.response.rb, (const char *)&accept_msg,
				sizeof(struct mar_res_setup));
	if (res < 0) {
		res = -errno;
		perror("qb_rb_chunk_write:RESPONSE");
		goto cleanup_response;
	}

	return 0;

cleanup_response:
	accept_msg.hdr.error = res;
	qb_rb_chunk_write(c->u.shm.response.rb, (const char *)&accept_msg,
			  sizeof(struct mar_res_setup));
	qb_rb_close(c->u.shm.response.rb);

	return res;
}

static ssize_t qb_ipcs_shm_request_recv(struct qb_ipcs_service *s, void *buf,
					size_t buf_size)
{
	int32_t res = qb_rb_chunk_read(s->u.rb, buf, buf_size, 0);
	if (res == -ETIMEDOUT) {
		return -EAGAIN;
	}
	return res;
}

static ssize_t qb_ipcs_shm_response_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	return qb_rb_chunk_write(c->u.shm.response.rb, (const char *)data,
				 size);
}

static ssize_t qb_ipcs_shm_dispatch_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	return qb_rb_chunk_write(c->u.shm.dispatch.rb, (const char *)data,
				 size);
}

int32_t qb_ipcs_shm_create(struct qb_ipcs_service *s)
{
	int32_t res = 0;

	s->funcs.destroy = qb_ipcs_shm_destroy;
	s->funcs.request_recv = qb_ipcs_shm_request_recv;
	s->funcs.response_send = qb_ipcs_shm_response_send;
	s->funcs.connect = qb_ipcs_shm_connect;
	s->funcs.disconnect = qb_ipcs_shm_disconnect;
	s->needs_sock_for_poll = QB_TRUE;

	s->u.rb = qb_rb_open(s->name, s->max_msg_size,
			     QB_RB_FLAG_CREATE | QB_RB_FLAG_SHARED_PROCESS);
	if (s->u.rb == NULL) {
		res = -errno;
		perror("qb_rb_open:REQUEST");
		return res;
	}

	qb_util_log(LOG_DEBUG, "%s()", __func__);
	return res;
}
