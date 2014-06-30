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

struct ipc_auth_ugp {
	uid_t uid;
	gid_t gid;
	pid_t pid;
};

static int32_t qb_ipcs_us_connection_acceptor(int fd, int revent, void *data);
static int32_t qb_ipcs_us_connection_authenticator(int fd, int revent, void *data);

ssize_t
qb_ipc_us_send(struct qb_ipc_one_way *one_way, const void *msg, size_t len)
{
	int32_t result;
	int32_t processed = 0;
	char *rbuf = (char *)msg;

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

retry_send:
	result = send(one_way->u.us.sock,
		      &rbuf[processed], len - processed, MSG_NOSIGNAL);

	if (result == -1) {
		if (errno == EAGAIN && processed > 0) {
			goto retry_send;
		} else {
			qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
			return -errno;
		}
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}

	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);

	return processed;
}

static ssize_t
qb_ipc_us_recv_msghdr(int32_t s, struct msghdr *hdr, char *msg, size_t len)
{
	int32_t result;
	int32_t processed = 0;

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

retry_recv:
	hdr->msg_iov->iov_base = &msg[processed];
	hdr->msg_iov->iov_len = len - processed;

	result = recvmsg(s, hdr, MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
	if (result == -1) {
		qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
		return -errno;
	}
	if (result == 0) {
		qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
		qb_util_log(LOG_DEBUG,
			    "recv(fd %d) got 0 bytes assuming ENOTCONN", s);
		return -ENOTCONN;
	}

	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
	assert(processed == len);

	return processed;
}

int32_t
qb_ipc_us_sock_error_is_disconnected(int err)
{
	if (err >= 0) {
		return QB_FALSE;
	} else if (err == -EAGAIN ||
	    err == -ETIMEDOUT ||
	    err == -EINTR ||
#ifdef EWOULDBLOCK
	    err == -EWOULDBLOCK ||
#endif
	    err == -EMSGSIZE ||
	    err == -ENOMSG ||
	    err == -EINVAL) {
		return QB_FALSE;
	}
	return QB_TRUE;
}

int32_t
qb_ipc_us_ready(struct qb_ipc_one_way * ow_data,
		struct qb_ipc_one_way * ow_conn,
		int32_t ms_timeout, int32_t events)
{
	struct pollfd ufds[2];
	int32_t poll_events;
	int numfds = 1;
	int i;

	ufds[0].fd = ow_data->u.us.sock;
	ufds[0].events = events;
	ufds[0].revents = 0;

	if (ow_conn && ow_data != ow_conn) {
		numfds++;
		ufds[1].fd = ow_conn->u.us.sock;
		ufds[1].events = POLLIN;
		ufds[1].revents = 0;
	}
	poll_events = poll(ufds, numfds, ms_timeout);
	if ((poll_events == -1 && errno == EINTR) || poll_events == 0) {
		return -EAGAIN;
	} else if (poll_events == -1) {
		return -errno;
	}
	for (i = 0; i < poll_events; i++) {
		if (ufds[i].revents & POLLERR) {
			qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLERR",
				    ufds[i].fd);
			return -ENOTCONN;
		} else if (ufds[i].revents & POLLHUP) {
			qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLHUP",
				    ufds[i].fd);
			return -ENOTCONN;
		} else if (ufds[i].revents & POLLNVAL) {
			qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLNVAL",
				    ufds[i].fd);
			return -ENOTCONN;
		} else if (ufds[i].revents == 0) {
			qb_util_log(LOG_DEBUG, "poll(fd %d) zero revents",
				    ufds[i].fd);
			return -ENOTCONN;
		}
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
	int32_t final_rc = 0;
	int32_t processed = 0;
	int32_t to_recv = len;
	char *data = msg;

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

