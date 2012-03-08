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
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include <qb/qbatomic.h>
#include <qb/qbipcs.h>
#include <qb/qbloop.h>
#include <qb/qbdefs.h>

#include "util_int.h"
#include "ipc_int.h"

#define SERVER_BACKLOG 5

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX    108
#endif /* UNIX_PATH_MAX */

#if defined(QB_LINUX) || defined(QB_SOLARIS)
#define QB_SUN_LEN(a) sizeof(*(a))
#else
#define QB_SUN_LEN(a) SUN_LEN(a)
#endif

struct ipc_us_control {
	int32_t sent;
	int32_t flow_control;
};

struct ipc_auth_ugp {
	uid_t uid;
	gid_t gid;
	pid_t pid;
};

static int32_t qb_ipcs_us_connection_acceptor(int fd, int revent, void *data);
static int32_t qb_ipc_us_fc_get(struct qb_ipc_one_way *one_way);

#ifdef SO_NOSIGPIPE
static void
socket_nosigpipe(int32_t s)
{
	int32_t on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
}
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

ssize_t
qb_ipc_us_send(struct qb_ipc_one_way *one_way, const void *msg, size_t len)
{
	int32_t result;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *rbuf = (char *)msg;
	int32_t processed = 0;
	struct ipc_us_control *ctl = NULL;

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

	result = sendmsg(one_way->u.us.sock, &msg_send, MSG_NOSIGNAL);
	if (result == -1) {
		return -errno;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}
	if (one_way->type == QB_IPC_SOCKET) {
		ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
		if (ctl) {
			qb_atomic_int_inc(&ctl->sent);
		}
	}
	return processed;
}

static ssize_t
qb_ipc_us_sendv(struct qb_ipc_one_way *one_way, const struct iovec *iov,
		size_t iov_len)
{
	int32_t result;
	struct msghdr msg_send;
	int32_t processed = 0;
	size_t len = 0;
	int32_t i;
	struct ipc_us_control *ctl = NULL;

	for (i = 0; i < iov_len; i++) {
		len += iov[i].iov_len;
	}
	msg_send.msg_iov = (struct iovec *)iov;
	msg_send.msg_iovlen = iov_len;
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
	result = sendmsg(one_way->u.us.sock, &msg_send, MSG_NOSIGNAL);
	if (result == -1) {
		return -errno;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}
	if (one_way->type == QB_IPC_SOCKET) {
		ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
		if (ctl) {
			qb_atomic_int_inc(&ctl->sent);
		}
	}
	return processed;
}

