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
#include <poll.h>

#include "util_int.h"
#include "ipc_int.h"
#include <qb/qbdefs.h>
#include <qb/qbatomic.h>
#include <qb/qbipcs.h>

static void qb_ipcs_flowcontrol_set(struct qb_ipcs_connection *c,
				    int32_t fc_enable);
static int32_t
new_event_notification(struct qb_ipcs_connection * c);


qb_ipcs_service_t *
qb_ipcs_create(const char *name,
	       int32_t service_id,
	       enum qb_ipc_type type, struct qb_ipcs_service_handlers *handlers)
{
	struct qb_ipcs_service *s;

	s = calloc(1, sizeof(struct qb_ipcs_service));
	if (s == NULL) {
		return NULL;
	}
	if (type == QB_IPC_NATIVE) {
#ifdef DISABLE_IPC_SHM
		s->type = QB_IPC_SOCKET;
#else
		s->type = QB_IPC_SHM;
#endif /* DISABLE_IPC_SHM */
	} else {
		s->type = type;
	}

	s->pid = getpid();
	s->needs_sock_for_poll = QB_FALSE;
	s->poll_priority = QB_LOOP_MED;

	/* Initial alloc ref */
	qb_ipcs_ref(s);

	s->service_id = service_id;
	(void)strlcpy(s->name, name, NAME_MAX);

	s->serv_fns.connection_accept = handlers->connection_accept;
	s->serv_fns.connection_created = handlers->connection_created;
	s->serv_fns.msg_process = handlers->msg_process;
	s->serv_fns.connection_closed = handlers->connection_closed;
	s->serv_fns.connection_destroyed = handlers->connection_destroyed;

	qb_list_init(&s->connections);

	return s;
}

void
qb_ipcs_poll_handlers_set(struct qb_ipcs_service *s,
			  struct qb_ipcs_poll_handlers *handlers)
{
	s->poll_fns.job_add = handlers->job_add;
	s->poll_fns.dispatch_add = handlers->dispatch_add;
	s->poll_fns.dispatch_mod = handlers->dispatch_mod;
	s->poll_fns.dispatch_del = handlers->dispatch_del;
}

void
qb_ipcs_service_context_set(qb_ipcs_service_t* s,
			    void *context)
{
	s->context = context;
}

void *
qb_ipcs_service_context_get(qb_ipcs_service_t* s)
{
	return s->context;
}

int32_t
qb_ipcs_run(struct qb_ipcs_service *s)
{
	int32_t res = 0;

	if (s->poll_fns.dispatch_add == NULL ||
	    s->poll_fns.dispatch_mod == NULL ||
	    s->poll_fns.dispatch_del == NULL) {

		res = -EINVAL;
		goto run_cleanup;
	}

	switch (s->type) {
	case QB_IPC_SOCKET:
		qb_ipcs_us_init((struct qb_ipcs_service *)s);
		break;
	case QB_IPC_SHM:
#ifdef DISABLE_IPC_SHM
		res = -ENOTSUP;
#else
		qb_ipcs_shm_init((struct qb_ipcs_service *)s);
#endif /* DISABLE_IPC_SHM */
		break;
	case QB_IPC_POSIX_MQ:
	case QB_IPC_SYSV_MQ:
		res = -ENOTSUP;
		break;
	default:
		res = -EINVAL;
		break;
	}

	if (res == 0) {
		res = qb_ipcs_us_publish(s);
		if (res < 0) {
			(void)qb_ipcs_us_withdraw(s);
			goto run_cleanup;
		}
	}

run_cleanup:
	if (res < 0) {
		/* Failed to run services, removing initial alloc reference. */
		qb_ipcs_unref(s);
	}

	return res;
}

