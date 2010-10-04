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
#include <qb/qbipcs.h>

static void qb_ipcs_destroy_internal(void *data);

QB_HDB_DECLARE(qb_ipc_services, qb_ipcs_destroy_internal);

qb_ipcs_service_pt qb_ipcs_create(const char *name,
				  int32_t service_id,
				  enum qb_ipc_type type,
				  struct qb_ipcs_service_handlers *handlers)
{
	struct qb_ipcs_service *s;
	qb_ipcs_service_pt h;

	qb_hdb_handle_create(&qb_ipc_services,
			     sizeof(struct qb_ipcs_service), &h);
	qb_hdb_handle_get(&qb_ipc_services, h, (void **)&s);

	s->pid = getpid();
	s->type = type;
	s->needs_sock_for_poll = QB_FALSE;

	s->service_id = service_id;
	strncpy(s->name, name, NAME_MAX);

	s->serv_fns.connection_accept = handlers->connection_accept;
	s->serv_fns.connection_created = handlers->connection_created;
	s->serv_fns.msg_process = handlers->msg_process;
	s->serv_fns.connection_destroyed = handlers->connection_destroyed;

	qb_list_init(&s->connections);

	qb_hdb_handle_put(&qb_ipc_services, h);

	return h;
}

void qb_ipcs_poll_handlers_set(qb_ipcs_service_pt pt,
			       struct qb_ipcs_poll_handlers *handlers)
{
	struct qb_ipcs_service *s;

	qb_hdb_handle_get(&qb_ipc_services, pt, (void **)&s);

	s->poll_fns.dispatch_add = handlers->dispatch_add;
	s->poll_fns.dispatch_rm = handlers->dispatch_rm;

	qb_hdb_handle_put(&qb_ipc_services, pt);
}

int32_t qb_ipcs_run(qb_ipcs_service_pt pt, qb_handle_t poll_handle)
{
	int32_t res;
	struct qb_ipcs_service *s;

	qb_hdb_handle_get(&qb_ipc_services, pt, (void **)&s);

	s->poll_handle = poll_handle;

	res = qb_ipcs_us_publish(s);
	if (res < 0) {
		qb_hdb_handle_put(&qb_ipc_services, pt);
		return res;
	}

	switch (s->type) {
	case QB_IPC_SOCKET:
		res = 0;
		break;
	case QB_IPC_SHM:
		res = qb_ipcs_shm_create((struct qb_ipcs_service *)s);
		break;
	case QB_IPC_POSIX_MQ:
		res = qb_ipcs_pmq_create((struct qb_ipcs_service *)s);
		break;
	case QB_IPC_SYSV_MQ:
		res = qb_ipcs_smq_create((struct qb_ipcs_service *)s);
		break;
	default:
		res = -EINVAL;
		break;
	}

	if (res < 0) {
		qb_ipcs_us_withdraw(s);
	}

	qb_hdb_handle_put(&qb_ipc_services, pt);
	return res;
}

void qb_ipcs_destroy(qb_ipcs_service_pt pt)
{
	qb_hdb_handle_put(&qb_ipc_services, pt);
	qb_hdb_handle_destroy(&qb_ipc_services, pt);
}

static void qb_ipcs_destroy_internal(void *data)
{
	struct qb_ipcs_service *s = (struct qb_ipcs_service *)data;
	s->funcs.destroy(s);
}

ssize_t qb_ipcs_response_send(struct qb_ipcs_connection *c, const void *data,
			      size_t size)
{
	ssize_t res;

	qb_ipcs_connection_ref_inc(c);
	res = c->service->funcs.send(&c->response, data, size);
	qb_ipcs_connection_ref_dec(c);

	return res;
}

ssize_t qb_ipcs_event_send(struct qb_ipcs_connection *c, const void *data,
			   size_t size)
{
	ssize_t res;

	qb_ipcs_connection_ref_inc(c);
	res = c->service->funcs.send(&c->event, data, size);

	if (c->service->needs_sock_for_poll) {
		qb_ipc_us_send(c->sock, data, 1);
	}

	qb_ipcs_connection_ref_dec(c);

	return res;
}


