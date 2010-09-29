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
#include <qb/qbpoll.h>

static ssize_t qb_ipcs_pmq_event_send(struct qb_ipcs_connection *c,
					 void *data, size_t size);

/*
 * utility functions
 * --------------------------------------------------------
 */
static int32_t posix_mq_increase_limits(size_t max_msg_size, int32_t q_len)
{
	FILE *proc_fd;
	int32_t msgsize_max;
	char size_str[10];
	int32_t res = 0;
	int32_t size = 0;
#ifdef QB_LINUX
	struct rlimit rlim;
	int32_t q_limit;
#endif /* QB_LINUX */

	proc_fd = fopen("/proc/sys/fs/mqueue/msgsize_max", "r+");
	if (proc_fd == NULL) {
		res = -errno;
		qb_util_log(LOG_ERR,
			    "failed to open \"%s\": %s",
			    "/proc/sys/fs/mqueue/msgsize_max",
			    strerror(errno));
	}

	if (res == 0) {
		res = fscanf(proc_fd, "%d", &msgsize_max);
		if (res < 0) {
			res = -errno;
			qb_util_log(LOG_ERR, "fscanf failed: %s", strerror(errno));
		}
	}

	if (res == 1) {
		if (msgsize_max <= max_msg_size) {
			/* we need to increase the size */
			res = snprintf(size_str, 10, "%zd", (max_msg_size + 1));
			size = fwrite(size_str, 1, strlen(size_str), proc_fd);
			if (res != size) {
				res = -errno;
			}
		}
	}
	if (proc_fd) {
		fclose(proc_fd);
	}

#ifdef QB_LINUX
	if (getrlimit(RLIMIT_MSGQUEUE, &rlim) != 0) {
		res = -errno;
		qb_util_log(LOG_ERR, "getrlimit failed");
		return res;
	}
	q_limit = (max_msg_size * q_len * 4) / 3;
	rlim.rlim_cur += q_limit;
	rlim.rlim_max += q_limit;
	if (setrlimit(RLIMIT_MSGQUEUE, &rlim) != 0) {
		res = -errno;
		qb_util_log(LOG_ERR, "setrlimit failed");
		return res;
	}
#endif /* QB_LINUX */

	return 0;
}

static mqd_t posix_mq_create(const char *mq_name, size_t max_msg_size,
			     int32_t flags)
{
	struct mq_attr attr;
	mqd_t res = 0;
	int32_t q_len = 10;
	mode_t m = 0600;

	attr.mq_flags = O_NONBLOCK;
	attr.mq_maxmsg = q_len;
	attr.mq_msgsize = max_msg_size;

	if (mq_unlink(mq_name) == -1) {
		if (errno == EACCES) {
			qb_util_log(LOG_ERR, "Can't remove old mq \"%s\" : %s",
				    mq_name, strerror(errno));
			return -1;
		}
	}
	res = mq_open(mq_name, flags, m, &attr);
	if (res == (mqd_t)-1) {
		qb_util_log(LOG_ERR, "Can't create mq \"%s\": %s",
				mq_name, strerror(errno));
	}

	return res;
}


/*
 * client functions
 * --------------------------------------------------------
 */

static int32_t qb_ipcc_pmq_send(struct qb_ipcc_connection *c,
				const void *msg_ptr, size_t msg_len)
{
	int32_t res = mq_send(c->u.pmq.request.q, msg_ptr, msg_len, 1);
	if (res != 0) {
		return -errno;
	}
	return msg_len;
}

static ssize_t qb_ipcc_pmq_recv(struct qb_ipcc_connection *c,
				void *msg_ptr, size_t msg_len)
{
	uint32_t msg_prio;
	ssize_t res = mq_receive(c->u.pmq.response.q, (char *)msg_ptr, c->max_msg_size,
			  &msg_prio);
	if (res < 0) {
		return -errno;
	}
	return res;
}

static void qb_ipcc_pmq_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_request_header hdr;

	qb_util_log(LOG_DEBUG, "%s()\n", __func__);
	if (c->needs_sock_for_poll) {
		return;
	}

	hdr.id = QB_IPC_MSG_DISCONNECT;
	hdr.session_id = c->session_id;
	hdr.size = sizeof(hdr);
	mq_send(c->u.pmq.request.q, (const char *)&hdr, hdr.size, 30);

	mq_close(c->u.pmq.event.q);
	mq_unlink(c->u.pmq.event.name);

	mq_close(c->u.pmq.response.q);
	mq_unlink(c->u.pmq.response.name);

	mq_close(c->u.pmq.request.q);
}

