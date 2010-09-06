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
#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif /* HAVE_SYS_UN_H */
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <qb/qbipcs.h>
#include <qb/qbpoll.h>
#include "util_int.h"
#include "ipc_int.h"

#define SERVER_BACKLOG 5

#if defined(QB_LINUX) || defined(QB_SOLARIS)
#define QB_SUN_LEN(a) sizeof(*(a))
#else
#define QB_SUN_LEN(a) SUN_LEN(a)
#endif

static int32_t qb_ipcs_us_connection_acceptor(qb_handle_t handle,
					      int fd, int revent, void *data);

#ifdef SO_NOSIGPIPE
static void socket_nosigpipe(int32_t s)
{
	int32_t on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
}
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int32_t qb_ipc_us_send(int32_t s, const void *msg, size_t len)
{
	int32_t result;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *rbuf = (char *)msg;
	int32_t processed = 0;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_iovlen = 1;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;

#if !defined(QB_SOLARIS)
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;
#else
	msg_send.msg_accrights = NULL;
	msg_send.msg_accrightslen = 0;
#endif

retry_send:
	iov_send.iov_base = &rbuf[processed];
	iov_send.iov_len = len - processed;

	result = sendmsg(s, &msg_send, MSG_NOSIGNAL);
	if (result == -1 && errno == EAGAIN) {
		goto retry_send;
	}
	if (result == -1) {
		return -errno;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}
	return processed;
}