static int32_t
_modify_dispatch_descriptor_(struct qb_ipcs_connection *c)
{
	qb_ipcs_dispatch_mod_fn disp_mod = c->service->poll_fns.dispatch_mod;

	if (c->service->type == QB_IPC_SOCKET) {
		return disp_mod(c->service->poll_priority,
				c->event.u.us.sock,
				c->poll_events, c,
				qb_ipcs_dispatch_connection_request);
	} else {
		return disp_mod(c->service->poll_priority,
				c->setup.u.us.sock,
				c->poll_events, c,
				qb_ipcs_dispatch_connection_request);
	}
	return -EINVAL;
}

void
qb_ipcs_request_rate_limit(struct qb_ipcs_service *s,
			   enum qb_ipcs_rate_limit rl)
{
	struct qb_ipcs_connection *c;
	enum qb_loop_priority old_p = s->poll_priority;
	struct qb_list_head *pos;
	struct qb_list_head *n;

	switch (rl) {
	case QB_IPCS_RATE_FAST:
		s->poll_priority = QB_LOOP_HIGH;
		break;
	case QB_IPCS_RATE_SLOW:
	case QB_IPCS_RATE_OFF:
	case QB_IPCS_RATE_OFF_2:
		s->poll_priority = QB_LOOP_LOW;
		break;
	default:
	case QB_IPCS_RATE_NORMAL:
		s->poll_priority = QB_LOOP_MED;
		break;
	}

	qb_list_for_each_safe(pos, n, &s->connections) {

		c = qb_list_entry(pos, struct qb_ipcs_connection, list);
		qb_ipcs_connection_ref(c);

		if (rl == QB_IPCS_RATE_OFF) {
			qb_ipcs_flowcontrol_set(c, 1);
		} else if (rl == QB_IPCS_RATE_OFF_2) {
			qb_ipcs_flowcontrol_set(c, 2);
		} else {
			qb_ipcs_flowcontrol_set(c, QB_FALSE);
		}
		if (old_p != s->poll_priority) {
			(void)_modify_dispatch_descriptor_(c);
		}
		qb_ipcs_connection_unref(c);
	}
}

void
qb_ipcs_ref(struct qb_ipcs_service *s)
{
	qb_atomic_int_inc(&s->ref_count);
}

void
qb_ipcs_unref(struct qb_ipcs_service *s)
{
	int32_t free_it;

	assert(s->ref_count > 0);
	free_it = qb_atomic_int_dec_and_test(&s->ref_count);
	if (free_it) {
		qb_util_log(LOG_DEBUG, "%s() - destroying", __func__);
		free(s);
	}
}

void
qb_ipcs_destroy(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_list_head *pos;
	struct qb_list_head *n;

	if (s == NULL) {
		return;
	}
	qb_list_for_each_safe(pos, n, &s->connections) {
		c = qb_list_entry(pos, struct qb_ipcs_connection, list);
		if (c == NULL) {
			continue;
		}
		qb_ipcs_disconnect(c);
	}
	(void)qb_ipcs_us_withdraw(s);

	/* service destroyed, remove initial alloc ref */
	qb_ipcs_unref(s);
}

/*
 * connection API
 */
static struct qb_ipc_one_way *
_event_sock_one_way_get(struct qb_ipcs_connection * c)
{
	if (c->service->needs_sock_for_poll) {
		return &c->setup;
	}
	if (c->event.type == QB_IPC_SOCKET) {
		return &c->event;
	}
	return NULL;
}

static struct qb_ipc_one_way *
_response_sock_one_way_get(struct qb_ipcs_connection * c)
{
	if (c->service->needs_sock_for_poll) {
		return &c->setup;
	}
	if (c->response.type == QB_IPC_SOCKET) {
		return &c->response;
	}
	return NULL;
}

