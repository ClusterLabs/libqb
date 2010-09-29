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

#include <sys/ipc.h>
#include <sys/msg.h>

#include <qb/qbpoll.h>
#include "ipc_int.h"
#include "util_int.h"
#ifndef MSGMAX
#define MSGMAX  8192
#endif
#define MY_DATA_SIZE 8000
struct my_msgbuf {
	int32_t id __attribute__ ((aligned(8)));
	char data[MY_DATA_SIZE] __attribute__ ((aligned(8)));
} __attribute__ ((aligned(8)));


static ssize_t qb_ipcs_smq_event_send(struct qb_ipcs_connection *c,
					 void *data, size_t size);

/*
 * utility functions
 * --------------------------------------------------------
 */
static int32_t sysv_mq_create(struct qb_ipcs_service *s)
{
	struct msqid_ds info;
	int32_t res = 0;

	s->u.smq.q = msgget(s->u.smq.key, IPC_CREAT | O_EXCL | IPC_NOWAIT);
	if (s->u.smq.q == -1) {
		res = -errno;
		perror("msgget:REQUEST");
		return res;
	}

	res = msgctl(s->u.smq.q, IPC_STAT, &info);
	if (res != 0) {
		res = -errno;
		perror("msgctl");
		qb_util_log(LOG_ERR, "error getting mq info");
		return res;
	}

	info.msg_qbytes = 10 * s->max_msg_size;
	res = msgctl(s->u.smq.q, IPC_SET, &info);
	if (res != 0) {
		res = -errno;
		perror("msgctl");
		qb_util_log(LOG_ERR, "error changing msg_qbytes to %zu",
			    10 * s->max_msg_size);
	}
	return res;
}

static int32_t sysv_mq_unnamed_create(struct qb_ipcc_smq_one_way *queue)
{
retry_creating_the_q:
	queue->key = random();
	queue->q = msgget(queue->key, IPC_CREAT | IPC_EXCL | IPC_NOWAIT);
	if (queue->q == -1 && errno == EEXIST) {
		goto retry_creating_the_q;
	} else if (queue->q == -1) {
		return -errno;
	}
	return 0;
}

static key_t sysv_key_from_name(const char *name)
{
	char key_location[PATH_MAX];

	snprintf(key_location, PATH_MAX, "/tmp/qb_%s.smq", name);

	return ftok(key_location, 0);
}

#define PACK_MESSAGES 1

static int32_t sysv_send(mqd_t q,
			const void *msg_ptr, size_t msg_len)
{
	int32_t res;
	int32_t sent = 0;
#ifdef PACK_MESSAGES
	char *progress = (char*)msg_ptr;
	struct my_msgbuf buf;
	size_t to_send_now; /* to send in this message */
	size_t to_send_next; /* to send in next message */

	do {
		to_send_now = QB_MIN(msg_len - sent, MY_DATA_SIZE);
		to_send_next = msg_len - (sent + to_send_now);
		/* receiver used the ID to check to see if there
		 * is more to recieve for this message.
		 */
		buf.id = to_send_next + 1;
		memcpy(buf.data, progress, to_send_now);
		res = msgsnd(q, &buf, to_send_now, IPC_NOWAIT);
		if (res == 0) {
			sent += to_send_now;
			progress += to_send_now;
		} else {
			goto return_status;
		}

	} while (sent < msg_len);

 return_status:
#else
	res = msgsnd(q, msg_ptr, msg_len, IPC_NOWAIT);
	sent = msg_len;
#endif
	if (res == -1) {
		return -errno;
	}
	return sent;
}

static ssize_t sysv_recv(mqd_t q, void *msg_ptr, size_t msg_len)
{
	ssize_t res;
	ssize_t received = 0;
#ifdef PACK_MESSAGES
	char *progress = (char*)msg_ptr;
	struct my_msgbuf buf;

	do {
 try_again:
		res = msgrcv(q, &buf,
			MY_DATA_SIZE, 0, IPC_NOWAIT);

		if (res == -1 && errno == ENOMSG) {
			goto try_again;
		}
		//printf("res:%zd, ID:%d\n", res, buf.id);
		if (res == -1) {
			goto return_status;
		}
		memcpy(progress, buf.data, res);
		received += res;
		progress += res;
	} while (buf.id > 1);
 return_status:
#else
	res = msgrcv(q, msg_ptr, msg_len,
		0, IPC_NOWAIT);
	received = res;
#endif
	if (res == -1 && errno == ENOMSG) {
		/* just to be consistent with other IPC types.
		 */
		return -EAGAIN;
	}
	if (res == -1) {
		perror(__func__);
		return -errno;
	}
	return received;
}


