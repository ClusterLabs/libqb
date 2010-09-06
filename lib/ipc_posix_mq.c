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


static ssize_t qb_ipcs_pmq_dispatch_send(struct qb_ipcs_connection *c,
					 void *data, size_t size);

/*
 * utility functions
 * --------------------------------------------------------
 */
static int32_t posix_mq_increase_limits(size_t max_msg_size,
		int32_t q_len)
{
	FILE* proc_fd;
	int32_t msgsize_max;
	char size_str[10];
	int32_t res = 0;
#ifdef QB_LINUX
	struct rlimit rlim;
	int32_t q_limit;
#endif /* QB_LINUX */

	proc_fd = fopen("/proc/sys/fs/mqueue/msgsize_max", "r+");
	if (proc_fd > 0) {
		res = fscanf(proc_fd, "%d", &msgsize_max);
	} else {
		res = -errno;
		qb_util_log(LOG_ERR, "fopen failed");
	}
	if (res == 1) {
		if (msgsize_max <= max_msg_size) {
			/* we need to increase the size */
			snprintf(size_str, 10, "%zd", (max_msg_size + 1));
			fwrite(size_str, 1, strlen(size_str), proc_fd);
		}
	} else {
		qb_util_log(LOG_ERR, "fscanf failed");
		return res;
	}
	fclose(proc_fd);

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

static mqd_t posix_mq_create(const char* mq_name, size_t max_msg_size,
		int32_t flags)
{
	struct mq_attr attr;
	mqd_t res = 0;
	int32_t q_len = 10;
        mode_t m = 0600;

	attr.mq_flags = O_NONBLOCK;
	attr.mq_maxmsg = q_len;
	attr.mq_msgsize = max_msg_size;

	mq_unlink(mq_name);
	res = mq_open(mq_name, flags, m, &attr);
	if (res == (mqd_t)-1) {
		perror(mq_name);
	}

	printf("%s(%s, %zd, %d) == %d\n",
			__func__, mq_name, max_msg_size, flags, res);

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
	if (res < 0) {
		return -errno;
	}
	return 0;
}

static ssize_t qb_ipcc_pmq_recv(struct qb_ipcc_connection *c,
				const void *msg_ptr, size_t msg_len)
{
	uint32_t msg_prio;
	ssize_t res = mq_receive(c->u.pmq.response.q, (char *)msg_ptr, c->max_msg_size,
			  &msg_prio);
	if (res < 0) {
		return -errno;
	}
	return 0;
}

static void qb_ipcc_pmq_disconnect(struct qb_ipcc_connection *c)
{
	struct qb_ipc_request_header hdr;

	printf("%s()\n", __func__);
	if (c->needs_sock_for_poll) {
		return;
	}

	hdr.id = QB_IPC_MSG_DISCONNECT;
	hdr.session_id = c->session_id;
	hdr.size = sizeof(hdr);
	mq_send(c->u.pmq.request.q, (const char *)&hdr, hdr.size, 30);

	mq_close(c->u.pmq.dispatch.q);
	mq_unlink(c->u.pmq.dispatch.name);

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
	strcpy(start.dispatch_mq, c->u.pmq.dispatch.name);

	res =
	    mq_send(c->u.pmq.request.q, (const char *)&start, start.hdr.size,
		    30);
	if (res == -1) {
		res = -errno;
		perror("mq_send");
		return res;
	}
	printf("sent request to server %d\n", res);
	printf("mq_receive'ing on %d\n", c->u.pmq.response.q);

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
	printf("received response from server %zd\n", size);
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
			NAME_MAX, "/%s-response-%d",
			c->name, getpid());

	posix_mq_increase_limits(c->max_msg_size, 10);
	c->u.pmq.response.q = posix_mq_create(c->u.pmq.response.name,
			c->max_msg_size,
			O_RDONLY | O_CREAT | O_EXCL | O_NONBLOCK);

	if (c->u.pmq.response.q == (mqd_t)-1) {
		res = -errno;
		perror("mq_open:RESPONSE");
		goto cleanup_request;
	}

	res =
	    snprintf(c->u.pmq.dispatch.name, NAME_MAX, "/%s-dispatch-%d",
		     c->name, getpid());

	posix_mq_increase_limits(c->max_msg_size, 10);
	c->u.pmq.dispatch.q = posix_mq_create(c->u.pmq.dispatch.name,
			c->max_msg_size,
			O_RDONLY | O_CREAT | O_EXCL | O_NONBLOCK);

	if (c->u.pmq.dispatch.q == (mqd_t)-1) {
		res = -errno;
		perror("mq_open:DISPATCH");
		goto cleanup_request_response;
	}

	res = _ipcc_pmq_connect_to_service_(c);
	if (res == 0) {
		return 0;
	}

	printf("%s:%d\n", __FILE__, __LINE__);

	mq_close(c->u.pmq.dispatch.q);
	mq_unlink(c->u.pmq.dispatch.name);

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

	qb_ipcs_pmq_dispatch_send(c, &msg, msg.size);
}

static void qb_ipcs_pmq_destroy(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_list_head *iter;
	struct qb_list_head *iter_next;

	printf("%s\n", __func__);

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
	if (mq_unlink(s->name) == -1)
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
		perror("mq_open:RESPONSE");
		return res;
	}
	qb_util_log(LOG_DEBUG, "%s:%s (fd==%d)",
			__func__, c->u.pmq.response.name, c->u.pmq.response.q);

	/* setup the dispatch message queue
	 */
	posix_mq_increase_limits(c->service->max_msg_size, 10);
	strcpy(c->u.pmq.dispatch.name, init->dispatch_mq);
	qb_util_log(LOG_DEBUG, "%s:%s", __func__, c->u.pmq.dispatch.name);
	c->u.pmq.dispatch.q = mq_open(c->u.pmq.dispatch.name,
			O_WRONLY | O_NONBLOCK);

	if (c->u.pmq.dispatch.q == (mqd_t)-1) {
		res = -errno;
		perror("mq_open:DISPATCH");
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

static ssize_t qb_ipcs_pmq_dispatch_send(struct qb_ipcs_connection *c,
					 void *data, size_t size)
{
	if (mq_send(c->u.pmq.dispatch.q, (const char *)data, size, 1) == -1) {
		return -errno;
	}
	return size;
}

int32_t qb_ipcs_pmq_create(struct qb_ipcs_service *s)
{
	char mq_name[NAME_MAX];
	int32_t res;

	snprintf(mq_name, NAME_MAX, "/%s", s->name);

	s->funcs.destroy = qb_ipcs_pmq_destroy;
	s->funcs.request_recv = qb_ipcs_pmq_request_recv;
	s->funcs.response_send = qb_ipcs_pmq_response_send;
	s->funcs.connect = qb_ipcs_pmq_connect;
	s->funcs.disconnect = qb_ipcs_pmq_disconnect;

	posix_mq_increase_limits(s->max_msg_size, 10);
	s->u.q = posix_mq_create(mq_name, s->max_msg_size,
			 (O_RDONLY | O_CREAT | O_EXCL | O_NONBLOCK));
	if (s->u.q == (mqd_t)-1) {
		res = -errno;
		perror("posix_mq_create:REQUEST");
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