static ssize_t qb_ipc_us_recv_msghdr(int32_t s,
				     struct msghdr *hdr, const char *msg,
				     size_t len)
{
	int32_t result;
	int32_t processed = 0;

retry_recv:
	hdr->msg_iov->iov_base = (void *)&msg[processed];
	hdr->msg_iov->iov_len = len - processed;

	result = recvmsg(s, hdr, MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
	if (result == -1) {
		return -errno;
	}
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
	/* On many OS poll never return POLLHUP or POLLERR.
	 * EOF is detected when recvmsg return 0.
	 */
	if (result == 0) {
		return -errno;	//ENOTCONN
	}
#endif

	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert(processed == len);

	return processed;
}

int32_t qb_ipc_us_recv(int32_t s, void *msg, size_t len)
{
	struct msghdr msg_recv;
	struct iovec iov_recv;

	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#if !defined (QB_SOLARIS)
	msg_recv.msg_control = 0;
	msg_recv.msg_controllen = 0;
	msg_recv.msg_flags = 0;
#else
	msg_recv.msg_accrights = NULL;
	msg_recv.msg_accrightslen = 0;
#endif

	return qb_ipc_us_recv_msghdr(s, &msg_recv, msg, len);
}

static int32_t qb_ipcs_uc_recv_and_auth(struct qb_ipcs_connection *c)
{
	int32_t res = 0;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	char setup_msg[sizeof(struct mar_req_initial_setup)];

#ifdef QB_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE(sizeof(struct ucred))];
	int off = 0;
	int on = 1;
	struct ucred *cred;
#endif
	msg_recv.msg_flags = 0;
	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifdef QB_LINUX
	msg_recv.msg_control = (void *)cmsg_cred;
	msg_recv.msg_controllen = sizeof(cmsg_cred);
#endif
#ifdef QB_SOLARIS
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;
#endif /* QB_SOLARIS */

	iov_recv.iov_base = &setup_msg;
	iov_recv.iov_len = sizeof(struct mar_req_initial_setup);
#ifdef QB_LINUX
	setsockopt(c->sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
#endif

	res = qb_ipc_us_recv_msghdr(c->sock, &msg_recv, setup_msg,
					 sizeof(struct mar_req_initial_setup));

	if (res < 0) {
		goto cleanup_and_return;
	}
	if (res != sizeof(struct mar_req_initial_setup)) {
		res = -EIO;
		goto cleanup_and_return;
	}
	res = -EBADMSG;

	/*
	 * currently support getpeerucred, getpeereid, and SO_PASSCRED credential
	 * retrieval mechanisms for various Platforms
	 */
#ifdef HAVE_GETPEERUCRED
	/*
	 * Solaris and some BSD systems
	 */
	{
		ucred_t *uc = NULL;

		if (getpeerucred(c->sock, &uc) == 0) {
			res = 0;
			c->euid = ucred_geteuid(uc);
			c->egid = ucred_getegid(uc);
			c->pid = ucred_getpid(uc);
			ucred_free(uc);
		} else {
			res = -errno;
		}
	}
#elif HAVE_GETPEEREID
	/*
	 * Usually MacOSX systems
	 */
	{
		/*
		 * TODO get the peer's pid.
		 * c->pid = ?;
		 */
		if (getpeereid(c->sock, &c->euid, &c->egid) == 0) {
			res = 0;
		} else {
			res = -errno;
		}
	}

#elif SO_PASSCRED
	/*
	 * Usually Linux systems
	 */
	cmsg = CMSG_FIRSTHDR(&msg_recv);
	assert(cmsg);
	cred = (struct ucred *)CMSG_DATA(cmsg);
	if (cred) {
		res = 0;
		c->pid = cred->pid;
		c->euid = cred->uid;
		c->egid = cred->gid;
	} else {
		res = -EBADMSG;
	}
#else /* no credentials */
	res = -ENOTSUP;
#endif /* no credentials */

cleanup_and_return:

#ifdef QB_LINUX
	setsockopt(c->sock, SOL_SOCKET, SO_PASSCRED, &off, sizeof(off));
#endif

	if (res == 0) {
		res = -EACCES;
		if (c->service->serv_fns.connection_authenticate &&
				c->service->serv_fns.connection_authenticate(c->handle,
					c->euid,
					c->egid)) {
			res = 0;
		} else if (c->service->serv_fns.connection_authenticate == NULL) {
			res = 0;
		}
	}
	return res;
}

int32_t qb_ipcc_us_connect(const char *socket_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un address;

#if defined(QB_SOLARIS)
	request_fd = socket(PF_UNIX, SOCK_STREAM, 0);
#else
	request_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (request_fd == -1) {
		return -errno;
	}
#ifdef SO_NOSIGPIPE
	socket_nosigpipe(request_fd);
#endif

	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	address.sun_len = SUN_LEN(&address);
#endif

#if defined(QB_LINUX)
	sprintf(address.sun_path + 1, "%s", socket_name);
#else
	sprintf(address.sun_path, "%s/%s", SOCKETDIR, socket_name);
#endif
	if (connect(request_fd, (struct sockaddr *)&address,
		    QB_SUN_LEN(&address)) == -1) {
		goto error_connect;
	}

	*sock_pt = request_fd;
	return 0;

error_connect:
	close(request_fd);
	*sock_pt = -1;

	return -errno;
}

void qb_ipcc_us_disconnect(int32_t sock)
{
	shutdown(sock, SHUT_RDWR);
	close(sock);
}

#if 0

cs_error_t coroipcc_dispatch_get(hdb_handle_t handle, void **data, int timeout)
{
	struct pollfd ufds;
	int poll_events;
	char buf;
	struct ipc_instance *ipc_instance;
	char *data_addr;
	cs_error_t error = CS_OK;
	int res;

	error =
	    hdb_error_to_cs(hdb_handle_get
			    (&ipc_hdb, handle, (void **)&ipc_instance));
	if (error != CS_OK) {
		return (error);
	}

	*data = NULL;

	ufds.fd = ipc_instance->fd;
	ufds.events = POLLIN;
	ufds.revents = 0;

	poll_events = poll(&ufds, 1, timeout);
	if (poll_events == -1 && errno == EINTR) {
		error = CS_ERR_TRY_AGAIN;
		goto error_put;
	} else if (poll_events == -1) {
		error = CS_ERR_LIBRARY;
		goto error_put;
	} else if (poll_events == 0) {
		error = CS_ERR_TRY_AGAIN;
		goto error_put;
	}
	if (poll_events == 1 && (ufds.revents & (POLLERR | POLLHUP))) {
		error = CS_ERR_LIBRARY;
		goto error_put;
	}

	error = socket_recv(ipc_instance->fd, &buf, 1);
	assert(error == CS_OK);

	if (shared_mem_dispatch_bytes_left(ipc_instance) > 500000) {
		/*
		 * Notify coroipcs to flush any pending dispatch messages
		 */

		res =
		    ipc_sem_post(ipc_instance->control_buffer,
				 SEMAPHORE_REQUEST_OR_FLUSH_OR_EXIT);
		if (res != CS_OK) {
			error = CS_ERR_LIBRARY;
			goto error_put;
		}

	}

	data_addr = ipc_instance->dispatch_buffer;

	data_addr = &data_addr[ipc_instance->control_buffer->read];

	*data = (void *)data_addr;

	return (CS_OK);
error_put:
	hdb_handle_put(&ipc_hdb, handle);
	return (error);
}

#endif

/*
 **************************************************************************
 * SERVER
 */

int32_t qb_ipcs_us_publish(struct qb_ipcs_service * s)
{
	struct sockaddr_un un_addr;
	int32_t res;
	char error_str[100];

	/*
	 * Create socket for IPC clients, name socket, listen for connections
	 */
#if defined(QB_SOLARIS)
	s->server_sock = socket(PF_UNIX, SOCK_STREAM, 0);
#else
	s->server_sock = socket(PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (s->server_sock == -1) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Cannot create server socket: %s\n", error_str);
		return res;
	}

	res = fcntl(s->server_sock, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT,
			    "Could not set non-blocking operation on server socket: %s\n",
			    error_str);
		goto error_close;
	}

	memset(&un_addr, 0, sizeof(struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	un_addr.sun_len = SUN_LEN(&un_addr);
#endif

#if defined(QB_LINUX)
	sprintf(un_addr.sun_path + 1, "%s", s->name);
#else
	{
		struct stat stat_out;
		res = stat(SOCKETDIR, &stat_out);
		if (res == -1 || (res == 0 && !S_ISDIR(stat_out.st_mode))) {
			res = -errno;
			qb_util_log(LOG_CRIT,
				    "Required directory not present %s\n",
				    SOCKETDIR);
			goto error_close;
		}
		sprintf(un_addr.sun_path, "%s/%s", SOCKETDIR, s->name);
		unlink(un_addr.sun_path);
	}
#endif

	res =
	    bind(s->server_sock, (struct sockaddr *)&un_addr,
		 QB_SUN_LEN(&un_addr));
	if (res) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT,
			    "Could not bind AF_UNIX (%s): %s.\n",
			    un_addr.sun_path, error_str);
		goto error_close;
	}

	/*
	 * Allow eveyrone to write to the socket since the IPC layer handles
	 * security automatically
	 */
#if !defined(QB_LINUX)
	res = chmod(un_addr.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
	listen(s->server_sock, SERVER_BACKLOG);

	qb_poll_dispatch_add(s->poll_handle, s->server_sock,
			     POLLIN | POLLPRI | POLLNVAL,
			     s, qb_ipcs_us_connection_acceptor);
	return 0;

error_close:
	close(s->server_sock);
	return res;
}

int32_t qb_ipcs_us_withdraw(struct qb_ipcs_service * s)
{
	shutdown(s->server_sock, SHUT_RDWR);
	close(s->server_sock);
	return 0;
}

static int32_t qb_ipcs_us_connection_acceptor(qb_handle_t handle,
					      int fd, int revent, void *data)
{
	struct sockaddr_un un_addr;
	int32_t new_fd;
	struct qb_ipcs_connection *c;
	struct qb_ipcs_service *s = (struct qb_ipcs_service *)data;
	struct mar_res_initial_setup init_res;
	int32_t res;
	socklen_t addrlen = sizeof(struct sockaddr_un);
	char error_str[100];

retry_accept:
	new_fd = accept(fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Could not accept Library connection: %s\n",
			    error_str);
		return (0);	/* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = fcntl(new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Could not set non-blocking operation on library connection: %s\n",
			    error_str);
		close(new_fd);
		return (0);	/* This is an error, but -1 would indicate disconnect from poll loop */
	}

	/*
	 * Valid accept
	 */
	c = qb_ipcs_connection_alloc(s);
	c->sock = new_fd;

	res = qb_ipcs_uc_recv_and_auth(c);
	if (res == 0) {
		qb_util_log(LOG_INFO, "IPC credentials authenticated");

		qb_list_add(&c->list, &s->connections);

		init_res.hdr.id = QB_IPC_MSG_AUTHENTICATE;
		init_res.hdr.size = sizeof(init_res);
		init_res.hdr.error = 0;
		init_res.connection_type = s->type;
		init_res.session_id = c->handle;
		init_res.max_msg_size = s->max_msg_size;

		qb_ipc_us_send(c->sock, &init_res, init_res.hdr.size);
		if (s->needs_sock_for_poll) {
			qb_poll_dispatch_add(s->poll_handle, c->sock,
					     POLLIN | POLLPRI | POLLNVAL,
					     c, qb_ipcs_dispatch_connection_request);
		}

	} else {
		if (res == -EACCES) {
			qb_util_log(LOG_ERR, "Invalid IPC credentials.");
		} else {
			strerror_r(-res, error_str, 100);
			qb_util_log(LOG_ERR, "Error in conection setup: %s.",
					error_str);
		}
		init_res.hdr.id = QB_IPC_MSG_AUTHENTICATE;
		init_res.hdr.size = sizeof(init_res);
		init_res.hdr.error = res;
		init_res.session_id = 0;

		qb_ipc_us_send(c->sock, &init_res, init_res.hdr.size);
		qb_ipcs_disconnect(c);
	}

	return (0);
}
