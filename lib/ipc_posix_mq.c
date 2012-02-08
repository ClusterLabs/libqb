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
#include <sys/resource.h>
#include "ipc_int.h"
#include "util_int.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>

#define QB_REQUEST_Q_LEN 3
#define QB_RESPONSE_Q_LEN 1
#define QB_EVENT_Q_LEN 3

static size_t q_space_used = 0;
#ifdef QB_LINUX
#define QB_RLIMIT_CHANGE_NEEDED 1
#endif /* QB_LINUX */

/*
 * utility functions
 * --------------------------------------------------------
 */
static int32_t
posix_mq_increase_limits(size_t max_msg_size, int32_t q_len)
{
	int32_t res = 0;
#ifdef QB_RLIMIT_CHANGE_NEEDED
	struct rlimit rlim;
	size_t q_space_needed;
#endif /* QB_RLIMIT_CHANGE_NEEDED */

#ifdef QB_RLIMIT_CHANGE_NEEDED
	if (getrlimit(RLIMIT_MSGQUEUE, &rlim) != 0) {
		res = -errno;
		qb_util_log(LOG_ERR, "getrlimit failed");
		return res;
	}
	q_space_needed = q_space_used + (max_msg_size * q_len * 4 / 3);

	qb_util_log(LOG_DEBUG, "rlimit:%d needed:%zu used:%zu",
		    (int)rlim.rlim_cur, q_space_needed, q_space_used);

	if (rlim.rlim_cur < q_space_needed) {
		rlim.rlim_cur = q_space_needed;
	}
	if (rlim.rlim_max < q_space_needed) {
		rlim.rlim_max = q_space_needed;
	}
	if (setrlimit(RLIMIT_MSGQUEUE, &rlim) != 0) {
		res = -errno;
		qb_util_log(LOG_ERR, "setrlimit failed");
	}
#endif /* QB_RLIMIT_CHANGE_NEEDED */
	return res;
}

static int32_t
posix_mq_open(struct qb_ipc_one_way *one_way, const char *name, size_t q_len)
{
	int32_t res = posix_mq_increase_limits(one_way->max_msg_size, q_len);
	if (res != 0) {
		return res;
	}
	one_way->u.pmq.q = mq_open(name, O_RDWR);
	if (one_way->u.pmq.q == (mqd_t) - 1) {
		res = -errno;
		perror("mq_open");
		return res;
	}
	(void)strlcpy(one_way->u.pmq.name, name, NAME_MAX);
	q_space_used += one_way->max_msg_size * q_len;
	return 0;
}

static int32_t
posix_mq_create(struct qb_ipcs_connection *c,
		struct qb_ipc_one_way *one_way, const char *name, size_t q_len)
{
	struct mq_attr attr;
	mqd_t q = 0;
	int32_t res = 0;
	mode_t m = 0600;
	size_t max_msg_size = one_way->max_msg_size;

	res = posix_mq_increase_limits(max_msg_size, q_len);
	if (res != 0) {
		return res;
	}
try_smaller:
	if (q != 0) {
		max_msg_size = max_msg_size / 2;
		q_len--;
	}
	attr.mq_flags = O_NONBLOCK;
	attr.mq_maxmsg = q_len;
	attr.mq_msgsize = max_msg_size;

	q = mq_open(name, O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK, m, &attr);
	if (q == (mqd_t) - 1 && errno == ENOMEM) {
		if (max_msg_size > 9000 && q_len > 3) {
			goto try_smaller;
		}
	}
	if (q == (mqd_t) - 1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "Can't create mq \"%s\"", name);
		return res;
	}
	q_space_used += max_msg_size * q_len;
	one_way->max_msg_size = max_msg_size;
	one_way->u.pmq.q = q;
	(void)strlcpy(one_way->u.pmq.name, name, NAME_MAX);

	res = fchown((int)q, c->euid, c->egid);
	if (res == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "fchown:%s", name);
		mq_close(q);
		mq_unlink(name);
	}

	return res;
}