static ssize_t
qb_ipc_us_recv_msghdr(int32_t s, struct msghdr *hdr, char *msg, size_t len)
{
	int32_t result;
	int32_t processed = 0;

retry_recv:
	hdr->msg_iov->iov_base = &msg[processed];
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

int32_t
qb_ipc_us_recv_ready(struct qb_ipc_one_way * one_way, int32_t ms_timeout)
{
	struct pollfd ufds;
	int32_t poll_events;

	ufds.fd = one_way->u.us.sock;
	ufds.events = POLLIN;
	ufds.revents = 0;

	poll_events = poll(&ufds, 1, ms_timeout);
	if ((poll_events == -1 && errno == EINTR) || poll_events == 0) {
		return -EAGAIN;
	} else if (poll_events == -1) {
		return -errno;
	} else if (poll_events == 1 && (ufds.revents & (POLLERR | POLLHUP))) {
		return -ENOTCONN;
	}
	return 0;
}

/*
 * recv an entire message - and try hard to get all of it.
 */
ssize_t
qb_ipc_us_recv(struct qb_ipc_one_way * one_way,
	       void *msg, size_t len, int32_t timeout)
{
	int32_t result;
	int32_t processed = 0;
	int32_t to_recv = len;
	char *data = msg;
	struct ipc_us_control *ctl = NULL;

retry_recv:
	result = recv(one_way->u.us.sock, &data[processed], to_recv, MSG_NOSIGNAL | MSG_WAITALL);
	if (timeout == -1) {
		if (result == -1 && errno == EAGAIN) {
			goto retry_recv;
		}
	}
	if (result == 0) {
		return -ENOTCONN;
	}
	if (result == -1) {
		return -errno;
	}
	processed += result;
	to_recv -= result;
	if (processed != len) {
		goto retry_recv;
	}
	if (one_way->type == QB_IPC_SOCKET) {
		ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
		if (ctl) {
			(void)qb_atomic_int_dec_and_test(&ctl->sent);
		}
	}
	return processed;
}

/*
 * recv a message of unknow size.
 * (could use MSG_PEEK here)
 */
static ssize_t
qb_ipc_us_recv_at_most(struct qb_ipc_one_way * one_way,
	       void *msg, size_t len, int32_t timeout)
{
	int32_t result;
	struct ipc_us_control *ctl = NULL;

retry_recv:
	result = recv(one_way->u.us.sock, msg, len, MSG_NOSIGNAL | MSG_WAITALL);
	if (timeout == -1) {
		if (result == -1 && errno == EAGAIN) {
			goto retry_recv;
		}
	}
	if (result == 0) {
		return -ENOTCONN;
	}
	if (result == -1) {
		return -errno;
	}
	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
	if (ctl) {
		(void)qb_atomic_int_dec_and_test(&ctl->sent);
	}
	return result;
}


static int32_t
qb_ipcc_us_sock_connect(const char *socket_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un address;
	int32_t res = 0;

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
#endif /* SO_NOSIGPIPE */
	res = qb_sys_fd_nonblock_cloexec_set(request_fd);
	if (res < 0) {
		goto error_connect;
	}

	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	address.sun_len = SUN_LEN(&address);
#endif

#if defined(QB_LINUX)
	snprintf(address.sun_path + 1, UNIX_PATH_MAX - 1, "%s", socket_name);
#else
	snprintf(address.sun_path, UNIX_PATH_MAX, "%s/%s", SOCKETDIR,
		 socket_name);
#endif
	if (connect(request_fd, (struct sockaddr *)&address,
		    QB_SUN_LEN(&address)) == -1) {
		res = -errno;
		goto error_connect;
	}

	*sock_pt = request_fd;
	return 0;

error_connect:
	close(request_fd);
	*sock_pt = -1;

	return res;
}

void
qb_ipcc_us_sock_close(int32_t sock)
{
	shutdown(sock, SHUT_RDWR);
	close(sock);
}

int32_t
qb_ipcc_us_setup_connect(struct qb_ipcc_connection *c,
			 struct qb_ipc_connection_response *r)
{
	int32_t res;
	struct qb_ipc_connection_request request;

	res = qb_ipcc_us_sock_connect(c->name, &c->setup.u.us.sock);
	if (res != 0) {
		return res;
	}

	request.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	request.hdr.size = sizeof(request);
	request.max_msg_size = c->setup.max_msg_size;
	res = qb_ipc_us_send(&c->setup, &request, request.hdr.size);
	if (res < 0) {
		qb_ipcc_us_sock_close(c->setup.u.us.sock);
		return res;
	}

	res =
	    qb_ipc_us_recv(&c->setup, r,
			   sizeof(struct qb_ipc_connection_response), -1);
	if (res < 0) {
		return res;
	}

	if (r->hdr.error != 0) {
		return r->hdr.error;
	}
	return 0;
}

static void
qb_ipcc_us_disconnect(struct qb_ipcc_connection *c)
{
	munmap(c->request.u.us.shared_data, sizeof(struct ipc_us_control));
	unlink(c->request.u.us.shared_file_name);
	close(c->request.u.us.sock);
	close(c->event.u.us.sock);
}

int32_t
qb_ipcc_us_connect(struct qb_ipcc_connection *c,
		   struct qb_ipc_connection_response *r)
{
	int32_t res;
	struct qb_ipc_event_connection_request request;
	char path[PATH_MAX];
	int32_t fd_hdr;
	struct ipc_us_control *ctl;

	c->needs_sock_for_poll = QB_FALSE;
	c->funcs.send = qb_ipc_us_send;
	c->funcs.sendv = qb_ipc_us_sendv;
	c->funcs.recv = qb_ipc_us_recv_at_most;
	c->funcs.fc_get = qb_ipc_us_fc_get;
	c->funcs.disconnect = qb_ipcc_us_disconnect;

	c->request.u.us.sock = c->setup.u.us.sock;
	c->response.u.us.sock = c->setup.u.us.sock;
	c->setup.u.us.sock = -1;

	fd_hdr = qb_sys_mmap_file_open(path, r->request,
				       sizeof(struct ipc_us_control), O_RDWR);
	if (fd_hdr < 0) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't open file for mmap");
		return res;
	}
	(void)strlcpy(c->request.u.us.shared_file_name, r->request, NAME_MAX);
	c->request.u.us.shared_data = mmap(0,
					   sizeof(struct ipc_us_control),
					   PROT_READ | PROT_WRITE, MAP_SHARED,
					   fd_hdr, 0);

	if (c->request.u.us.shared_data == MAP_FAILED) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't create mmap for header");
		goto cleanup_hdr;
	}

	ctl = (struct ipc_us_control *)c->request.u.us.shared_data;
	ctl->sent = 0;
	ctl->flow_control = 0;

	close(fd_hdr);

	res = qb_ipcc_us_sock_connect(c->name, &c->event.u.us.sock);
	if (res != 0) {
		goto cleanup_hdr;
	}

	request.hdr.id = QB_IPC_MSG_NEW_EVENT_SOCK;
	request.hdr.size = sizeof(request);
	request.connection = r->connection;
	res = qb_ipc_us_send(&c->event, &request, request.hdr.size);
	if (res < 0) {
		qb_ipcc_us_sock_close(c->event.u.us.sock);
		return res;
	}

	return 0;