static int32_t _ipcc_pmq_connect_to_service_(struct qb_ipcc_connection *c)
{
	int32_t res;
	ssize_t size;
	uint32_t priority;
	struct mar_req_pmq_setup start;
	struct mar_res_setup *msg_res;

	start.hdr.id = QB_IPC_MSG_CONNECT;
	start.hdr.session_id = c->session_id;
	start.pid = getpid();
	start.hdr.size = sizeof(struct mar_req_pmq_setup);
	strcpy(start.response_mq, c->u.pmq.response.name);
	strcpy(start.event_mq, c->u.pmq.event.name);

	res =
	    mq_send(c->u.pmq.request.q, (const char *)&start, start.hdr.size,
		    30);
	if (res == -1) {
		res = -errno;
		perror("mq_send");
		return res;
	}

	qb_util_log(LOG_DEBUG, "sent request to server %d\n", res);
	qb_util_log(LOG_DEBUG, "mq_receive'ing on %d\n", c->u.pmq.response.q);

mq_recv_again:
	size = mq_receive(c->u.pmq.response.q, c->receive_buf,
			  c->max_msg_size, &priority);

	if (size == -1 && errno == EAGAIN) {
		usleep(100000);
		goto mq_recv_again;
	}
	if (size == -1) {
		res = -errno;
		perror("_ipcc_pmq_connect_to_service_:mq_receive");
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

int32_t qb_ipcc_pmq_connect(struct qb_ipcc_connection * c)
{
	int32_t res = 0;

	c->funcs.send = qb_ipcc_pmq_send;
	c->funcs.recv = qb_ipcc_pmq_recv;
	c->funcs.disconnect = qb_ipcc_pmq_disconnect;
#if defined(QB_LINUX) || defined(QB_BSD)
	c->needs_sock_for_poll = QB_FALSE;
#else
	c->needs_sock_for_poll = QB_TRUE;
#endif

	if (strlen(c->name) > (NAME_MAX - 20)) {
		return -EINVAL;
	}

	/* Connect to the service's request message queue.
	 */
	posix_mq_increase_limits(c->max_msg_size, 10);
	snprintf(c->u.pmq.request.name, NAME_MAX, "/%s", c->name);
	c->u.pmq.request.q = mq_open(c->u.pmq.request.name,
				     O_WRONLY | O_NONBLOCK);
	if (c->u.pmq.request.q == (mqd_t)-1) {
		res = -errno;
		perror("mq_open:REQUEST");
		return res;
	}

	/* Create the response message queue.
	 */
	res = snprintf(c->u.pmq.response.name,
		       NAME_MAX, "/%s-response-%d", c->name, getpid());

	posix_mq_increase_limits(c->max_msg_size, 10);
	c->u.pmq.response.q = posix_mq_create(c->u.pmq.response.name,
					      c->max_msg_size,
					      O_RDONLY | O_CREAT | O_EXCL |
					      O_NONBLOCK);

	if (c->u.pmq.response.q == (mqd_t)-1) {
		res = -errno;
		perror("mq_open:RESPONSE");
		goto cleanup_request;
	}

	res =
	    snprintf(c->u.pmq.event.name, NAME_MAX, "/%s-event-%d",
		     c->name, getpid());

	posix_mq_increase_limits(c->max_msg_size, 10);
	c->u.pmq.event.q = posix_mq_create(c->u.pmq.event.name,
					      c->max_msg_size,
					      O_RDONLY | O_CREAT | O_EXCL |
					      O_NONBLOCK);

	if (c->u.pmq.event.q == (mqd_t)-1) {
		res = -errno;
		perror("mq_open:event");
		goto cleanup_request_response;
	}

	res = _ipcc_pmq_connect_to_service_(c);
	if (res == 0) {
		return 0;
	}

	mq_close(c->u.pmq.event.q);
	mq_unlink(c->u.pmq.event.name);

cleanup_request_response:
	mq_close(c->u.pmq.response.q);
	mq_unlink(c->u.pmq.response.name);

cleanup_request:
	mq_close(c->u.pmq.request.q);

	return res;
}


/*
 * service functions
 * --------------------------------------------------------
 */

static void qb_ipcs_pmq_disconnect(struct qb_ipcs_connection *c)
{
	struct qb_ipc_response_header msg;

	msg.id = QB_IPC_MSG_DISCONNECT;
	msg.size = sizeof(msg);
	msg.error = 0;

	qb_ipcs_pmq_event_send(c, &msg, msg.size);
}

static void qb_ipcs_pmq_destroy(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_list_head *iter;
	struct qb_list_head *iter_next;
	char mq_name[NAME_MAX];

	snprintf(mq_name, NAME_MAX, "/%s", s->name);

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

	if (mq_close(s->u.q) == -1)
		perror("mq_close");
	if (mq_unlink(mq_name) == -1)
		perror("mq_unlink");
}

static int32_t qb_ipcs_pmq_connect(struct qb_ipcs_service *s,
				   struct qb_ipcs_connection *c, void *data,
				   size_t size)
{
	int32_t res;
	struct mar_req_pmq_setup *init = (struct mar_req_pmq_setup *)data;
	struct mar_res_setup accept_msg;

	c->pid = init->pid;
	c->service = s;

	/* setup the response message queue
	 */
	posix_mq_increase_limits(c->service->max_msg_size, 10);
	strcpy(c->u.pmq.response.name, init->response_mq);
	c->u.pmq.response.q = mq_open(c->u.pmq.response.name,
				      O_WRONLY | O_NONBLOCK);
	if (c->u.pmq.response.q == (mqd_t)-1) {
		res = -errno;
		return res;
	}

	/* setup the event message queue
	 */
	posix_mq_increase_limits(c->service->max_msg_size, 10);
	strcpy(c->u.pmq.event.name, init->event_mq);
	c->u.pmq.event.q = mq_open(c->u.pmq.event.name,
				      O_WRONLY | O_NONBLOCK);

	if (c->u.pmq.event.q == (mqd_t)-1) {
		res = -errno;
		goto cleanup_response;
	}

	/* send the "connection accepted" mesage back.
	 */
	accept_msg.hdr.id = QB_IPC_MSG_CONNECT;
	accept_msg.hdr.size = sizeof(struct mar_res_setup);
	accept_msg.hdr.error = 0;
	accept_msg.max_msg_size = s->max_msg_size;

	res =
	    mq_send(c->u.pmq.response.q, (const char *)&accept_msg,
		    sizeof(struct mar_res_setup), 30);
	if (res == -1) {
		res = -errno;
		perror("mq_send:RESPONSE");
		goto cleanup_response;
	}

	return 0;

cleanup_response:
	accept_msg.hdr.error = res;
	mq_send(c->u.pmq.response.q, (const char *)&accept_msg,
		sizeof(struct mar_res_setup), 30);
	mq_close(c->u.pmq.response.q);

	return res;
}

static ssize_t qb_ipcs_pmq_request_recv(struct qb_ipcs_service *s, void *buf,
					size_t buf_size)
{
	uint32_t msg_prio;
	ssize_t res = mq_receive(s->u.q, buf, buf_size, &msg_prio);
	if (res == -1) {
		return -errno;
	}
	return res;
}

#if 0
static int32_t qb_ipcs_pmq_fd_get(struct qb_ipcs_service *s)
{
	return s->u.q;
}
#endif

static ssize_t qb_ipcs_pmq_response_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	if (mq_send(c->u.pmq.response.q, (const char *)data, size, 1) == -1) {
		return -errno;
	}
	return size;
}

static ssize_t qb_ipcs_pmq_event_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	if (mq_send(c->u.pmq.event.q, (const char *)data, size, 1) == -1) {
		return -errno;
	}
	return size;
}