static ssize_t
qb_ipc_pmq_send(struct qb_ipc_one_way *one_way,
		const void *msg_ptr, size_t msg_len)
{
	int32_t res = mq_send(one_way->u.pmq.q, msg_ptr, msg_len, 1);
	if (res != 0) {
		return -errno;
	}
	return msg_len;
}

static ssize_t
qb_ipc_pmq_sendv(struct qb_ipc_one_way *one_way,
		 const struct iovec *iov, size_t iov_len)
{
	int32_t total_size = 0;
	int32_t i;
	int32_t res = 0;
	char *data = NULL;
	char *pt = NULL;

	for (i = 0; i < iov_len; i++) {
		total_size += iov[i].iov_len;
	}
	if (total_size == 0) {
		return -EINVAL;
	}
	data = malloc(total_size);
	if (data == NULL) {
		return -ENOMEM;
	}
	pt = data;

	for (i = 0; i < iov_len; i++) {
		memcpy(pt, iov[i].iov_base, iov[i].iov_len);
		pt += iov[i].iov_len;
	}

	res = mq_send(one_way->u.pmq.q, data, total_size, 1);
	free(data);
	if (res != 0) {
		return -errno;
	}
	return total_size;
}

static ssize_t
qb_ipc_pmq_recv(struct qb_ipc_one_way *one_way,
		void *msg_ptr, size_t msg_len, int32_t ms_timeout)
{
	uint32_t msg_prio;
	struct timespec ts_timeout;
	ssize_t res;

	if (ms_timeout >= 0) {
		qb_util_timespec_from_epoch_get(&ts_timeout);
		qb_timespec_add_ms(&ts_timeout, ms_timeout);
	}

mq_receive_again:
	if (ms_timeout >= 0) {
		res = mq_timedreceive(one_way->u.pmq.q,
				      (char *)msg_ptr,
				      one_way->max_msg_size,
				      &msg_prio, &ts_timeout);
	} else {
		res = mq_receive(one_way->u.pmq.q,
				 (char *)msg_ptr,
				 one_way->max_msg_size, &msg_prio);
	}
	if (res == -1) {
		switch (errno) {
		case EINTR:
			goto mq_receive_again;
			break;
		case EAGAIN:
			res = -ETIMEDOUT;
			break;
		case ETIMEDOUT:
			res = -errno;
			break;
		default:
			res = -errno;
			qb_util_perror(LOG_ERR,
				       "error waiting for mq_timedreceive");
			break;
		}
	}

	return res;
}

/*
 * client functions
 * --------------------------------------------------------
 */

static void
qb_ipcc_pmq_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_request_header hdr;

	qb_util_log(LOG_DEBUG, "%s()", __func__);
	if (c->needs_sock_for_poll) {
		return;
	}

	hdr.id = QB_IPC_MSG_DISCONNECT;
	hdr.size = sizeof(hdr);
	mq_send(c->request.u.pmq.q, (const char *)&hdr, hdr.size, 30);

	mq_close(c->event.u.pmq.q);
	mq_close(c->response.u.pmq.q);
	mq_close(c->request.u.pmq.q);

	mq_unlink(c->event.u.pmq.name);
	mq_unlink(c->request.u.pmq.name);
	mq_unlink(c->response.u.pmq.name);
}

int32_t
qb_ipcc_pmq_connect(struct qb_ipcc_connection *c,
		    struct qb_ipc_connection_response *response)
{
	int32_t res = 0;

	c->funcs.send = qb_ipc_pmq_send;
	c->funcs.sendv = qb_ipc_pmq_sendv;
	c->funcs.recv = qb_ipc_pmq_recv;
	c->funcs.fc_get = NULL;
	c->funcs.disconnect = qb_ipcc_pmq_disconnect;
#if defined(QB_LINUX) || defined(QB_BSD)
	c->needs_sock_for_poll = QB_FALSE;
#else
	c->needs_sock_for_poll = QB_TRUE;
#endif

	if (strlen(c->name) > (NAME_MAX - 20)) {
		return -EINVAL;
	}

