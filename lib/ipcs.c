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

#include "util_int.h"
#include "ipc_int.h"
#include <qb/qbdefs.h>
#include <qb/qbatomic.h>
#include <qb/qbipcs.h>

static void qb_ipcs_flowcontrol_set(struct qb_ipcs_connection *c,
				    int32_t fc_enable);

static QB_LIST_DECLARE(qb_ipc_services);

qb_ipcs_service_t* qb_ipcs_create(const char *name,
				  int32_t service_id,
				  enum qb_ipc_type type,
				  struct qb_ipcs_service_handlers *handlers)
{
	struct qb_ipcs_service *s;

	s = calloc(1, sizeof(struct qb_ipcs_service));

	s->pid = getpid();
	s->type = type;
	s->needs_sock_for_poll = QB_FALSE;
	s->poll_priority = QB_LOOP_MED;
	s->ref_count = 1;

	s->service_id = service_id;
	strncpy(s->name, name, NAME_MAX);

	s->serv_fns.connection_accept = handlers->connection_accept;
	s->serv_fns.connection_created = handlers->connection_created;
	s->serv_fns.msg_process = handlers->msg_process;
	s->serv_fns.connection_closed = handlers->connection_closed;
	s->serv_fns.connection_destroyed = handlers->connection_destroyed;

	qb_list_init(&s->connections);
	qb_list_init(&s->list);
	qb_list_add(&s->list, &qb_ipc_services);

	return s;
}

void qb_ipcs_poll_handlers_set(struct qb_ipcs_service* s,
			       struct qb_ipcs_poll_handlers *handlers)
{
	s->poll_fns.job_add = handlers->job_add;
	s->poll_fns.dispatch_add = handlers->dispatch_add;
	s->poll_fns.dispatch_mod = handlers->dispatch_mod;
	s->poll_fns.dispatch_del = handlers->dispatch_del;
}

int32_t qb_ipcs_run(struct qb_ipcs_service* s)
{
	int32_t res;

	switch (s->type) {
	case QB_IPC_SOCKET:
		qb_ipcs_us_init((struct qb_ipcs_service *)s);
		break;
	case QB_IPC_SHM:
		qb_ipcs_shm_init((struct qb_ipcs_service *)s);
		break;
	case QB_IPC_POSIX_MQ:
		qb_ipcs_pmq_init((struct qb_ipcs_service *)s);
		break;
	case QB_IPC_SYSV_MQ:
		qb_ipcs_smq_init((struct qb_ipcs_service *)s);
		break;
	default:
		res = -EINVAL;
		break;
	}
	res = qb_ipcs_us_publish(s);
	if (res < 0) {
		qb_ipcs_unref(s);
		return res;
	}

	if (res < 0) {
		(void)qb_ipcs_us_withdraw(s);
	}

	return res;
}

void qb_ipcs_request_rate_limit(struct qb_ipcs_service* s, enum qb_ipcs_rate_limit rl)
{
	struct qb_ipcs_connection *c;
	enum qb_loop_priority p;

	switch (rl) {
	case QB_IPCS_RATE_FAST:
		p = QB_LOOP_HIGH;
		break;
	case QB_IPCS_RATE_SLOW:
	case QB_IPCS_RATE_OFF:
		p = QB_LOOP_LOW;
		break;
	default:
	case QB_IPCS_RATE_NORMAL:
		p = QB_LOOP_MED;
		break;
	}

	qb_list_for_each_entry(c, &s->connections, list) {
		qb_ipcs_connection_ref_inc(c);

		qb_ipcs_flowcontrol_set(c, (rl == QB_IPCS_RATE_OFF));
		if (s->poll_priority == p) {
			qb_ipcs_connection_ref_dec(c);
			continue;
		}

		if (s->type == QB_IPC_POSIX_MQ && !s->needs_sock_for_poll) {
			(void)s->poll_fns.dispatch_mod(p, (int32_t)c->request.u.pmq.q,
						       POLLIN | POLLPRI | POLLNVAL,
						       c, qb_ipcs_dispatch_service_request);
		} else if (s->type == QB_IPC_SOCKET) {
			(void)s->poll_fns.dispatch_mod(p, c->event.u.us.sock,
						       POLLIN | POLLPRI | POLLNVAL,
						       c,
						       qb_ipcs_dispatch_connection_request);
		} else {
			(void)s->poll_fns.dispatch_mod(p, c->setup.u.us.sock,
						       POLLIN | POLLPRI | POLLNVAL,
						       c,
						       qb_ipcs_dispatch_connection_request);
		}
		qb_ipcs_connection_ref_dec(c);
	}
	s->poll_priority = p;
}