ssize_t
qb_ipcs_response_send(struct qb_ipcs_connection *c, const void *data,
		      size_t size)
{
	ssize_t res;

	if (c == NULL) {
		return -EINVAL;
	}
	qb_ipcs_connection_ref(c);
	res = c->service->funcs.send(&c->response, data, size);
	if (res == size) {
		c->stats.responses++;
	} else if (res == -EAGAIN || res == -ETIMEDOUT) {
		struct qb_ipc_one_way *ow = _response_sock_one_way_get(c);
		if (ow) {
			ssize_t res2 = qb_ipc_us_ready(ow, &c->setup, 0, POLLOUT);
			if (res2 < 0) {
				res = res2;
			}
		}
		c->stats.send_retries++;
	}
	qb_ipcs_connection_unref(c);

	return res;
}

ssize_t
qb_ipcs_response_sendv(struct qb_ipcs_connection * c, const struct iovec * iov,
		       size_t iov_len)
{
	ssize_t res;

	if (c == NULL) {
		return -EINVAL;
	}
	qb_ipcs_connection_ref(c);
	res = c->service->funcs.sendv(&c->response, iov, iov_len);
	if (res > 0) {
		c->stats.responses++;
	} else if (res == -EAGAIN || res == -ETIMEDOUT) {
		struct qb_ipc_one_way *ow = _response_sock_one_way_get(c);
		if (ow) {
			ssize_t res2 = qb_ipc_us_ready(ow, &c->setup, 0, POLLOUT);
			if (res2 < 0) {
				res = res2;
			}
		}
		c->stats.send_retries++;
	}
	qb_ipcs_connection_unref(c);

	return res;
}

static int32_t
resend_event_notifications(struct qb_ipcs_connection *c)
{
	ssize_t res = 0;

	if (!c->service->needs_sock_for_poll) {
		return res;
	}

	if (c->outstanding_notifiers > 0) {
		res = qb_ipc_us_send(&c->setup, c->receive_buf,
				     c->outstanding_notifiers);
	}
	if (res > 0) {
		c->outstanding_notifiers -= res;
	}

	assert(c->outstanding_notifiers >= 0);
	if (c->outstanding_notifiers == 0) {
		c->poll_events = POLLIN | POLLPRI | POLLNVAL;
		(void)_modify_dispatch_descriptor_(c);
	}
	return res;
}

static int32_t
new_event_notification(struct qb_ipcs_connection * c)
{
	ssize_t res = 0;

	if (!c->service->needs_sock_for_poll) {
		return res;
	}

	assert(c->outstanding_notifiers >= 0);
	if (c->outstanding_notifiers > 0) {
		c->outstanding_notifiers++;
		res = resend_event_notifications(c);
	} else {
		res = qb_ipc_us_send(&c->setup, &c->outstanding_notifiers, 1);
		if (res == -EAGAIN) {
			/*
			 * notify the client later, when we can.
			 */
			c->outstanding_notifiers++;
			c->poll_events = POLLOUT | POLLIN | POLLPRI | POLLNVAL;
			(void)_modify_dispatch_descriptor_(c);
		}
	}
	return res;
}

ssize_t
qb_ipcs_event_send(struct qb_ipcs_connection * c, const void *data, size_t size)
{
	ssize_t res;
	ssize_t resn;

	if (c == NULL) {
		return -EINVAL;
	} else if (size > c->event.max_msg_size) {
		return -EMSGSIZE;
	}

	qb_ipcs_connection_ref(c);
	res = c->service->funcs.send(&c->event, data, size);
	if (res == size) {
		c->stats.events++;
		resn = new_event_notification(c);
		if (resn < 0 && resn != -EAGAIN && resn != -ENOBUFS) {
			errno = -resn;
			qb_util_perror(LOG_DEBUG,
				       "new_event_notification (%s)",
				       c->description);
			res = resn;
		}
	} else if (res == -EAGAIN || res == -ETIMEDOUT) {
		struct qb_ipc_one_way *ow = _event_sock_one_way_get(c);

		if (c->outstanding_notifiers > 0) {
			resn = resend_event_notifications(c);
		}
		if (ow) {
			resn = qb_ipc_us_ready(ow, &c->setup, 0, POLLOUT);
			if (resn < 0) {
				res = resn;
			}
		}
		c->stats.send_retries++;
	}

	qb_ipcs_connection_unref(c);
	return res;
}

