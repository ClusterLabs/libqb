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

#ifdef HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif
#ifdef HAVE_SYS_MSG_H
#include <sys/msg.h>
#endif

#include <qb/qbdefs.h>
#include <qb/qbloop.h>
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

/*
 * utility functions
 * --------------------------------------------------------
 */
static int32_t
sysv_mq_unnamed_create(struct qb_ipcs_connection *c,
		       struct qb_ipc_one_way *queue)
{
	struct msqid_ds info;
	int32_t res = 0;

retry_creating_the_q:
	queue->u.smq.key = random();
	queue->u.smq.q =
	    msgget(queue->u.smq.key,
		   IPC_CREAT | IPC_EXCL | IPC_NOWAIT | S_IWUSR | S_IRUSR);
	if (queue->u.smq.q == -1 && errno == EEXIST) {
		goto retry_creating_the_q;
	} else if (queue->u.smq.q == -1) {
		return -errno;
	}

	/*
	 * change the queue size and change the ownership to that of
	 * the client so they can access it.
	 */
	res = msgctl(queue->u.smq.q, IPC_STAT, &info);
	if (res != 0) {
		res = -errno;
		qb_util_perror(LOG_ERR, "error getting sysv-mq info");
		return res;
	}

	if (info.msg_perm.uid != 0) {
		qb_util_log(LOG_WARNING,
			    "not enough privileges to increase msg_qbytes");
		return res;
	}
	info.msg_qbytes = 2 * queue->max_msg_size;
	info.msg_perm.uid = c->euid;
	info.msg_perm.gid = c->egid;

	res = msgctl(queue->u.smq.q, IPC_SET, &info);
	if (res != 0) {
		res = -errno;
		qb_util_perror(LOG_ERR,
			       "error modifying the SYSV message queue");
		return res;
	}

	return 0;
}

static int32_t
sysv_split_and_send(int32_t q, const void *msg_ptr,
		    size_t msg_len, int32_t last_chunk)
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
		 * is more to receive for this message.
		 */
		if (last_chunk) {
			buf.id = to_send_next + 1;
		} else {
			buf.id = to_send_next + 1 + msg_len;
		}
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

/*
 * client functions
 * --------------------------------------------------------
 */
static ssize_t
qb_ipc_smq_send(struct qb_ipc_one_way *one_way,
		const void *msg_ptr, size_t msg_len)
{
	return sysv_split_and_send(one_way->u.smq.q, msg_ptr, msg_len, QB_TRUE);
}

static ssize_t
qb_ipc_smq_sendv(struct qb_ipc_one_way *one_way,
		 const struct iovec *iov, size_t iov_len)
{
	int32_t res;
	int32_t sent = 0;
	struct my_msgbuf buf;
	int32_t i;

	for (i = 0; i < iov_len; i++) {
		if (iov[i].iov_len <= MY_DATA_SIZE) {
			if (i == iov_len - 1) {
				buf.id = 1;
			} else {
				buf.id = i + iov[i].iov_len;
			}
			memcpy(buf.data, iov[i].iov_base, iov[i].iov_len);
			res = msgsnd(one_way->u.smq.q,
				     &buf, iov[i].iov_len,
				     IPC_NOWAIT);
			if (res == 0) {
				res = iov[i].iov_len;
			} else {
				res = -errno;
			}
		} else {
			res = sysv_split_and_send(one_way->u.smq.q,
						  iov[i].iov_base,
						  iov[i].iov_len,
						  (i == iov_len - 1));
		}
		if (res > 0) {
			sent += res;
		} else {
			return res;
		}
	}
	return sent;
}