void qb_ipcs_ref(struct qb_ipcs_service *s)
{
	qb_atomic_int_inc(&s->ref_count);
}

void qb_ipcs_unref(struct qb_ipcs_service *s)
{
	int32_t free_it;
	struct qb_ipcs_connection *c = NULL;
	struct qb_list_head *iter;
	struct qb_list_head *iter_next;

	assert(s->ref_count > 0);
	free_it = qb_atomic_int_dec_and_test(&s->ref_count);
	if (free_it) {
		qb_util_log(LOG_DEBUG, "%s() - destorying\n", __func__);
		qb_list_for_each_safe(iter, iter_next, &s->connections) {
			c = qb_list_entry(iter, struct qb_ipcs_connection, list);
			if (c == NULL) {
				continue;
			}
			qb_ipcs_disconnect(c);
		}
		(void)qb_ipcs_us_withdraw(s);
		free(s);
	}
}

void qb_ipcs_destroy(struct qb_ipcs_service* s)
{
	qb_ipcs_unref(s);
}

/*
 * connection API
 */

ssize_t qb_ipcs_response_send(struct qb_ipcs_connection *c, const void *data,
			      size_t size)
{
	ssize_t res;

	qb_ipcs_connection_ref_inc(c);
	res = c->service->funcs.send(&c->response, data, size);
	if (res == size) {
		c->stats.responses++;
	} else if (res == -EAGAIN) {
		c->stats.send_retries++;
	}
	qb_ipcs_connection_ref_dec(c);

	return res;
}

ssize_t qb_ipcs_response_sendv(struct qb_ipcs_connection *c, const struct iovec * iov, size_t iov_len)
{
	ssize_t res;

	qb_ipcs_connection_ref_inc(c);
	res = c->service->funcs.sendv(&c->response, iov, iov_len);
	if (res > 0) {
		c->stats.responses++;
	} else if (res == -EAGAIN) {
		c->stats.send_retries++;
	}
	qb_ipcs_connection_ref_dec(c);

	return res;
}

ssize_t qb_ipcs_event_send(struct qb_ipcs_connection *c, const void *data,
			   size_t size)
{
	ssize_t res;
	ssize_t res2 = 0;
	int32_t try_count = 0;

	qb_ipcs_connection_ref_inc(c);

	do {
		try_count++;
		res = c->service->funcs.send(&c->event, data, size);
		if (res == size) {
			c->stats.events++;
		} else if (res == -EAGAIN) {
			c->stats.send_retries++;
		}
	} while (res == -EAGAIN && try_count < 20);
	if (res > 0) {
		if (c->service->needs_sock_for_poll) {
			do {
				res2 = qb_ipc_us_send(&c->setup, &res, 1);
			} while (res2 == -EAGAIN);
		}
	} else if (res != -EAGAIN) {
		qb_util_log(LOG_ERR,
			    "failed to send event : %s",
			    strerror(-res));
	}
	qb_ipcs_connection_ref_dec(c);

	return res;
}


ssize_t qb_ipcs_event_sendv(struct qb_ipcs_connection *c, const struct iovec * iov, size_t iov_len)
{
	ssize_t res;
	ssize_t res2;
	int32_t try_count = 0;

	qb_ipcs_connection_ref_inc(c);

	do {
		try_count++;
		res = c->service->funcs.sendv(&c->event, iov, iov_len);
		if (res > 0) {
			c->stats.events++;
		} else if (res == -EAGAIN) {
			c->stats.send_retries++;
		}
	} while (res == -EAGAIN && try_count < 20);
	if (res > 0) {
		if (c->service->needs_sock_for_poll) {
			do {
				res2 = qb_ipc_us_send(&c->setup, &res, 1);
			} while (res2 == -EAGAIN);
		}
	} else if (res != -EAGAIN) {
		qb_util_log(LOG_ERR,
			    "failed to send event : %s",
			    strerror(-res));
	}

	qb_ipcs_connection_ref_dec(c);

	return res;
}

qb_ipcs_connection_t * qb_ipcs_connection_first_get(struct qb_ipcs_service* s)
{
	struct qb_ipcs_connection *c;
	struct qb_list_head *iter;

	if (qb_list_empty(&s->connections)) {
		return NULL;
	}
	iter = s->connections.next;

	c = qb_list_entry(iter, struct qb_ipcs_connection, list);
	qb_ipcs_connection_ref_inc(c);

	return c;
}