ssize_t
qb_ipcs_event_sendv(struct qb_ipcs_connection * c,
		    const struct iovec * iov, size_t iov_len)
{
	ssize_t res;
	ssize_t resn;

	if (c == NULL) {
		return -EINVAL;
	}
	qb_ipcs_connection_ref(c);

	res = c->service->funcs.sendv(&c->event, iov, iov_len);
	if (res > 0) {
		c->stats.events++;
		resn = new_event_notification(c);
		if (resn < 0 && resn != -EAGAIN) {
			errno = -resn;
			qb_util_perror(LOG_DEBUG,
				       "new_event_notification (%s)",
				       c->description);
			res = resn;
		}
	} else if (res == -EAGAIN || res == -ETIMEDOUT) {
		struct qb_ipc_one_way *ow = _event_sock_one_way_get(c);

		if (c->outstanding_notifiers > 0) {
			resn = resend_event_notifications(c);
		}
		if (ow) {
			resn = qb_ipc_us_ready(ow, &c->setup, 0, POLLOUT);
			if (resn < 0) {
				res = resn;
			}
		}
		c->stats.send_retries++;
	}

	qb_ipcs_connection_unref(c);
	return res;
}

qb_ipcs_connection_t *
qb_ipcs_connection_first_get(struct qb_ipcs_service * s)
{
	struct qb_ipcs_connection *c;

	if (qb_list_empty(&s->connections)) {
		return NULL;
	}

	c = qb_list_first_entry(&s->connections, struct qb_ipcs_connection,
				list);
	qb_ipcs_connection_ref(c);

	return c;
}

qb_ipcs_connection_t *
qb_ipcs_connection_next_get(struct qb_ipcs_service * s,
			    struct qb_ipcs_connection * current)
{
	struct qb_ipcs_connection *c;

	if (current == NULL ||
	    qb_list_is_last(&current->list, &s->connections)) {
		return NULL;
	}

	c = qb_list_first_entry(&current->list, struct qb_ipcs_connection,
				list);
	qb_ipcs_connection_ref(c);

	return c;
}

int32_t
qb_ipcs_service_id_get(struct qb_ipcs_connection * c)
{
	if (c == NULL) {
		return -EINVAL;
	}
	return c->service->service_id;
}

struct qb_ipcs_connection *
qb_ipcs_connection_alloc(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c =
	    calloc(1, sizeof(struct qb_ipcs_connection));

	if (c == NULL) {
		return NULL;
	}

	c->pid = 0;
	c->euid = -1;
	c->egid = -1;
	c->receive_buf = NULL;
	c->context = NULL;
	c->fc_enabled = QB_FALSE;
	c->state = QB_IPCS_CONNECTION_INACTIVE;
	c->poll_events = POLLIN | POLLPRI | POLLNVAL;

	c->setup.type = s->type;
	c->request.type = s->type;
	c->response.type = s->type;
	c->event.type = s->type;
	(void)strlcpy(c->description, "not set yet", CONNECTION_DESCRIPTION);

	/* initial alloc ref */
	qb_ipcs_connection_ref(c);

	/*
	 * The connection makes use of the service object. Give the connection
	 * a reference to the service so we know the service can never be destroyed
	 * until the connection is done with it.
	 */
	qb_ipcs_ref(s);
	c->service = s;
	qb_list_init(&c->list);

	return c;
}

void
qb_ipcs_connection_ref(struct qb_ipcs_connection *c)
{
	if (c) {
		qb_atomic_int_inc(&c->refcount);
	}
}