int32_t qb_ipcs_pmq_create(struct qb_ipcs_service * s)
{
	char mq_name[NAME_MAX];
	int32_t res;

	snprintf(mq_name, NAME_MAX, "/%s", s->name);

	s->funcs.destroy = qb_ipcs_pmq_destroy;
	s->funcs.request_recv = qb_ipcs_pmq_request_recv;
	s->funcs.response_send = qb_ipcs_pmq_response_send;
	s->funcs.connect = qb_ipcs_pmq_connect;
	s->funcs.disconnect = qb_ipcs_pmq_disconnect;
#if defined(QB_LINUX) || defined(QB_BSD)
	s->needs_sock_for_poll = QB_FALSE;
#else
	s->needs_sock_for_poll = QB_TRUE;
#endif

	posix_mq_increase_limits(s->max_msg_size, 10);
	s->u.q = posix_mq_create(mq_name, s->max_msg_size,
				 (O_RDONLY | O_CREAT | O_EXCL | O_NONBLOCK));
	if (s->u.q == (mqd_t)-1) {
		res = -errno;
		return res;
	}
	qb_util_log(LOG_DEBUG, "%s() %d", __func__, s->u.q);

	if (!s->needs_sock_for_poll) {
		qb_poll_dispatch_add(s->poll_handle, s->u.q,
				     POLLIN | POLLPRI | POLLNVAL,
				     s, qb_ipcs_dispatch_service_request);
	}
	return 0;
}