qb_ipcs_connection_t * qb_ipcs_connection_next_get(struct qb_ipcs_service* s,
						   struct qb_ipcs_connection *current)
{
	struct qb_ipcs_connection *c;
	struct qb_list_head *iter;

	if (current->list.next == &s->connections) {
		return NULL;
	}
	iter = current->list.next;

	c = qb_list_entry(iter, struct qb_ipcs_connection, list);
	qb_ipcs_connection_ref_inc(c);

	return c;
}

int32_t qb_ipcs_service_id_get(struct qb_ipcs_connection *c)
{
	return c->service->service_id;
}

struct qb_ipcs_connection *qb_ipcs_connection_alloc(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = calloc(1, sizeof(struct qb_ipcs_connection));

	c->refcount = 1;
	c->service = s;
	c->pid = 0;
	c->euid = -1;
	c->egid = -1;
	qb_list_init(&c->list);
	c->receive_buf = NULL;
	c->context = NULL;
	c->fc_enabled = QB_FALSE;
	c->state = QB_IPCS_CONNECTION_INACTIVE;
	return c;
}

void qb_ipcs_connection_ref_inc(struct qb_ipcs_connection *c)
{
	qb_atomic_int_inc(&c->refcount);
}

void qb_ipcs_connection_ref_dec(struct qb_ipcs_connection *c)
{
	int32_t free_it;

	if (c->refcount < 1) {
		qb_util_log(LOG_ERR, "%s() ref:%d state:%d fd:%d",
			    __func__, c->refcount, c->state, c->setup.u.us.sock);
		assert(0);
	}
	free_it = qb_atomic_int_dec_and_test(&c->refcount);
	if (free_it) {
		qb_list_del(&c->list);
		if (c->service->serv_fns.connection_destroyed) {
			c->service->serv_fns.connection_destroyed(c);
		}
		c->service->funcs.disconnect(c);
		if (c->receive_buf) {
			free(c->receive_buf);
		}
		free(c);
	}
}

void qb_ipcs_disconnect(struct qb_ipcs_connection *c)
{
	int32_t res = 0;
	qb_loop_job_dispatch_fn rerun_job;

	qb_util_log(LOG_DEBUG, "%s() state:%d", __func__, c->state);

	if (c->state == QB_IPCS_CONNECTION_ACTIVE) {
		if (c->service->needs_sock_for_poll &&
		    c->setup.u.us.sock > 0) {
			qb_ipcc_us_sock_close(c->setup.u.us.sock);
			c->setup.u.us.sock = -1;
			qb_ipcs_connection_ref_dec(c);
		}
		c->state = QB_IPCS_CONNECTION_DOWN;
		c->service->stats.active_connections--;
		c->service->stats.closed_connections++;
	}
	if (c->state == QB_IPCS_CONNECTION_DOWN) {
		res = 0;
		if (c->service->serv_fns.connection_closed) {
			res = c->service->serv_fns.connection_closed(c);
		}
		if (res == 0) {
			qb_ipcs_connection_ref_dec(c);
		} else {
			/* ok, so they want the connection_closedd()
			 * function re-run */
			rerun_job = (qb_loop_job_dispatch_fn)qb_ipcs_disconnect;
			res = c->service->poll_fns.job_add(QB_LOOP_LOW, c,
							   rerun_job);
			if (res != 0) {
				/* last ditch attempt to cleanup */
				qb_ipcs_connection_ref_dec(c);
			}
		}
	}
}

static void qb_ipcs_flowcontrol_set(struct qb_ipcs_connection *c, int32_t fc_enable)
{
	if (c->fc_enabled != fc_enable) {
		c->service->funcs.fc_set(&c->request, fc_enable);
		c->fc_enabled = fc_enable;
		c->stats.flow_control_state = fc_enable;
		c->stats.flow_control_count++;
	}
}