void
qb_ipcs_connection_unref(struct qb_ipcs_connection *c)
{
	int32_t free_it;

	if (c == NULL) {
		return;
	}
	if (c->refcount < 1) {
		qb_util_log(LOG_ERR, "ref:%d state:%d (%s)",
			    c->refcount, c->state, c->description);
		assert(0);
	}
	free_it = qb_atomic_int_dec_and_test(&c->refcount);
	if (free_it) {
		qb_list_del(&c->list);
		if (c->service->serv_fns.connection_destroyed) {
			c->service->serv_fns.connection_destroyed(c);
		}
		c->service->funcs.disconnect(c);
		/* Let go of the connection's reference to the service */
		qb_ipcs_unref(c->service);
		free(c->receive_buf);
		free(c);
	}
}

void
qb_ipcs_disconnect(struct qb_ipcs_connection *c)
{
	int32_t res = 0;
	qb_loop_job_dispatch_fn rerun_job;

	if (c == NULL) {
		return;
	}
	qb_util_log(LOG_DEBUG, "%s(%s) state:%d",
		    __func__, c->description, c->state);

	if (c->state == QB_IPCS_CONNECTION_ACTIVE) {
		c->service->funcs.disconnect(c);
		c->state = QB_IPCS_CONNECTION_INACTIVE;
		c->service->stats.closed_connections++;

		/* This removes the initial alloc ref */
		qb_ipcs_connection_unref(c);

		/* return early as it's an incomplete connection.
		 */
		return;
	}
	if (c->state == QB_IPCS_CONNECTION_ESTABLISHED) {
		c->service->funcs.disconnect(c);
		c->state = QB_IPCS_CONNECTION_SHUTTING_DOWN;
		c->service->stats.active_connections--;
		c->service->stats.closed_connections++;
	}
	if (c->state == QB_IPCS_CONNECTION_SHUTTING_DOWN) {
		int scheduled_retry = 0;
		res = 0;
		if (c->service->serv_fns.connection_closed) {
			res = c->service->serv_fns.connection_closed(c);
		}
		if (res != 0) {
			/* OK, so they want the connection_closed
			 * function re-run */
			rerun_job =
			    (qb_loop_job_dispatch_fn) qb_ipcs_disconnect;
			res = c->service->poll_fns.job_add(QB_LOOP_LOW,
							   c, rerun_job);
			if (res == 0) {
				/* this function is going to be called again.
				 * so hold off on the unref */
				scheduled_retry = 1;
			}
		}
		remove_tempdir(c->description);
		if (scheduled_retry == 0) {
			/* This removes the initial alloc ref */
			qb_ipcs_connection_unref(c);
		}
	}

}

static void
qb_ipcs_flowcontrol_set(struct qb_ipcs_connection *c, int32_t fc_enable)
{
	if (c == NULL) {
		return;
	}
	if (c->fc_enabled != fc_enable) {
		c->service->funcs.fc_set(&c->request, fc_enable);
		c->fc_enabled = fc_enable;
		c->stats.flow_control_state = fc_enable;
		c->stats.flow_control_count++;
	}
}

static int32_t
_process_request_(struct qb_ipcs_connection *c, int32_t ms_timeout)
{
	int32_t res = 0;
	ssize_t size;
	struct qb_ipc_request_header *hdr;

	if (c->service->funcs.peek && c->service->funcs.reclaim) {
		size = c->service->funcs.peek(&c->request, (void **)&hdr,
					      ms_timeout);
	} else {
		hdr = c->receive_buf;
		size = c->service->funcs.recv(&c->request,
					      hdr,
					      c->request.max_msg_size,
					      ms_timeout);
	}
	if (size < 0) {
		if (size != -EAGAIN && size != -ETIMEDOUT) {
			qb_util_perror(LOG_DEBUG,
				       "recv from client connection failed (%s)",
				       c->description);
		} else {
			c->stats.recv_retries++;
		}
		res = size;
		goto cleanup;
	} else if (size == 0 || hdr->id == QB_IPC_MSG_DISCONNECT) {
		qb_util_log(LOG_DEBUG, "client requesting a disconnect (%s)",
			    c->description);
		res = -ESHUTDOWN;
		goto cleanup;
	} else {
		c->stats.requests++;
		res = c->service->serv_fns.msg_process(c, hdr, hdr->size);
		/* 0 == good, negative == backoff */
		if (res < 0) {
			res = -ENOBUFS;
		} else {
			res = size;
		}
	}

	if (c && c->service->funcs.peek && c->service->funcs.reclaim) {
		c->service->funcs.reclaim(&c->request);
	}

cleanup:
	return res;
}

