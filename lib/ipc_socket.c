/*
 * Copyright (C) 2010,2013 Red Hat, Inc.
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

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif /* HAVE_SYS_UN_H */
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include <qb/qbatomic.h>
#include <qb/qbipcs.h>
#include <qb/qbloop.h>
#include <qb/qbdefs.h>

#include "util_int.h"
#include "ipc_int.h"

struct ipc_us_control {
	int32_t sent;
	int32_t flow_control;
};
#define SHM_CONTROL_SIZE (3 * sizeof(struct ipc_us_control))


/*
 * client functions
 * --------------------------------------------------------
 */
static void
qb_ipcc_us_disconnect(struct qb_ipcc_connection *c)
{
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	unlink(c->request.u.us.shared_file_name);
	close(c->request.u.us.sock);
	close(c->event.u.us.sock);
}

static ssize_t
qb_ipc_socket_send(struct qb_ipc_one_way *one_way,
		const void *msg_ptr, size_t msg_len)
{
	ssize_t rc = 0;
	struct ipc_us_control *ctl = NULL;

	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;

	rc = qb_ipc_us_send(one_way, msg_ptr, msg_len);

	if (rc == msg_len && ctl) {
		qb_atomic_int_inc(&ctl->sent);
	}

	return rc;
}

static ssize_t
qb_ipc_socket_sendv(struct qb_ipc_one_way *one_way, const struct iovec *iov,
		    size_t iov_len)
{
	int32_t result;
	int32_t processed = 0;
	int32_t total_processed = 0;
	int32_t iov_p = 0;
	struct ipc_us_control *ctl;
	char *rbuf = (char *)iov[iov_p].iov_base;

	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

retry_send:
	result = send(one_way->u.us.sock,
		      &rbuf[processed],
		      iov[iov_p].iov_len - processed,
		      MSG_NOSIGNAL);

	if (result == -1) {
		if (errno == EAGAIN &&
		    (processed > 0 || iov_p > 0)) {
			goto retry_send;
		} else {
			qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
			return -errno;
		}
	}

	processed += result;
	if (processed == iov[iov_p].iov_len) {
		iov_p++;
		total_processed += processed;
		if (iov_p < iov_len) {
			processed = 0;
			rbuf = (char *)iov[iov_p].iov_base;
			goto retry_send;
		}
	} else {
		goto retry_send;
	}

	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);

	if (total_processed > 0 && ctl) {
		qb_atomic_int_inc(&ctl->sent);
	}
	return total_processed;
}


/*
 * recv a message of unknown size.
 */
static ssize_t
qb_ipc_us_recv_at_most(struct qb_ipc_one_way * one_way,
	       void *msg, size_t len, int32_t timeout)
{
	int32_t result;
	int32_t final_rc = 0;
	int32_t processed = 0;
	int32_t to_recv = sizeof(struct qb_ipc_request_header);
	char *data = msg;
	struct ipc_us_control *ctl = NULL;
	struct qb_ipc_request_header *hdr = NULL;

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

retry_recv:
	result = recv(one_way->u.us.sock, &data[processed], to_recv,
		      MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1) {
		if (errno == EAGAIN &&
		    (processed > 0 || timeout == -1)) {
			/*
			 * Don't spin too hard else we can consume too
			 * much cpu.
			 */
			result = qb_ipc_us_ready(one_way,
						 100,
						 POLLIN);
			if (result == 0 || result == -EAGAIN) {
				goto retry_recv;
			}
			final_rc = result;
			goto cleanup_sigpipe;
		} else {
			final_rc = -errno;
			goto cleanup_sigpipe;
		}
	} else if (result == 0) {
		final_rc = -ENOTCONN;
		goto cleanup_sigpipe;
	}

	processed += result;
	if (processed >= sizeof(struct qb_ipc_request_header) && hdr == NULL) {
		hdr = (struct qb_ipc_request_header*)msg;
	}
	if (hdr) {
		to_recv = hdr->size - processed;
	} else {
		to_recv = len - processed;
	}
	if (to_recv > 0) {
		goto retry_recv;
	}
	final_rc = processed;

	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
	if (ctl) {
		(void)qb_atomic_int_dec_and_test(&ctl->sent);
	}

 cleanup_sigpipe:
	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
	return final_rc;
}


static void
qb_ipc_us_fc_set(struct qb_ipc_one_way *one_way, int32_t fc_enable)
{
	struct ipc_us_control *ctl =
	    (struct ipc_us_control *)one_way->u.us.shared_data;

	qb_util_log(LOG_TRACE, "setting fc to %d",
		    fc_enable);
	qb_atomic_int_set(&ctl->flow_control, fc_enable);
}

static int32_t
qb_ipc_us_fc_get(struct qb_ipc_one_way *one_way)
{
	struct ipc_us_control *ctl =
	    (struct ipc_us_control *)one_way->u.us.shared_data;

	return qb_atomic_int_get(&ctl->flow_control);
}

static ssize_t
qb_ipc_us_q_len_get(struct qb_ipc_one_way *one_way)
{
	struct ipc_us_control *ctl =
	    (struct ipc_us_control *)one_way->u.us.shared_data;
	return qb_atomic_int_get(&ctl->sent);
}

/*
 * setup:
 * send -> server
 * recv <- server
 * call us, we connect to the dgram sockets
 */
int32_t
qb_ipcc_us_connect(struct qb_ipcc_connection *c,
		   struct qb_ipc_connection_response *r)
{
	int32_t res;
	struct qb_ipc_event_connection_request request;
	char path[PATH_MAX];
	int32_t fd_hdr;
	char * shm_ptr;

	qb_atomic_init();