static int32_t _process_request_(struct qb_ipcs_connection *c,
				 int32_t ms_timeout)
{
	int32_t res = 0;
	ssize_t size;
	struct qb_ipc_request_header *hdr;

	qb_ipcs_connection_ref_inc(c);

	if (c->service->funcs.peek && c->service->funcs.reclaim) {
		size = c->service->funcs.peek(&c->request, (void**)&hdr,
					      ms_timeout);
	} else {
		hdr = (struct qb_ipc_request_header *)c->receive_buf;
		size = c->service->funcs.recv(&c->request, hdr, c->request.max_msg_size,
					      ms_timeout);
	}
	if (size < 0) {
		if (size != -EAGAIN) {
			qb_util_log(LOG_ERR, "%s(): %s", __func__, strerror(-res));
		} else {
			c->stats.recv_retries++;
		}
		res = size;
		goto cleanup;
	}
	c->stats.requests++;

	if (hdr->id == QB_IPC_MSG_DISCONNECT) {
		qb_util_log(LOG_DEBUG, "%s() QB_IPC_MSG_DISCONNECT", __func__);
		qb_ipcs_disconnect(c);
		res = -ESHUTDOWN;
	} else {
		res = c->service->serv_fns.msg_process(c, hdr, hdr->size);
		/* 0 == good, negitive == backoff */
		if (res < 0) {
			res = -ENOBUFS;
		} else {
			res = size;
		}
	}

	if (c->service->funcs.peek && c->service->funcs.reclaim) {
		c->service->funcs.reclaim(&c->request);
	}

cleanup:
	qb_ipcs_connection_ref_dec(c);
	return res;
}

#define IPC_REQUEST_TIMEOUT 10
#define MAX_RECV_MSGS 50

int32_t qb_ipcs_dispatch_service_request(int32_t fd, int32_t revents,
					 void *data)
{
	int32_t res = _process_request_((struct qb_ipcs_connection *)data,
					IPC_REQUEST_TIMEOUT);
	if (res > 0) {
		return 0;
	}
	return res;
}

static ssize_t _request_q_len_get(struct qb_ipcs_connection *c)
{
	ssize_t q_len;
	if (c->service->funcs.q_len_get) {
		q_len = c->service->funcs.q_len_get(&c->request);
		if (q_len < 0) {
			q_len = 1;
		}
		q_len = QB_MIN(q_len, MAX_RECV_MSGS);
		if (c->service->poll_priority == QB_LOOP_MED)
			q_len = QB_MIN(q_len, 5);
		if (c->service->poll_priority == QB_LOOP_LOW)
			q_len = 1;

	} else {
		q_len = 1;
	}
	return q_len;
}

int32_t qb_ipcs_dispatch_connection_request(int32_t fd, int32_t revents,
					    void *data)
{
	struct qb_ipcs_connection *c = (struct qb_ipcs_connection *)data;
	char bytes[MAX_RECV_MSGS];
	int32_t res;
	int32_t recvd = 0;
	ssize_t avail;

	if (revents & POLLHUP) {
		qb_util_log(LOG_DEBUG, "%s HUP conn:%p fd:%d", __func__, c, fd);
		qb_ipcs_disconnect(c);
		return -ESHUTDOWN;
	}
	avail = _request_q_len_get(c);
	do {
		res = _process_request_(c, IPC_REQUEST_TIMEOUT);
		if (res > 0 || res == -ENOBUFS || res == -EINVAL) {
			recvd++;
		}
		if (res > 0) {
			avail--;
		}
	} while (avail > 0 && res > 0);

	if (c->service->needs_sock_for_poll && recvd > 0) {
		(void)qb_ipc_us_recv(&c->setup, bytes, recvd, 0);
	}

	res = QB_MIN(0, res);
	if (res == -EAGAIN || res == -ENOBUFS) {
		res = 0;
	}
	if (res != 0) {
		qb_util_log(LOG_INFO, "%s returning %d : %s",
			    __func__, res, strerror(-res));
		qb_ipcs_connection_ref_dec(c);
	}

	return res;
}

void qb_ipcs_context_set(struct qb_ipcs_connection *c, void *context)
{
	c->context = context;
}

void *qb_ipcs_context_get(struct qb_ipcs_connection *c)
{
	return c->context;
}

int32_t qb_ipcs_connection_stats_get(qb_ipcs_connection_t *c,
				     struct qb_ipcs_connection_stats* stats,
				     int32_t clear_after_read)
{
	memcpy(stats, &c->stats, sizeof(struct qb_ipcs_connection_stats));
	if (clear_after_read) {
		memset(&c->stats, 0, sizeof(struct qb_ipcs_connection_stats));
		c->stats.client_pid = c->pid;
	}
	return 0;
}

int32_t qb_ipcs_stats_get(struct qb_ipcs_service* s,
			  struct qb_ipcs_stats* stats,
			  int32_t clear_after_read)
{
	memcpy(stats, &s->stats, sizeof(struct qb_ipcs_stats));
	if (clear_after_read) {
		memset(&s->stats, 0, sizeof(struct qb_ipcs_stats));
	}
	return 0;
}