#define IPC_REQUEST_TIMEOUT 10
#define MAX_RECV_MSGS 50

static ssize_t
_request_q_len_get(struct qb_ipcs_connection *c)
{
	ssize_t q_len;
	if (c->service->funcs.q_len_get) {
		q_len = c->service->funcs.q_len_get(&c->request);
		if (q_len <= 0) {
			return q_len;
		}
		if (c->service->poll_priority == QB_LOOP_MED) {
			q_len = QB_MIN(q_len, 5);
		} else if (c->service->poll_priority == QB_LOOP_LOW) {
			q_len = 1;
		} else {
			q_len = QB_MIN(q_len, MAX_RECV_MSGS);
		}
	} else {
		q_len = 1;
	}
	return q_len;
}

int32_t
qb_ipcs_dispatch_connection_request(int32_t fd, int32_t revents, void *data)
{
	struct qb_ipcs_connection *c = (struct qb_ipcs_connection *)data;
	char bytes[MAX_RECV_MSGS];
	int32_t res = 0;
	int32_t res2;
	int32_t recvd = 0;
	ssize_t avail;

	if (revents & POLLNVAL) {
		qb_util_log(LOG_DEBUG, "NVAL conn (%s)", c->description);
		res = -EINVAL;
		goto dispatch_cleanup;
	}
	if (revents & POLLHUP) {
		qb_util_log(LOG_DEBUG, "HUP conn (%s)", c->description);
		res = -ESHUTDOWN;
		goto dispatch_cleanup;
	}

	if (revents & POLLOUT) {
		/* try resend events now that fd can write */
		res = resend_event_notifications(c);
		if (res < 0 && res != -EAGAIN) {
			errno = -res;
			qb_util_perror(LOG_WARNING,
				       "resend_event_notifications (%s)",
				       c->description);
		}
		/* nothing to read */
		if ((revents & POLLIN) == 0) {
			res = 0;
			goto dispatch_cleanup;
		}
	}
	if (c->fc_enabled) {
		res = 0;
		goto dispatch_cleanup;
	}
	avail = _request_q_len_get(c);

	if (c->service->needs_sock_for_poll && avail == 0) {
		res2 = qb_ipc_us_recv(&c->setup, bytes, 1, 0);
		if (qb_ipc_us_sock_error_is_disconnected(res2)) {
			errno = -res2;
			qb_util_perror(LOG_WARNING, "conn (%s) disconnected",
				       c->description);
			res = -ESHUTDOWN;
			goto dispatch_cleanup;
		} else {
			qb_util_log(LOG_WARNING,
				    "conn (%s) Nothing in q but got POLLIN on fd:%d (res2:%d)",
				    c->description, fd, res2);
			res = 0;
			goto dispatch_cleanup;
		}
	}

	do {
		res = _process_request_(c, IPC_REQUEST_TIMEOUT);

		if (res == -ESHUTDOWN) {
			goto dispatch_cleanup;
		}

		if (res > 0 || res == -ENOBUFS || res == -EINVAL) {
			recvd++;
		}
		if (res > 0) {
			avail--;
		}
	} while (avail > 0 && res > 0 && !c->fc_enabled);

	if (c->service->needs_sock_for_poll && recvd > 0) {
		res2 = qb_ipc_us_recv(&c->setup, bytes, recvd, -1);
		if (qb_ipc_us_sock_error_is_disconnected(res2)) {
			errno = -res2;
			qb_util_perror(LOG_ERR, "error receiving from setup sock (%s)", c->description);

			res = -ESHUTDOWN;
			goto dispatch_cleanup;
		}
	}

	res = QB_MIN(0, res);
	if (res == -EAGAIN || res == -ETIMEDOUT || res == -ENOBUFS) {
		res = 0;
	}
	if (res != 0) {
		if (res != -ENOTCONN) {
			/*
			 * Abnormal state (ENOTCONN is normal shutdown).
			 */
			errno = -res;
			qb_util_perror(LOG_ERR, "request returned error (%s)",
				       c->description);
		}
	}

dispatch_cleanup:
	if (res != 0) {
		qb_ipcs_disconnect(c);
	}
	return res;
}