cleanup_hdr:
	close(fd_hdr);
	unlink(r->request);
	munmap(c->request.u.us.shared_data, sizeof(struct ipc_us_control));
	return res;
}

/*
 **************************************************************************
 * SERVER
 */

int32_t
qb_ipcs_us_publish(struct qb_ipcs_service * s)
{
	struct sockaddr_un un_addr;
	int32_t res;

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
		qb_util_perror(LOG_ERR, "Cannot create server socket");
		return res;
	}

	res = qb_sys_fd_nonblock_cloexec_set(s->server_sock);
	if (res < 0) {
		goto error_close;
	}

	memset(&un_addr, 0, sizeof(struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	un_addr.sun_len = SUN_LEN(&un_addr);
#endif

	qb_util_log(LOG_INFO, "server name: %s", s->name);
#if defined(QB_LINUX)
	snprintf(un_addr.sun_path + 1, UNIX_PATH_MAX - 1, "%s", s->name);
#else
	{
		struct stat stat_out;
		res = stat(SOCKETDIR, &stat_out);
		if (res == -1 || (res == 0 && !S_ISDIR(stat_out.st_mode))) {
			res = -errno;
			qb_util_log(LOG_CRIT,
				    "Required directory not present %s",
				    SOCKETDIR);
			goto error_close;
		}
		snprintf(un_addr.sun_path, UNIX_PATH_MAX, "%s/%s", SOCKETDIR,
			 s->name);
		unlink(un_addr.sun_path);
	}
#endif

	res = bind(s->server_sock, (struct sockaddr *)&un_addr,
		   QB_SUN_LEN(&un_addr));
	if (res) {
		res = -errno;
		qb_util_perror(LOG_ERR, "Could not bind AF_UNIX (%s)",
			       un_addr.sun_path);
		goto error_close;
	}

	/*
	 * Allow everyone to write to the socket since the IPC layer handles
	 * security automatically
	 */
#if !defined(QB_LINUX)
	res = chmod(un_addr.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
	if (listen(s->server_sock, SERVER_BACKLOG) == -1) {
		qb_util_perror(LOG_ERR, "socket listen failed");
	}

	res = s->poll_fns.dispatch_add(s->poll_priority, s->server_sock,
				       POLLIN | POLLPRI | POLLNVAL,
				       s, qb_ipcs_us_connection_acceptor);
	return res;

error_close:
	close(s->server_sock);
	return res;
}

int32_t
qb_ipcs_us_withdraw(struct qb_ipcs_service * s)
{
	qb_util_log(LOG_INFO, "withdrawing server sockets");
	shutdown(s->server_sock, SHUT_RDWR);
	close(s->server_sock);
	return 0;
}

static int32_t
handle_new_connection(struct qb_ipcs_service *s,
		      int32_t auth_result,
		      int32_t sock,
		      void *msg, size_t len, struct ipc_auth_ugp *ugp)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_ipc_connection_request *req = msg;
	int32_t res = auth_result;
	int32_t res2 = 0;
	struct qb_ipc_connection_response response;

	c = qb_ipcs_connection_alloc(s);
	if (c == NULL) {
		qb_ipcc_us_sock_close(sock);
		return -ENOMEM;
	}

	c->receive_buf = malloc(req->max_msg_size);
	if (c->receive_buf == NULL) {
		free(c);
		qb_ipcc_us_sock_close(sock);
		return -ENOMEM;
	}
	c->setup.u.us.sock = sock;
	c->request.max_msg_size = req->max_msg_size;
	c->response.max_msg_size = req->max_msg_size;
	c->event.max_msg_size = req->max_msg_size;
	c->pid = ugp->pid;
	c->euid = ugp->uid;
	c->egid = ugp->gid;
	c->stats.client_pid = ugp->pid;

	if (auth_result == 0 && c->service->serv_fns.connection_accept) {
		res = c->service->serv_fns.connection_accept(c,
							     c->euid, c->egid);
	}
	if (res != 0) {
		goto send_response;
	}

	qb_util_log(LOG_INFO, "IPC credentials authenticated");

	memset(&response, 0, sizeof(response));
	if (s->funcs.connect) {
		res = s->funcs.connect(s, c, &response);
		if (res != 0) {
			goto send_response;
		}
	}
	/*
	 * The connection is good, add it to the active connection list
	 */
	c->state = QB_IPCS_CONNECTION_ACTIVE;
	qb_list_add(&c->list, &s->connections);

	if (s->needs_sock_for_poll) {
		qb_ipcs_connection_ref(c);
		res = s->poll_fns.dispatch_add(s->poll_priority,
					       c->setup.u.us.sock,
					       POLLIN | POLLPRI | POLLNVAL,
					       c,
					       qb_ipcs_dispatch_connection_request);
	}
	if (s->type == QB_IPC_SOCKET) {
		c->request.u.us.sock = c->setup.u.us.sock;
		c->response.u.us.sock = c->setup.u.us.sock;
		res = s->poll_fns.dispatch_add(s->poll_priority,
					       c->request.u.us.sock,
					       POLLIN | POLLPRI | POLLNVAL,
					       c,
					       qb_ipcs_dispatch_connection_request);
		if (res < 0) {
			qb_util_log(LOG_ERR,
				    "Error adding socket to mainloop.");
		}
	}

send_response:
	response.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	response.hdr.size = sizeof(response);
	response.hdr.error = res;
	if (res == 0) {
		response.connection = (intptr_t) c;
		response.connection_type = s->type;
		response.max_msg_size = c->request.max_msg_size;
		s->stats.active_connections++;
	}

	res2 = qb_ipc_us_send(&c->setup, &response, response.hdr.size);
	if (res == 0 && res2 != response.hdr.size) {
		res = res2;
	}

	if (res == 0) {
		qb_ipcs_connection_ref(c);
		if (s->serv_fns.connection_created) {
			s->serv_fns.connection_created(c);
		}
		if (c->state == QB_IPCS_CONNECTION_ACTIVE) {
			c->state = QB_IPCS_CONNECTION_ESTABLISHED;
		}
		qb_ipcs_connection_unref(c);
	} else {
		if (res == -EACCES) {
			qb_util_log(LOG_ERR, "Invalid IPC credentials.");
		} else {
			qb_util_perror(LOG_ERR, "Error in connection setup");
		}
		qb_ipcs_disconnect(c);
	}
	return res;
}

static void
handle_connection_new_sock(struct qb_ipcs_service *s, int32_t sock, void *msg)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_ipc_event_connection_request *req = msg;

	c = (struct qb_ipcs_connection *)req->connection;
	c->event.u.us.sock = sock;
}