retry_recv:
	result = recv(one_way->u.us.sock, &data[processed], to_recv,
		      MSG_NOSIGNAL | MSG_WAITALL);

	if (result == -1) {
		if (errno == EAGAIN && (processed > 0 || timeout == -1)) {
			result = qb_ipc_us_ready(one_way, NULL, timeout, POLLIN);
			if (result == 0 || result == -EAGAIN) {
				goto retry_recv;
			}
			final_rc = result;
			goto cleanup_sigpipe;
		} else if (errno == ECONNRESET || errno == EPIPE) {
			final_rc = -ENOTCONN;
			goto cleanup_sigpipe;
		} else {
			final_rc = -errno;
			goto cleanup_sigpipe;
		}
	}

	if (result == 0) {
		final_rc = -ENOTCONN;
		goto cleanup_sigpipe;
	}
	processed += result;
	to_recv -= result;
	if (processed != len) {
		goto retry_recv;
	}
	final_rc = processed;

cleanup_sigpipe:
	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);
	return final_rc;
}

static int32_t
qb_ipcc_stream_sock_connect(const char *socket_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un address;
	int32_t res = 0;

	request_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (request_fd == -1) {
		return -errno;
	}

	qb_socket_nosigpipe(request_fd);

	res = qb_sys_fd_nonblock_cloexec_set(request_fd);
	if (res < 0) {
		goto error_connect;
	}

	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	address.sun_len = QB_SUN_LEN(&address);
#endif

#if defined(QB_LINUX) || defined(QB_CYGWIN)
	snprintf(address.sun_path + 1, UNIX_PATH_MAX - 1, "%s", socket_name);
#else
	snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s", SOCKETDIR,
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
#ifdef QB_LINUX
	int off = 0;
	int on = 1;
#endif

	res = qb_ipcc_stream_sock_connect(c->name, &c->setup.u.us.sock);
	if (res != 0) {
		return res;
	}
#ifdef QB_LINUX
	setsockopt(c->setup.u.us.sock, SOL_SOCKET, SO_PASSCRED, &on,
		   sizeof(on));
#endif

	memset(&request, 0, sizeof(request));
	request.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	request.hdr.size = sizeof(request);
	request.max_msg_size = c->setup.max_msg_size;
	res = qb_ipc_us_send(&c->setup, &request, request.hdr.size);
	if (res < 0) {
		qb_ipcc_us_sock_close(c->setup.u.us.sock);
		return res;
	}
#ifdef QB_LINUX
	setsockopt(c->setup.u.us.sock, SOL_SOCKET, SO_PASSCRED, &off,
		   sizeof(off));
#endif

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

/*
 **************************************************************************
 * SERVER
 */

int32_t
qb_ipcs_us_publish(struct qb_ipcs_service * s)
{
	struct sockaddr_un un_addr;
	int32_t res;
#ifdef SO_PASSCRED
	int on = 1;
#endif

	/*
	 * Create socket for IPC clients, name socket, listen for connections
	 */
	s->server_sock = socket(PF_UNIX, SOCK_STREAM, 0);
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
#if defined(QB_LINUX) || defined(QB_CYGWIN)
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
		snprintf(un_addr.sun_path, sizeof(un_addr.sun_path), "%s/%s", SOCKETDIR,
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
#if !defined(QB_LINUX) && !defined(QB_CYGWIN)
	res = chmod(un_addr.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
#ifdef SO_PASSCRED
	setsockopt(s->server_sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
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
	(void)s->poll_fns.dispatch_del(s->server_sock);
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

	c->receive_buf = calloc(1, req->max_msg_size);
	if (c->receive_buf == NULL) {
		free(c);
		qb_ipcc_us_sock_close(sock);
		return -ENOMEM;
	}
	c->setup.u.us.sock = sock;
	c->request.max_msg_size = QB_MAX(req->max_msg_size, s->max_buffer_size);
	c->response.max_msg_size = QB_MAX(req->max_msg_size, s->max_buffer_size);
	c->event.max_msg_size = QB_MAX(req->max_msg_size, s->max_buffer_size);
	c->pid = ugp->pid;
	c->auth.uid = c->euid = ugp->uid;
	c->auth.gid = c->egid = ugp->gid;
	c->auth.mode = 0600;
	c->stats.client_pid = ugp->pid;
	snprintf(c->description, CONNECTION_DESCRIPTION,
		 "%d-%d-%d", s->pid, ugp->pid, c->setup.u.us.sock);

	if (auth_result == 0 && c->service->serv_fns.connection_accept) {
		res = c->service->serv_fns.connection_accept(c,
							     c->euid, c->egid);
	}
	if (res != 0) {
		goto send_response;
	}

	qb_util_log(LOG_DEBUG, "IPC credentials authenticated (%s)",
		    c->description);

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
			qb_util_log(LOG_ERR, "Invalid IPC credentials (%s).",
				    c->description);
		} else if (res == -EAGAIN) {
			qb_util_log(LOG_WARNING, "Denied connection, is not ready (%s)",
				    c->description);
		} else {
			errno = -res;
			qb_util_perror(LOG_ERR,
				       "Error in connection setup (%s)",
				       c->description);
		}
		qb_ipcs_disconnect(c);
	}
	return res;
}

static int32_t
qb_ipcs_uc_recv_and_auth(int32_t sock, void *msg, size_t len,
			 struct ipc_auth_ugp *ugp)
{
	int32_t res = 0;
	struct msghdr msg_recv;
	struct iovec iov_recv;

#ifdef SO_PASSCRED
	char cmsg_cred[CMSG_SPACE(sizeof(struct ucred))];
	int off = 0;
	int on = 1;
#endif
	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifdef SO_PASSCRED
	msg_recv.msg_control = (void *)cmsg_cred;
	msg_recv.msg_controllen = sizeof(cmsg_cred);
#endif
#ifdef QB_SOLARIS
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;
#else
	msg_recv.msg_flags = 0;
#endif /* QB_SOLARIS */

	iov_recv.iov_base = msg;
	iov_recv.iov_len = len;
#ifdef SO_PASSCRED
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
		struct cmsghdr *cmsg;

		res = -EINVAL;
		for (cmsg = CMSG_FIRSTHDR(&msg_recv); cmsg != NULL;
		     cmsg = CMSG_NXTHDR(&msg_recv, cmsg)) {
			if (cmsg->cmsg_type != SCM_CREDENTIALS)
				continue;

			memcpy(&cred, CMSG_DATA(cmsg), sizeof(struct ucred));
			res = 0;
			ugp->pid = cred.pid;
			ugp->uid = cred.uid;
			ugp->gid = cred.gid;
			break;
		}
	}
#else /* no credentials */
	ugp->pid = 0;
	ugp->uid = 0;
	ugp->gid = 0;
	res = -ENOTSUP;
#endif /* no credentials */

cleanup_and_return:

#ifdef SO_PASSCRED
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
	socklen_t addrlen = sizeof(struct sockaddr_un);

	if (revent & (POLLNVAL | POLLHUP | POLLERR)) {
		/*
		 * handle shutdown more cleanly.
		 */
		return -1;
	}

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

	res = s->poll_fns.dispatch_add(s->poll_priority, new_fd,
				       POLLIN | POLLPRI | POLLNVAL,
				       s, qb_ipcs_us_connection_authenticator);
	if (res < 0) {
		close(new_fd);
		/* This is an error, but -1 would indicate disconnect
		 * from the poll loop
		 */
		return 0;
	}

	return 0;
}

static int32_t
qb_ipcs_us_connection_authenticator(int fd, int revent, void *data)
{
	struct qb_ipcs_service *s = (struct qb_ipcs_service *)data;
	int32_t res;
	struct qb_ipc_connection_request setup_msg;
	struct ipc_auth_ugp ugp;

	res = qb_ipcs_uc_recv_and_auth(fd, &setup_msg, sizeof(setup_msg),
				       &ugp);
	if (res < 0 || setup_msg.hdr.id != QB_IPC_MSG_AUTHENTICATE) {
		close(fd);
		goto cleanup;
	}

	res = handle_new_connection(s, res, fd, &setup_msg,
				    sizeof(setup_msg), &ugp);

cleanup:
	return res;
}