void
qb_ipcs_context_set(struct qb_ipcs_connection *c, void *context)
{
	if (c == NULL) {
		return;
	}
	c->context = context;
}

void *
qb_ipcs_context_get(struct qb_ipcs_connection *c)
{
	if (c == NULL) {
		return NULL;
	}
	return c->context;
}

void *
qb_ipcs_connection_service_context_get(qb_ipcs_connection_t *c)
{
	if (c == NULL || c->service == NULL) {
		return NULL;
	}
	return c->service->context;
}

int32_t
qb_ipcs_connection_stats_get(qb_ipcs_connection_t * c,
			     struct qb_ipcs_connection_stats * stats,
			     int32_t clear_after_read)
{
	if (c == NULL) {
		return -EINVAL;
	}
	memcpy(stats, &c->stats, sizeof(struct qb_ipcs_connection_stats));
	if (clear_after_read) {
		memset(&c->stats, 0, sizeof(struct qb_ipcs_connection_stats_2));
		c->stats.client_pid = c->pid;
	}
	return 0;
}

struct qb_ipcs_connection_stats_2*
qb_ipcs_connection_stats_get_2(qb_ipcs_connection_t *c,
			       int32_t clear_after_read)
{
	struct qb_ipcs_connection_stats_2 * stats;

	if (c == NULL) {
		errno = EINVAL;
		return NULL;
	}
	stats = calloc(1, sizeof(struct qb_ipcs_connection_stats_2));
	if (stats == NULL) {
		return NULL;
	}

	memcpy(stats, &c->stats, sizeof(struct qb_ipcs_connection_stats_2));

	if (c->service->funcs.q_len_get) {
		stats->event_q_length = c->service->funcs.q_len_get(&c->event);
	} else {
		stats->event_q_length = 0;
	}
	if (clear_after_read) {
		memset(&c->stats, 0, sizeof(struct qb_ipcs_connection_stats_2));
		c->stats.client_pid = c->pid;
	}
	return stats;
}

int32_t
qb_ipcs_stats_get(struct qb_ipcs_service * s,
		  struct qb_ipcs_stats * stats, int32_t clear_after_read)
{
	if (s == NULL) {
		return -EINVAL;
	}
	memcpy(stats, &s->stats, sizeof(struct qb_ipcs_stats));
	if (clear_after_read) {
		memset(&s->stats, 0, sizeof(struct qb_ipcs_stats));
	}
	return 0;
}

void
qb_ipcs_connection_auth_set(qb_ipcs_connection_t *c, uid_t uid,
			    gid_t gid, mode_t mode)
{
	if (c) {
		c->auth.uid = uid;
		c->auth.gid = gid;
		c->auth.mode = mode;
	}
}

int32_t
qb_ipcs_connection_get_buffer_size(qb_ipcs_connection_t *c)
{
	if (c == NULL) {
		return -EINVAL;
	}

	/* request, response, and event shoud all have the same
	 * buffer size allocated. It doesn't matter which we return
	 * here. */
	return c->response.max_msg_size;
}

void qb_ipcs_enforce_buffer_size(qb_ipcs_service_t *s, uint32_t buf_size)
{
	if (s == NULL) {
		return;
	}
	s->max_buffer_size = buf_size;
}