static ssize_t
qb_ipc_smq_recv(struct qb_ipc_one_way *one_way,
		void *msg_ptr, size_t msg_len, int32_t ms_timeout)
{
	ssize_t res;
	ssize_t received = 0;
#ifdef PACK_MESSAGES
	char *progress = (char *)msg_ptr;
	struct my_msgbuf buf;

	do {
try_again:
		res = msgrcv(one_way->u.smq.q, &buf,
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
	res = msgrcv(one_way->u.smq.q, msg_ptr, msg_len, 0, IPC_NOWAIT);
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

static void
qb_ipcc_smq_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_request_header hdr;

	qb_util_log(LOG_TRACE, "%s()", __func__);

	hdr.id = QB_IPC_MSG_DISCONNECT;
	hdr.size = sizeof(hdr);
	(void)sysv_split_and_send(c->request.u.smq.q,
				  (const char *)&hdr, hdr.size, QB_TRUE);

	msgctl(c->event.u.smq.q, IPC_RMID, NULL);
	msgctl(c->response.u.smq.q, IPC_RMID, NULL);
	msgctl(c->request.u.smq.q, IPC_RMID, NULL);
}

int32_t
qb_ipcc_smq_connect(struct qb_ipcc_connection *c,
		    struct qb_ipc_connection_response *response)
{
	int32_t res = 0;

	c->funcs.send = qb_ipc_smq_send;
	c->funcs.sendv = qb_ipc_smq_sendv;
	c->funcs.recv = qb_ipc_smq_recv;
	c->funcs.fc_get = NULL;
	c->funcs.disconnect = qb_ipcc_smq_disconnect;
	c->needs_sock_for_poll = QB_TRUE;

	if (strlen(c->name) > (NAME_MAX - 20)) {
		return -EINVAL;
	}

	memcpy(&c->request.u.smq.key, response->request, sizeof(uint32_t));
	c->request.u.smq.q = msgget(c->request.u.smq.key, IPC_NOWAIT);
	if (c->request.u.smq.q == -1) {
		res = -errno;
		perror("msgget:REQUEST");
		goto cleanup;
	}

	memcpy(&c->response.u.smq.key, response->response, sizeof(uint32_t));
	c->response.u.smq.q = msgget(c->response.u.smq.key, IPC_NOWAIT);
	if (c->response.u.smq.q == -1) {
		res = -errno;
		perror("msgget:RESPONSE");
		goto cleanup_request;
	}

	memcpy(&c->event.u.smq.key, response->event, sizeof(uint32_t));
	c->event.u.smq.q = msgget(c->event.u.smq.key, IPC_NOWAIT);
	if (c->event.u.smq.q == -1) {
		res = -errno;
		perror("msgget:EVENT");
		goto cleanup_request_response;
	}
	return 0;

cleanup_request_response:
	msgctl(c->response.u.smq.q, IPC_RMID, NULL);
cleanup_request:
	msgctl(c->request.u.smq.q, IPC_RMID, NULL);
cleanup:
	return res;
}

/*
 * service functions
 * --------------------------------------------------------
 */
static void
qb_ipcs_smq_disconnect(struct qb_ipcs_connection *c)
{
	struct qb_ipc_response_header msg;

	if (c->setup.u.us.sock != -1) {
		msg.id = QB_IPC_MSG_DISCONNECT;
		msg.size = sizeof(msg);
		msg.error = 0;

		(void)qb_ipc_smq_send(&c->event, &msg, msg.size);
	} else {
		msgctl(c->event.u.smq.q, IPC_RMID, NULL);
		msgctl(c->response.u.smq.q, IPC_RMID, NULL);
		msgctl(c->request.u.smq.q, IPC_RMID, NULL);
	}
}

static int32_t
qb_ipcs_smq_connect(struct qb_ipcs_service *s,
		    struct qb_ipcs_connection *c,
		    struct qb_ipc_connection_response *r)
{
	int32_t res = 0;

	res = sysv_mq_unnamed_create(c, &c->request);
	if (res < 0) {
		res = -errno;
		goto cleanup;
	}
	memcpy(r->request, &c->request.u.smq.key, sizeof(int32_t));

	res = sysv_mq_unnamed_create(c, &c->response);
	if (res < 0) {
		res = -errno;
		goto cleanup_request;
	}
	memcpy(r->response, &c->response.u.smq.key, sizeof(int32_t));

	res = sysv_mq_unnamed_create(c, &c->event);
	if (res < 0) {
		res = -errno;
		goto cleanup_request_response;
	}
	memcpy(r->event, &c->event.u.smq.key, sizeof(int32_t));

	r->hdr.error = 0;
	return 0;

cleanup_request_response:
	msgctl(c->response.u.smq.q, IPC_RMID, NULL);
cleanup_request:
	msgctl(c->request.u.smq.q, IPC_RMID, NULL);

cleanup:
	r->hdr.error = res;
	return res;
}

static ssize_t
qb_ipc_smq_q_len_get(struct qb_ipc_one_way *one_way)
{
	struct msqid_ds info;
	int32_t res = msgctl(one_way->u.smq.q, IPC_STAT, &info);
	if (res == 0) {
		return info.msg_qnum;
	}
	return -errno;
}

void
qb_ipcs_smq_init(struct qb_ipcs_service *s)
{
	s->funcs.connect = qb_ipcs_smq_connect;
	s->funcs.disconnect = qb_ipcs_smq_disconnect;

	s->funcs.send = qb_ipc_smq_send;
	s->funcs.sendv = qb_ipc_smq_sendv;
	s->funcs.recv = qb_ipc_smq_recv;
	s->funcs.peek = NULL;
	s->funcs.reclaim = NULL;

	s->funcs.fc_set = NULL;
	s->funcs.q_len_get = qb_ipc_smq_q_len_get;

	s->needs_sock_for_poll = QB_TRUE;
}