	c->needs_sock_for_poll = QB_FALSE;
	c->funcs.send = qb_ipc_socket_send;
	c->funcs.sendv = qb_ipc_socket_sendv;
	c->funcs.recv = qb_ipc_us_recv_at_most;
	c->funcs.fc_get = qb_ipc_us_fc_get;
	c->funcs.disconnect = qb_ipcc_us_disconnect;

	c->request.u.us.sock = c->setup.u.us.sock;
	c->response.u.us.sock = c->setup.u.us.sock;
	c->setup.u.us.sock = -1;

	fd_hdr = qb_sys_mmap_file_open(path, r->request,
				       SHM_CONTROL_SIZE, O_RDWR);
	if (fd_hdr < 0) {
		res = fd_hdr;
		errno = -fd_hdr;
		qb_util_perror(LOG_ERR, "couldn't open file for mmap");
		return res;
	}
	(void)strlcpy(c->request.u.us.shared_file_name, r->request, NAME_MAX);
	shm_ptr = mmap(0, SHM_CONTROL_SIZE,
		       PROT_READ | PROT_WRITE, MAP_SHARED, fd_hdr, 0);

	if (shm_ptr == MAP_FAILED) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't create mmap for header");
		goto cleanup_hdr;
	}
	c->request.u.us.shared_data = shm_ptr;
	c->response.u.us.shared_data = shm_ptr + sizeof(struct ipc_us_control);
	c->event.u.us.shared_data =  shm_ptr + (2 * sizeof(struct ipc_us_control));

	close(fd_hdr);

	res = qb_ipcc_us_sock_connect(c->name, &c->event.u.us.sock);
	if (res != 0) {
		goto cleanup_hdr;
	}

	memset(&request, 0, sizeof(request));
	request.hdr.id = QB_IPC_MSG_NEW_EVENT_SOCK;
	request.hdr.size = sizeof(request);
	request.connection = r->connection;
	res = qb_ipc_us_send(&c->event, &request, request.hdr.size);
	if (res < 0) {
		qb_ipcc_us_sock_close(c->event.u.us.sock);
		goto cleanup_hdr;
	}

	return 0;

cleanup_hdr:
	close(fd_hdr);
	unlink(r->request);
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	return res;
}


/*
 * service functions
 * --------------------------------------------------------
 */
static void
qb_ipcs_us_disconnect(struct qb_ipcs_connection *c)
{
	qb_enter();
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	unlink(c->request.u.us.shared_file_name);

	qb_ipcc_us_sock_close(c->request.u.us.sock);
	qb_ipcc_us_sock_close(c->event.u.us.sock);
}

static int32_t
qb_ipcs_us_connect(struct qb_ipcs_service *s,
		   struct qb_ipcs_connection *c,
		   struct qb_ipc_connection_response *r)
{
	char path[PATH_MAX];
	int32_t fd_hdr;
	int32_t res = 0;
	struct ipc_us_control *ctl;
	char * shm_ptr;

	qb_util_log(LOG_DEBUG, "connecting to client (%s)",
		    c->description);

	snprintf(r->request, NAME_MAX, "qb-%s-control-%s",
		 s->name, c->description);

	fd_hdr = qb_sys_mmap_file_open(path, r->request,
				       SHM_CONTROL_SIZE,
				       O_CREAT | O_TRUNC | O_RDWR);
	if (fd_hdr < 0) {
		res = fd_hdr;
		errno = -fd_hdr;
		qb_util_perror(LOG_ERR, "couldn't create file for mmap (%s)",
			       c->description);
		return res;
	}
	(void)strlcpy(r->request, path, PATH_MAX);
	(void)strlcpy(c->request.u.us.shared_file_name, r->request, NAME_MAX);
	res = chown(r->request, c->auth.uid, c->auth.gid);
	if (res != 0) {
		/* ignore res, this is just for the compiler warnings.
		 */
		res = 0;
	}
	res = chmod(r->request, c->auth.mode);
	if (res != 0) {
		/* ignore res, this is just for the compiler warnings.
		*/
		res = 0;
	}

	shm_ptr = mmap(0, SHM_CONTROL_SIZE,
		       PROT_READ | PROT_WRITE, MAP_SHARED, fd_hdr, 0);

	if (shm_ptr == MAP_FAILED) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't create mmap for header (%s)",
			       c->description);
		goto cleanup_hdr;
	}
	c->request.u.us.shared_data = shm_ptr;
	c->response.u.us.shared_data = shm_ptr + sizeof(struct ipc_us_control);
	c->event.u.us.shared_data =  shm_ptr + (2 * sizeof(struct ipc_us_control));

	ctl = (struct ipc_us_control *)c->request.u.us.shared_data;
	ctl->sent = 0;
	ctl->flow_control = 0;
	ctl = (struct ipc_us_control *)c->response.u.us.shared_data;
	ctl->sent = 0;
	ctl->flow_control = 0;
	ctl = (struct ipc_us_control *)c->event.u.us.shared_data;
	ctl->sent = 0;
	ctl->flow_control = 0;

	close(fd_hdr);
	return res;

cleanup_hdr:
	close(fd_hdr);
	unlink(r->request);
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	return res;
}


void
qb_ipcs_us_init(struct qb_ipcs_service *s)
{
	s->funcs.connect = qb_ipcs_us_connect;
	s->funcs.disconnect = qb_ipcs_us_disconnect;

	s->funcs.recv = qb_ipc_us_recv_at_most;
	s->funcs.peek = NULL;
	s->funcs.reclaim = NULL;
	s->funcs.send = qb_ipc_socket_send;
	s->funcs.sendv = qb_ipc_socket_sendv;

	s->funcs.fc_set = qb_ipc_us_fc_set;
	s->funcs.q_len_get = qb_ipc_us_q_len_get;

	s->needs_sock_for_poll = QB_FALSE;

	qb_atomic_init();
}