ssize_t qb_ipcs_event_sendv(qb_ipcs_connection_t *c, const struct iovec * iov, size_t iov_len)
{
	ssize_t res;

	qb_ipcs_connection_ref_inc(c);
	res = c->service->funcs.sendv(&c->event, iov, iov_len);

	if (c->service->needs_sock_for_poll) {
		qb_ipc_us_send(c->sock, &res, 1);
	}

	qb_ipcs_connection_ref_dec(c);

	return res;
}

struct qb_ipcs_connection *qb_ipcs_connection_alloc(struct qb_ipcs_service *s)
{
	struct qb_ipcs_connection *c = malloc(sizeof(struct qb_ipcs_connection));

	c->refcount = 1;
	c->service = s;
	c->pid = 0;
	c->euid = -1;
	c->egid = -1;
	c->sock = -1;
	qb_list_init(&c->list);
	c->receive_buf = NULL;

	return c;
}

void qb_ipcs_connection_ref_inc(struct qb_ipcs_connection *c)
{
	// lock
	c->refcount++;
	//qb_util_log(LOG_DEBUG, "%s() %d", __func__, c->refcount);
	// unlock
}

void qb_ipcs_connection_ref_dec(struct qb_ipcs_connection *c)
{
	// lock
	c->refcount--;
	//qb_util_log(LOG_DEBUG, "%s() %d", __func__, c->refcount);
	if (c->refcount == 0) {
		qb_util_log(LOG_DEBUG, "%s() %d", __func__, c->refcount);
		qb_list_del(&c->list);
		// unlock
		if (c->service->serv_fns.connection_destroyed) {
			c->service->serv_fns.connection_destroyed(c);
		}
		c->service->funcs.disconnect(c);
		qb_ipcc_us_disconnect(c->sock);
		if (c->receive_buf) {
			free(c->receive_buf);
		}
	} else {
		// unlock
	}
}

int32_t qb_ipcs_service_id_get(struct qb_ipcs_connection *c)
{
	return c->service->service_id;
}

void qb_ipcs_disconnect(struct qb_ipcs_connection *c)
{
	qb_util_log(LOG_DEBUG, "%s()", __func__);
	qb_ipcs_connection_ref_dec(c);
}

static int32_t _process_request_(struct qb_ipcs_connection *c)
{
	int32_t res = 0;
	struct qb_ipc_request_header *hdr;

	hdr = (struct qb_ipc_request_header *)c->receive_buf;

	qb_ipcs_connection_ref_inc(c);
get_msg_with_live_connection:
	res = c->service->funcs.recv(&c->request, hdr, c->request.max_msg_size);
	if (res == -EAGAIN) {
		goto get_msg_with_live_connection;
	}
	if (res < 0) {
		qb_util_log(LOG_DEBUG, "%s(): %s", __func__, strerror(-res));
		goto cleanup;
	}

	switch (hdr->id) {
	case QB_IPC_MSG_DISCONNECT:
		qb_util_log(LOG_DEBUG, "%s() QB_IPC_MSG_DISCONNECT", __func__);
		qb_ipcs_disconnect(c);
		res = -ESHUTDOWN;
		break;

	case QB_IPC_MSG_NEW_MESSAGE:
	default:
		c->service->serv_fns.msg_process(c, hdr, hdr->size);
		break;
	}
cleanup:
	qb_ipcs_connection_ref_dec(c);
	return res;
}

int32_t qb_ipcs_dispatch_service_request(qb_handle_t handle,
					 int32_t fd, int32_t revents,
					 void *data)
{
	return _process_request_((struct qb_ipcs_connection *)data);
}

int32_t qb_ipcs_dispatch_connection_request(qb_handle_t handle,
					    int32_t fd, int32_t revents,
					    void *data)
{
	struct qb_ipcs_connection *c = (struct qb_ipcs_connection *)data;
	char one_byte;

	if (revents & POLLHUP) {
		qb_util_log(LOG_DEBUG, "%s HUP", __func__);
		qb_ipcs_disconnect(c);
		return -ESHUTDOWN;
	}
	if (c->service->needs_sock_for_poll) {
		qb_ipc_us_recv(c->sock, &one_byte, 1);
	}

	return _process_request_(c);
}
