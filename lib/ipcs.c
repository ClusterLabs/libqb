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
#include <qb/qbipcs.h>

static void qb_ipcs_destroy_internal(void *data);
static void qb_ipcs_disconnect_internal(void *data);

QB_HDB_DECLARE(qb_ipc_services, qb_ipcs_destroy_internal);
QB_HDB_DECLARE(qb_ipc_connections, qb_ipcs_disconnect_internal);

qb_ipcs_service_pt qb_ipcs_create(const char *name, enum qb_ipc_type type,
				  size_t max_msg_size)
{
	struct qb_ipcs_service *s;
	qb_ipcs_service_pt h;

	qb_hdb_handle_create(&qb_ipc_services,
			     sizeof(struct qb_ipcs_service), &h);
	qb_hdb_handle_get(&qb_ipc_services, h, (void **)&s);

	s->pid = getpid();
	s->type = type;
	s->max_msg_size = max_msg_size;
	s->receive_buf = malloc(s->max_msg_size);
	s->needs_sock_for_poll = QB_FALSE;

	qb_list_init(&s->connections);
	snprintf(s->name, 255, "%s", name);

	switch (s->type) {
	case QB_IPC_SOCKET:
	case QB_IPC_POSIX_MQ:
	case QB_IPC_SYSV_MQ:
	case QB_IPC_SHM:
		break;
	default:
		qb_hdb_handle_destroy(&qb_ipc_services, h);
		errno = EINVAL;
		h = 0;
		break;
	}
	qb_hdb_handle_put(&qb_ipc_services, h);

	return h;
}

void qb_ipcs_service_handlers_set(qb_ipcs_service_pt pt,
				  struct qb_ipcs_service_handlers *handlers)
{
	struct qb_ipcs_service *s;

	qb_hdb_handle_get(&qb_ipc_services, pt, (void **)&s);

	s->serv_fns.connection_authenticate = handlers->connection_authenticate;
	s->serv_fns.connection_created = handlers->connection_created;
	s->serv_fns.msg_process = handlers->msg_process;
	s->serv_fns.connection_destroyed = handlers->connection_destroyed;

	qb_hdb_handle_put(&qb_ipc_services, pt);
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
	free(s->receive_buf);
}

ssize_t qb_ipcs_response_send(qb_ipcs_connection_pt c, void *data, size_t size)
{
	ssize_t res;
	struct qb_ipcs_connection *con;

	res = qb_hdb_handle_get(&qb_ipc_connections, c, (void **)&con);
	if (res < 0) {
		return res;
	}
	res = con->service->funcs.response_send(con, data, size);
	qb_hdb_handle_put(&qb_ipc_connections, c);

	return res;
}

struct qb_ipcs_connection *qb_ipcs_connection_alloc(struct qb_ipcs_service *s)
{
	qb_ipcs_connection_pt h;
	struct qb_ipcs_connection *c;

	qb_hdb_handle_create(&qb_ipc_connections,
			     sizeof(struct qb_ipcs_connection), &h);
	qb_hdb_handle_get(&qb_ipc_connections, h, (void **)&c);

	c->handle = h;
	c->service = s;
	c->pid = 0;
	c->euid = -1;
	c->egid = -1;
	c->sock = -1;
	qb_list_init(&c->list);

	return c;
}

static void qb_ipcs_disconnect_internal(void *data)
{
	struct qb_ipcs_connection *c = (struct qb_ipcs_connection *)data;

	qb_util_log(LOG_DEBUG, "%s()", __func__);
	qb_list_del(&c->list);
	if (c->service->serv_fns.connection_destroyed) {
		c->service->serv_fns.connection_destroyed(c->handle);
	}
	c->service->funcs.disconnect(c);
	qb_ipcc_us_disconnect(c->sock);
}

void qb_ipcs_disconnect(struct qb_ipcs_connection *c)
{
	if (qb_hdb_handle_destroy(&qb_ipc_connections, c->handle) != 0)
		perror("qb_ipcs_disconnect:destroy");
	if (qb_hdb_handle_put(&qb_ipc_connections, c->handle) != 0)
		perror("qb_ipcs_disconnect:put");
}

static int32_t _process_request_(struct qb_ipcs_service *s)
{
	int32_t res = 0;
	struct qb_ipcs_connection *c = NULL;
	struct qb_ipc_request_header *hdr;

	hdr = (struct qb_ipc_request_header *)s->receive_buf;

get_msg_with_live_connection:
	res = s->funcs.request_recv(s, hdr, s->max_msg_size);
	if (res == -EAGAIN) {
		goto get_msg_with_live_connection;
	}
	if (res < 0) {
		goto cleanup;
	}
	if (qb_hdb_handle_get
	    (&qb_ipc_connections, hdr->session_id, (void **)&c) != 0) {
		qb_util_log(LOG_DEBUG,
			    "%s dropping message for expired connection.",
			    __func__);
		goto get_msg_with_live_connection;
	}

	switch (hdr->id) {
	case QB_IPC_MSG_CONNECT:
		qb_util_log(LOG_DEBUG, "%s() QB_IPC_MSG_CONNECT", __func__);
		if (s->funcs.connect(s, c, hdr, hdr->size) == 0) {
			if (s->serv_fns.connection_created) {
				s->serv_fns.connection_created(c->handle);
			}
		}
		break;

	case QB_IPC_MSG_DISCONNECT:
		qb_util_log(LOG_DEBUG, "%s() QB_IPC_MSG_DISCONNECT", __func__);
		qb_ipcs_disconnect(c);
		res = -ESHUTDOWN;
		break;

	case QB_IPC_MSG_NEW_MESSAGE:
	default:
		s->serv_fns.msg_process(c->handle, hdr, hdr->size);
		break;
	}
cleanup:
	qb_hdb_handle_put(&qb_ipc_connections, hdr->session_id);
	return res;
}

int32_t qb_ipcs_dispatch_service_request(qb_handle_t handle,
					 int32_t fd, int32_t revents,
					 void *data)
{
	return _process_request_((struct qb_ipcs_service *)data);
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

	return _process_request_(c->service);
}