	res = posix_mq_open(&c->request, response->request, QB_REQUEST_Q_LEN);
	if (res != 0) {
		perror("mq_open:REQUEST");
		return res;
	}
	res = posix_mq_open(&c->response, response->response,
			    QB_RESPONSE_Q_LEN);
	if (res != 0) {
		perror("mq_open:RESPONSE");
		goto cleanup_request;
	}
	res = posix_mq_open(&c->event, response->event, QB_EVENT_Q_LEN);
	if (res != 0) {
		perror("mq_open:EVENT");
		goto cleanup_request_response;
	}

	return 0;

cleanup_request_response:
	mq_close(c->response.u.pmq.q);

cleanup_request:
	mq_close(c->request.u.pmq.q);

	return res;
}

/*
 * service functions
 * --------------------------------------------------------
 */

static void
qb_ipcs_pmq_disconnect(struct qb_ipcs_connection *c)
{
	struct qb_ipc_response_header msg;

	msg.id = QB_IPC_MSG_DISCONNECT;
	msg.size = sizeof(msg);
	msg.error = 0;

	(void)qb_ipc_pmq_send(&c->event, &msg, msg.size);

	mq_close(c->event.u.pmq.q);
	mq_close(c->response.u.pmq.q);
	mq_close(c->request.u.pmq.q);

	mq_unlink(c->event.u.pmq.name);
	mq_unlink(c->request.u.pmq.name);
	mq_unlink(c->response.u.pmq.name);
}

static int32_t
qb_ipcs_pmq_connect(struct qb_ipcs_service *s,
		    struct qb_ipcs_connection *c,
		    struct qb_ipc_connection_response *r)
{
	int32_t res = 0;

	snprintf(r->request, NAME_MAX, "/%s-request-%d", s->name, c->pid);
	snprintf(r->response, NAME_MAX, "/%s-response-%d", s->name, c->pid);
	snprintf(r->event, NAME_MAX, "/%s-event-%d", s->name, c->pid);

	res = posix_mq_create(c, &c->request, r->request, QB_REQUEST_Q_LEN);
	if (res < 0) {
		goto cleanup;
	}

	res = posix_mq_create(c, &c->response, r->response, QB_RESPONSE_Q_LEN);
	if (res < 0) {
		goto cleanup_request;
	}

	res = posix_mq_create(c, &c->event, r->event, QB_EVENT_Q_LEN);
	if (res < 0) {
		goto cleanup_request_response;
	}

	if (!s->needs_sock_for_poll) {
		res =
		    s->poll_fns.dispatch_add(s->poll_priority,
					     (int32_t) c->request.u.pmq.q,
					     POLLIN | POLLPRI | POLLNVAL, c,
					     qb_ipcs_dispatch_service_request);
	}

	r->hdr.error = 0;
	return res;

cleanup_request_response:
	mq_close(c->response.u.pmq.q);
	mq_unlink(r->response);
cleanup_request:
	mq_close(c->request.u.pmq.q);
	mq_unlink(r->request);

cleanup:
	r->hdr.error = res;

	return res;
}

static ssize_t
qb_ipc_pmq_q_len_get(struct qb_ipc_one_way *one_way)
{
	struct mq_attr info;
	int32_t res = mq_getattr(one_way->u.pmq.q, &info);
	if (res == 0) {
		return info.mq_curmsgs;
	}
	return -errno;
}

void
qb_ipcs_pmq_init(struct qb_ipcs_service *s)
{
	s->funcs.connect = qb_ipcs_pmq_connect;
	s->funcs.disconnect = qb_ipcs_pmq_disconnect;

	s->funcs.recv = qb_ipc_pmq_recv;
	s->funcs.send = qb_ipc_pmq_send;
	s->funcs.sendv = qb_ipc_pmq_sendv;
	s->funcs.peek = NULL;
	s->funcs.reclaim = NULL;

	s->funcs.fc_set = NULL;
	s->funcs.q_len_get = qb_ipc_pmq_q_len_get;

#if defined(QB_LINUX) || defined(QB_BSD)
	s->needs_sock_for_poll = QB_FALSE;
#else
	s->needs_sock_for_poll = QB_TRUE;
#endif
}