static int32_t
qb_ipcs_uc_recv_and_auth(int32_t sock, void *msg, size_t len,
			 struct ipc_auth_ugp *ugp)
{
	int32_t res = 0;
	struct msghdr msg_recv;
	struct iovec iov_recv;

#ifdef QB_LINUX
	char cmsg_cred[CMSG_SPACE(sizeof(struct ucred))];
	int off = 0;
	int on = 1;
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

	iov_recv.iov_base = msg;
	iov_recv.iov_len = len;
#ifdef QB_LINUX
	setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
#endif

	res = qb_ipc_us_recv_msghdr(sock, &msg_recv, msg, len);
	if (res < 0) {
		goto cleanup_and_return;
	}
	if (res != len) {
		res = -EIO;
		goto cleanup_and_return;
	}

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

		if (getpeerucred(sock, &uc) == 0) {
			res = 0;
			ugp->uid = ucred_geteuid(uc);
			ugp->gid = ucred_getegid(uc);
			ugp->pid = ucred_getpid(uc);
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
		if (getpeereid(sock, &ugp->uid, &ugp->gid) == 0) {
			res = 0;
		} else {
			res = -errno;
		}
	}

#elif SO_PASSCRED
	/*
	 * Usually Linux systems
	 */
	{
		struct ucred cred;
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg_recv);
		assert(cmsg != NULL);
		if (CMSG_DATA(cmsg)) {
			memcpy(&cred, CMSG_DATA(cmsg), sizeof(struct ucred));
			res = 0;
			ugp->pid = cred.pid;
			ugp->uid = cred.uid;
			ugp->gid = cred.gid;
		} else {
			res = -EBADMSG;
		}
	}
#else /* no credentials */
	ugp->pid = 0;
	ugp->uid = 0;
	ugp->gid = 0;
	res = -ENOTSUP;