/*
 * client functions
 * --------------------------------------------------------
 */
static int32_t qb_ipcc_smq_send(struct qb_ipcc_connection *c,
				const void *msg_ptr, size_t msg_len)
{
	return sysv_send(c->u.smq.request.q, msg_ptr, msg_len);
}

static ssize_t qb_ipcc_smq_recv(struct qb_ipcc_connection *c,
				void *msg_ptr, size_t msg_len)
{
	return sysv_recv(c->u.smq.response.q, msg_ptr, msg_len);
}

static void qb_ipcc_smq_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_request_header hdr;

	qb_util_log(LOG_DEBUG, "%s()\n", __func__);
	if (c->needs_sock_for_poll) {
		return;
	}

	hdr.id = QB_IPC_MSG_DISCONNECT;
	hdr.session_id = c->session_id;
	hdr.size = sizeof(hdr);
	sysv_send(c->u.smq.request.q, (const char *)&hdr, hdr.size);

	msgctl(c->u.smq.event.q, IPC_RMID, NULL);
	msgctl(c->u.smq.response.q, IPC_RMID, NULL);
}

static int32_t _smq_connect_to_service_(struct qb_ipcc_connection *c)
{
	int32_t res;
	ssize_t size;
	int32_t waited;
	struct mar_req_smq_setup start;
	struct mar_res_setup *msg_res;

	start.hdr.id = QB_IPC_MSG_CONNECT;
	start.hdr.session_id = c->session_id;
	start.pid = getpid();
	start.hdr.size = sizeof(struct mar_req_smq_setup);
	start.response_key = c->u.smq.response.key;
	start.event_key = c->u.smq.event.key;

	if (c->needs_sock_for_poll) {
		qb_ipc_us_send(c->sock, &start, 1);
	}
	res = sysv_send(c->u.smq.request.q, (const char *)&start,
		     start.hdr.size);

	if (res == -1) {
		perror("sysv_send");
		return res;
	}
	qb_util_log(LOG_DEBUG, "sent request to server %d\n", res);

	waited = 0;
mq_recv_again:
	size = sysv_recv(c->u.smq.response.q, c->receive_buf,
		      c->max_msg_size);

	if ((size == -EAGAIN || size == -ENOMSG) && waited < 10) {
		usleep(100000);
		waited++;
		goto mq_recv_again;
	}
	if (size < 0) {
		perror("sysv_recv");
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

int32_t qb_ipcc_smq_connect(struct qb_ipcc_connection * c)
{
	int32_t res;

	c->funcs.send = qb_ipcc_smq_send;
	c->funcs.recv = qb_ipcc_smq_recv;
	c->funcs.disconnect = qb_ipcc_smq_disconnect;
	c->type = QB_IPC_SYSV_MQ;
	c->needs_sock_for_poll = QB_TRUE;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		return -EINVAL;
	}

	/* Connect to the service's request message queue.
	 */
	c->u.smq.request.key = sysv_key_from_name(c->name);
	if (c->u.smq.request.key == -1) {
		res = -errno;
		perror("ftok:REQUEST");
		return res;
	}
	c->u.smq.request.q = msgget(c->u.smq.request.key, IPC_NOWAIT);
	if (c->u.smq.request.q == -1) {
		res = -errno;
		perror("msgget:REQUEST");
		return res;
	}

	/* Create the response message queue.
	 */
	res = sysv_mq_unnamed_create(&c->u.smq.response);
	if (res < 0) {
		perror("msgget:RESPONSE");
		goto cleanup_request;
	}

	/* Create the event message queue.
	 */
	res = sysv_mq_unnamed_create(&c->u.smq.event);
	if (res < 0) {
		perror("msgget:event");
		goto cleanup_request_response;
	}

	res = _smq_connect_to_service_(c);
	if (res == 0) {
		return 0;
	}

	msgctl(c->u.smq.event.q, IPC_RMID, NULL);

cleanup_request_response:
	msgctl(c->u.smq.response.q, IPC_RMID, NULL);

cleanup_request:

	return res;
}

/*
 * service functions
 * --------------------------------------------------------
 */
static void qb_ipcs_smq_disconnect(struct qb_ipcs_connection *c)
{
	struct qb_ipc_response_header msg;

	if (c->service->needs_sock_for_poll) {
		return;
	}

	msg.id = QB_IPC_MSG_DISCONNECT;
	msg.size = sizeof(msg);
	msg.error = 0;

	qb_ipcs_smq_event_send(c, &msg, msg.size);
}

static void qb_ipcs_smq_destroy(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_list_head *iter;
	struct qb_list_head *iter_next;

	qb_util_log(LOG_DEBUG, "%s\n", __func__);

	for (iter = s->connections.next;
	     iter != &s->connections; iter = iter_next) {

		iter_next = iter->next;

		c = qb_list_entry(iter, struct qb_ipcs_connection, list);
		if (c == NULL) {
			continue;
		}
		qb_ipcs_disconnect(c);
	}

	if (msgctl(s->u.smq.q, IPC_RMID, NULL) == -1)
		perror("msgctl:RMID");
}

static int32_t qb_ipcs_smq_connect(struct qb_ipcs_service *s,
				   struct qb_ipcs_connection *c, void *data,
				   size_t size)
{
	int32_t res = 0;
	struct mar_req_smq_setup *init = (struct mar_req_smq_setup *)data;
	struct mar_res_setup accept_msg;

	c->pid = init->pid;
	c->service = s;

	accept_msg.hdr.id = QB_IPC_MSG_CONNECT;
	accept_msg.hdr.size = sizeof(struct mar_res_setup);

	/* setup the response message queue
	 */
	c->u.smq.response.key = init->response_key;
	c->u.smq.response.q = msgget(c->u.smq.response.key, IPC_NOWAIT);
	if (c->u.smq.response.q == -1) {
		res = -errno;
		perror("msgget:RESPONSE");
		goto cleanup;
	}

	/* setup the event message queue
	 */
	c->u.smq.event.key = init->event_key;
	c->u.smq.event.q = msgget(c->u.smq.event.key, IPC_NOWAIT);
	if (c->u.smq.event.q == -1) {
		res = -errno;
		perror("msgget:event");
		goto cleanup_response;
	}

	/* send the "connection accepted" message back.
	 */
	accept_msg.hdr.error = 0;
	accept_msg.max_msg_size = s->max_msg_size;

	res = sysv_send(c->u.smq.response.q, (const char *)&accept_msg,
		     sizeof(struct mar_res_setup));
	if (res < 0) {
		perror("sysv_send:RESPONSE");
		goto cleanup_response;
	}

	return 0;

cleanup_response:
	accept_msg.hdr.error = res;
	sysv_send(c->u.smq.response.q, (const char *)&accept_msg,
		     sizeof(struct mar_res_setup));

cleanup:
	return res;
}

#if 0
static int32_t qb_ipcs_smq_is_msg_ready(struct qb_ipcs_service *s)
{
	struct msqid_ds info;
	int32_t res = msgctl(s->u.smq.q, IPC_STAT, &info);
	if (res == 0) {
		return info.msg_qnum;
	} else {
		perror("is_msg_ready");
	}

	return -errno;
}
#endif
static ssize_t qb_ipcs_smq_request_recv(struct qb_ipcs_service *s, void *buf,
					size_t buf_size)
{
	return sysv_recv(s->u.q, buf, buf_size);
}

static ssize_t qb_ipcs_smq_response_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	return sysv_send(c->u.smq.response.q, (const char *)data, size);
}

static ssize_t qb_ipcs_smq_event_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	return sysv_send(c->u.smq.event.q, (const char *)data, size);
}

int32_t qb_ipcs_smq_create(struct qb_ipcs_service * s)
{
	int32_t fd;
	char key_location[PATH_MAX];

	snprintf(key_location, PATH_MAX, "/tmp/qb_%s.smq", s->name);

	fd = creat(key_location, 0600);
	s->u.smq.key = ftok(key_location, 0);

	s->funcs.destroy = qb_ipcs_smq_destroy;
	s->funcs.connect = qb_ipcs_smq_connect;
	s->funcs.disconnect = qb_ipcs_smq_disconnect;
	s->funcs.response_send = qb_ipcs_smq_response_send;
	s->funcs.request_recv = qb_ipcs_smq_request_recv;
	s->needs_sock_for_poll = QB_TRUE;

	//s->max_msg_size = MSGMAX;

	return sysv_mq_create(s);
}
