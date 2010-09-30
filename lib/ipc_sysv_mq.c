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
static int32_t sysv_mq_unnamed_create(struct qb_ipcs_connection *c,
				      union qb_ipc_one_way *queue)
{
	struct msqid_ds info;
	int32_t res = 0;

retry_creating_the_q:
	queue->smq.key = random();
	queue->smq.q =
	    msgget(queue->smq.key,
		   IPC_CREAT | IPC_EXCL | IPC_NOWAIT | S_IWUSR | S_IRUSR);
	if (queue->smq.q == -1 && errno == EEXIST) {
		goto retry_creating_the_q;
	} else if (queue->smq.q == -1) {
		return -errno;
	}

	/*
	 * change the queue size and change the ownership to that of
	 * the client so they can access it.
	 */
	res = msgctl(queue->smq.q, IPC_STAT, &info);
	if (res != 0) {
		res = -errno;
		qb_util_log(LOG_ERR, "error getting sysv-mq info : %s",
			    strerror(errno));
		return res;
	}

	if (info.msg_perm.uid != 0) {
		qb_util_log(LOG_WARNING,
			    "not enough privileges to increase msg_qbytes");
		return res;
	}
	info.msg_qbytes = 2 * c->max_msg_size;
	info.msg_perm.uid = c->euid;
	info.msg_perm.gid = c->egid;

	res = msgctl(queue->smq.q, IPC_SET, &info);
	if (res != 0) {
		res = -errno;
		qb_util_log(LOG_ERR,
			    "error modifing the SYSV message queue : %s",
			    strerror(errno));
		return res;
	}

	return 0;
}

static int32_t sysv_send(mqd_t q, const void *msg_ptr, size_t msg_len)
{
	int32_t res;
	int32_t sent = 0;
#ifdef PACK_MESSAGES
	char *progress = (char *)msg_ptr;
	struct my_msgbuf buf;
	size_t to_send_now;	/* to send in this message */
	size_t to_send_next;	/* to send in next message */

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
	char *progress = (char *)msg_ptr;
	struct my_msgbuf buf;

	do {
try_again:
		res = msgrcv(q, &buf, MY_DATA_SIZE, 0, IPC_NOWAIT);

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
	res = msgrcv(q, msg_ptr, msg_len, 0, IPC_NOWAIT);
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
	return sysv_send(c->request.smq.q, msg_ptr, msg_len);
}

static ssize_t qb_ipcc_smq_recv(struct qb_ipcc_connection *c,
				void *msg_ptr, size_t msg_len)
{
	return sysv_recv(c->response.smq.q, msg_ptr, msg_len);
}

static void qb_ipcc_smq_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_request_header hdr;

	qb_util_log(LOG_DEBUG, "%s()\n", __func__);
	if (c->needs_sock_for_poll) {
		return;
	}

	hdr.id = QB_IPC_MSG_DISCONNECT;
	hdr.size = sizeof(hdr);
	sysv_send(c->request.smq.q, (const char *)&hdr, hdr.size);

	msgctl(c->event.smq.q, IPC_RMID, NULL);
	msgctl(c->response.smq.q, IPC_RMID, NULL);
}

int32_t qb_ipcc_smq_connect(struct qb_ipcc_connection *c,
			    struct qb_ipc_connection_response *response)
{
	int32_t res = 0;

	c->funcs.send = qb_ipcc_smq_send;
	c->funcs.recv = qb_ipcc_smq_recv;
	c->funcs.disconnect = qb_ipcc_smq_disconnect;
	c->type = QB_IPC_SYSV_MQ;
	c->needs_sock_for_poll = QB_TRUE;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		return -EINVAL;
	}

	memcpy(&c->request.smq.key, response->request, sizeof(uint32_t));
	c->request.smq.q = msgget(c->request.smq.key, IPC_NOWAIT);
	if (c->request.smq.q == -1) {
		res = -errno;
		perror("msgget:REQUEST");
		goto cleanup;
	}

	memcpy(&c->response.smq.key, response->response, sizeof(uint32_t));
	c->response.smq.q = msgget(c->response.smq.key, IPC_NOWAIT);
	if (c->response.smq.q == -1) {
		res = -errno;
		perror("msgget:RESPONSE");
		goto cleanup;
	}

	memcpy(&c->event.smq.key, response->event, sizeof(uint32_t));
	c->event.smq.q = msgget(c->event.smq.key, IPC_NOWAIT);
	if (c->event.smq.q == -1) {
		res = -errno;
		perror("msgget:EVENT");
		goto cleanup;
	}

cleanup:
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
}

static int32_t qb_ipcs_smq_connect(struct qb_ipcs_service *s,
				   struct qb_ipcs_connection *c,
				   struct qb_ipc_connection_response *r)
{
	int32_t res = 0;

	res = sysv_mq_unnamed_create(c, &c->request);
	if (res < 0) {
		res = -errno;
		goto cleanup;
	}
	memcpy(r->request, &c->request.smq.key, sizeof(int32_t));

	res = sysv_mq_unnamed_create(c, &c->response);
	if (res < 0) {
		res = -errno;
		goto cleanup_request;
	}
	memcpy(r->response, &c->response.smq.key, sizeof(int32_t));

	res = sysv_mq_unnamed_create(c, &c->event);
	if (res < 0) {
		res = -errno;
		goto cleanup_request_response;
	}
	memcpy(r->event, &c->event.smq.key, sizeof(int32_t));

	r->hdr.error = 0;
	return 0;

cleanup_request:
	msgctl(c->request.smq.q, IPC_RMID, NULL);
cleanup_request_response:
	msgctl(c->response.smq.q, IPC_RMID, NULL);

cleanup:
	r->hdr.error = res;
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
static ssize_t qb_ipcs_smq_request_recv(struct qb_ipcs_connection *c, void *buf,
					size_t buf_size)
{
	return sysv_recv(c->request.smq.q, buf, buf_size);
}

static ssize_t qb_ipcs_smq_response_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	return sysv_send(c->response.smq.q, (const char *)data, size);
}

static ssize_t qb_ipcs_smq_event_send(struct qb_ipcs_connection *c,
				      void *data, size_t size)
{
	return sysv_send(c->event.smq.q, (const char *)data, size);
}

int32_t qb_ipcs_smq_create(struct qb_ipcs_service *s)
{
	s->funcs.destroy = qb_ipcs_smq_destroy;
	s->funcs.connect = qb_ipcs_smq_connect;
	s->funcs.disconnect = qb_ipcs_smq_disconnect;
	s->funcs.response_send = qb_ipcs_smq_response_send;
	s->funcs.request_recv = qb_ipcs_smq_request_recv;
	s->needs_sock_for_poll = QB_TRUE;
	return 0;
}