#endif /* no credentials */

cleanup_and_return:

#ifdef QB_LINUX
	setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &off, sizeof(off));
#endif

	return res;
}

static int32_t
qb_ipcs_us_connection_acceptor(int fd, int revent, void *data)
{
	struct sockaddr_un un_addr;
	int32_t new_fd;
	struct qb_ipcs_service *s = (struct qb_ipcs_service *)data;
	int32_t res;
	struct qb_ipc_connection_request setup_msg;
	struct ipc_auth_ugp ugp;
	socklen_t addrlen = sizeof(struct sockaddr_un);

retry_accept:
	errno = 0;
	new_fd = accept(fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1 && errno == EBADF) {
		qb_util_perror(LOG_ERR,
			       "Could not accept client connection from fd:%d",
			       fd);
		return -1;
	}
	if (new_fd == -1) {
		qb_util_perror(LOG_ERR, "Could not accept client connection");
		/* This is an error, but -1 would indicate disconnect
		 * from the poll loop
		 */
		return 0;
	}

	res = qb_sys_fd_nonblock_cloexec_set(new_fd);
	if (res < 0) {
		close(new_fd);
		/* This is an error, but -1 would indicate disconnect
		 * from the poll loop
		 */
		return 0;
	}

	res = qb_ipcs_uc_recv_and_auth(new_fd, &setup_msg, sizeof(setup_msg),
				       &ugp);

	if (setup_msg.hdr.id == QB_IPC_MSG_AUTHENTICATE) {
		(void)handle_new_connection(s, res, new_fd, &setup_msg,
					    sizeof(setup_msg), &ugp);
	} else if (setup_msg.hdr.id == QB_IPC_MSG_NEW_EVENT_SOCK) {
		if (res == 0) {
			handle_connection_new_sock(s, new_fd, &setup_msg);
		} else {
			close(new_fd);
		}
	} else {
		close(new_fd);
	}

	return 0;
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

	qb_util_log(LOG_DEBUG, "connecting to client [%d]", c->pid);

	snprintf(r->request, NAME_MAX, "qb-%s-control-%d-%d",
		 s->name, c->pid, c->setup.u.us.sock);

	fd_hdr = qb_sys_mmap_file_open(path, r->request,
				       sizeof(struct ipc_us_control),
				       O_CREAT | O_TRUNC | O_RDWR);
	if (fd_hdr < 0) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't create file for mmap");
		return res;
	}
	(void)strlcpy(r->request, path, PATH_MAX);
	(void)strlcpy(c->request.u.us.shared_file_name, r->request, NAME_MAX);

	c->request.u.us.shared_data = mmap(0,
					   sizeof(struct ipc_us_control),
					   PROT_READ | PROT_WRITE, MAP_SHARED,
					   fd_hdr, 0);

	if (c->request.u.us.shared_data == MAP_FAILED) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't create mmap for header");
		goto cleanup_hdr;
	}

	ctl = (struct ipc_us_control *)c->request.u.us.shared_data;
	ctl->sent = 0;
	ctl->flow_control = 0;

	close(fd_hdr);
	return res;

cleanup_hdr:
	close(fd_hdr);
	unlink(r->request);
	munmap(c->request.u.us.shared_data, sizeof(struct ipc_us_control));
	return res;
}

static void
qb_ipc_us_fc_set(struct qb_ipc_one_way *one_way, int32_t fc_enable)
{
	struct ipc_us_control *ctl =
	    (struct ipc_us_control *)one_way->u.us.shared_data;

	qb_util_log(LOG_TRACE, "setting fc to %d", fc_enable);
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

static void
qb_ipcs_us_disconnect(struct qb_ipcs_connection *c)
{
	munmap(c->request.u.us.shared_data, sizeof(struct ipc_us_control));
	unlink(c->request.u.us.shared_file_name);

	close(c->request.u.us.sock);
	close(c->event.u.us.sock);
}

void
qb_ipcs_us_init(struct qb_ipcs_service *s)
{
	s->funcs.connect = qb_ipcs_us_connect;
	s->funcs.disconnect = qb_ipcs_us_disconnect;

	s->funcs.recv = qb_ipc_us_recv_at_most;
	s->funcs.peek = NULL;
	s->funcs.reclaim = NULL;
	s->funcs.send = qb_ipc_us_send;
	s->funcs.sendv = qb_ipc_us_sendv;

	s->funcs.fc_set = qb_ipc_us_fc_set;
	s->funcs.q_len_get = qb_ipc_us_q_len_get;

	s->needs_sock_for_poll = QB_FALSE;
}
