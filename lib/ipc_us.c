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

/*
 * SUN_LEN() does a strlen() on sun_path, but if you are trying to use the
 * "Linux abstract namespace" (you have to set sun_path[0] == '\0') then
 * the strlen() doesn't work.
 */
#if defined(SUN_LEN)
#define QB_SUN_LEN(a) ((a)->sun_path[0] == '\0') ? sizeof(*(a)) : SUN_LEN(a)
#else
#define QB_SUN_LEN(a) sizeof(*(a))
#endif

struct ipc_us_control {
	int32_t sent;
	int32_t flow_control;
};
#define SHM_CONTROL_SIZE (3 * sizeof(struct ipc_us_control))

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
	int32_t processed = 0;
	char *rbuf = (char *)msg;

retry_send:
	result = send(one_way->u.us.sock,
		      &rbuf[processed],
		      len - processed,
		      MSG_NOSIGNAL);

	if (result == -1) {
		if (errno == EAGAIN && processed > 0) {
			goto retry_send;
		} else {
			return -errno;
		}
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}
	if (one_way->type == QB_IPC_SOCKET) {
		struct ipc_us_control *ctl = NULL;
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
	int32_t processed = 0;
	int32_t total_processed = 0;
	int32_t iov_p = 0;
	char *rbuf = (char *)iov[iov_p].iov_base;

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
	if (one_way->type == QB_IPC_SOCKET) {
		struct ipc_us_control *ctl;
		ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
		if (ctl) {
			qb_atomic_int_inc(&ctl->sent);
		}
	}
	return total_processed;
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
	if (result == 0) {
		qb_util_log(LOG_DEBUG,
			    "recv(fd %d) got 0 bytes assuming ENOTCONN", s);
		return -ENOTCONN;
	}

	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert(processed == len);

	return processed;
}

int32_t
qb_ipc_us_sock_error_is_disconnected(int err)
{
	if (err == -EAGAIN ||
	    err == -ETIMEDOUT ||
	    err == -EINTR ||
#ifdef EWOULDBLOCK
	    err == -EWOULDBLOCK ||
#endif
	    err == -EINVAL) {
		return QB_FALSE;
	}
	return QB_TRUE;
}

int32_t
qb_ipc_us_ready(struct qb_ipc_one_way * one_way,
		int32_t ms_timeout, int32_t events)
{
	struct pollfd ufds;
	int32_t poll_events;

	ufds.fd = one_way->u.us.sock;
	ufds.events = events;
	ufds.revents = 0;

	poll_events = poll(&ufds, 1, ms_timeout);
	if ((poll_events == -1 && errno == EINTR) || poll_events == 0) {
		return -EAGAIN;
	} else if (poll_events == -1) {
		return -errno;
	} else if (poll_events == 1 && (ufds.revents & POLLERR)) {
		qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLERR", one_way->u.us.sock);
		return -ENOTCONN;
	} else if (poll_events == 1 && (ufds.revents & POLLHUP)) {
		qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLHUP", one_way->u.us.sock);
		return -ENOTCONN;
	} else if (poll_events == 1 && (ufds.revents & POLLNVAL)) {
		qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLNVAL", one_way->u.us.sock);
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

retry_recv:
	result = recv(one_way->u.us.sock, &data[processed], to_recv,
		      MSG_NOSIGNAL | MSG_WAITALL);

	if (result == -1) {
		if (errno == EAGAIN &&
		    (processed > 0 || timeout == -1)) {
			goto retry_recv;
		} else if (errno == ECONNRESET || errno == EPIPE) {
			qb_util_perror(LOG_DEBUG,
				       "recv(fd %d) converting to ENOTCONN",
				       one_way->u.us.sock);
			return -ENOTCONN;
		} else {
			return -errno;
		}
	}

	if (result == 0) {
		qb_util_log(LOG_DEBUG,
			    "recv(fd %d) got 0 bytes assuming ENOTCONN",
			    one_way->u.us.sock);
		return -ENOTCONN;
	}
	processed += result;
	to_recv -= result;
	if (processed != len) {
		goto retry_recv;
	}
	if (one_way->type == QB_IPC_SOCKET) {
		struct ipc_us_control *ctl = NULL;
		ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
		if (ctl) {
			(void)qb_atomic_int_dec_and_test(&ctl->sent);
		}
	}
	return processed;
}

/*
 * recv a message of unknown size.
 */
static ssize_t
qb_ipc_us_recv_at_most(struct qb_ipc_one_way * one_way,
	       void *msg, size_t len, int32_t timeout)
{
	int32_t result;
	int32_t processed = 0;
	int32_t to_recv = sizeof(struct qb_ipc_request_header);
	char *data = msg;
	struct ipc_us_control *ctl = NULL;
	struct qb_ipc_request_header *hdr = NULL;

retry_recv:
	result = recv(one_way->u.us.sock, &data[processed], to_recv,
		      MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1) {
		if (errno == EAGAIN &&
		    (processed > 0 || timeout == -1)) {
			goto retry_recv;
		} else {
			return -errno;
		}
	} else if (result == 0) {
		qb_util_log(LOG_DEBUG,
			    "recv(fd %d) got 0 bytes assuming ENOTCONN",
			    one_way->u.us.sock);
		return -ENOTCONN;
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
	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;
	if (ctl) {
		(void)qb_atomic_int_dec_and_test(&ctl->sent);
	}
	return processed;
}


static int32_t
qb_ipcc_us_sock_connect(const char *socket_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un address;
	int32_t res = 0;

	request_fd = socket(PF_UNIX, SOCK_STREAM, 0);
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
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	address.sun_len = QB_SUN_LEN(&address);
#endif

#if defined(QB_LINUX) || defined(QB_CYGWIN)
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
#ifdef QB_LINUX
	int off = 0;
	int on = 1;
#endif

	res = qb_ipcc_us_sock_connect(c->name, &c->setup.u.us.sock);
	if (res != 0) {
		return res;
	}

#ifdef QB_LINUX
	setsockopt(c->setup.u.us.sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
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
	setsockopt(c->setup.u.us.sock, SOL_SOCKET, SO_PASSCRED, &off, sizeof(off));
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

static void
qb_ipcc_us_disconnect(struct qb_ipcc_connection *c)
{
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
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
	char * shm_ptr;

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
				       SHM_CONTROL_SIZE, O_RDWR);
	if (fd_hdr < 0) {
		res = -errno;
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
#if !defined(QB_LINUX) && !defined(QB_CYGWIN)
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

	c->receive_buf = calloc(1, req->max_msg_size);
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
	c->auth.uid = c->euid = ugp->uid;
	c->auth.gid = c->egid = ugp->gid;
	c->auth.mode = 0600;
	c->stats.client_pid = ugp->pid;
	snprintf(c->description, CONNECTION_DESCRIPTION,
		 "%d-%d-%d", s->pid, ugp->pid,
		 c->setup.u.us.sock);

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

	if (s->needs_sock_for_poll) {
		qb_ipcs_connection_ref(c);
		res = s->poll_fns.dispatch_add(s->poll_priority,
					       c->setup.u.us.sock,
					       POLLIN | POLLPRI | POLLNVAL,
					       c,
					       qb_ipcs_dispatch_connection_request);
		if (res < 0) {
			qb_util_log(LOG_ERR,
				    "Error adding socket to mainloop (%s).",
				    c->description);
		}
	}
	if (s->type == QB_IPC_SOCKET) {
		c->request.u.us.sock = c->setup.u.us.sock;
		c->response.u.us.sock = c->setup.u.us.sock;
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
		if (s->type != QB_IPC_SOCKET) {
			qb_ipcs_connection_ref(c);
			if (s->serv_fns.connection_created) {
				s->serv_fns.connection_created(c);
			}
			if (c->state == QB_IPCS_CONNECTION_ACTIVE) {
				c->state = QB_IPCS_CONNECTION_ESTABLISHED;
			}
			qb_ipcs_connection_unref(c);
		}
	} else {
		if (res == -EACCES) {
			qb_util_log(LOG_ERR, "Invalid IPC credentials (%s).",
				    c->description);
		} else {
			errno = -res;
			qb_util_perror(LOG_ERR, "Error in connection setup (%s)",
				       c->description);
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
	qb_ipcs_connection_ref(c);
	c->event.u.us.sock = sock;
	if (c->state == QB_IPCS_CONNECTION_ACTIVE) {
		c->state = QB_IPCS_CONNECTION_ESTABLISHED;
	}
	if (s->serv_fns.connection_created) {
		s->serv_fns.connection_created(c);
	}

	if (c->state == QB_IPCS_CONNECTION_ESTABLISHED &&
	    s->type == QB_IPC_SOCKET) {
		int32_t res;
		qb_ipcs_connection_ref(c);
		res = s->poll_fns.dispatch_add(s->poll_priority,
					       c->request.u.us.sock,
					       POLLIN | POLLPRI | POLLNVAL,
					       c,
					       qb_ipcs_dispatch_connection_request);
		if (res < 0) {
			qb_util_log(LOG_ERR,
				    "Error adding socket to mainloop (%s).",
				    c->description);
		}
	}

	qb_ipcs_connection_unref(c);
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
		for (cmsg = CMSG_FIRSTHDR(&msg_recv); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg_recv, cmsg)) {
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
	struct qb_ipc_connection_request setup_msg;
	struct ipc_auth_ugp ugp;
	socklen_t addrlen = sizeof(struct sockaddr_un);

	if (revent & (POLLNVAL|POLLHUP|POLLERR)) {
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

	res = qb_ipcs_uc_recv_and_auth(new_fd, &setup_msg, sizeof(setup_msg),
				       &ugp);
	if (res < 0) {
		close(new_fd);
		/* This is an error, but -1 would indicate disconnect
		 * from the poll loop
		 */
		return 0;
	}

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
	char * shm_ptr;

	qb_util_log(LOG_DEBUG, "connecting to client (%s)",
		    c->description);

	snprintf(r->request, NAME_MAX, "qb-%s-control-%s",
		 s->name, c->description);

	fd_hdr = qb_sys_mmap_file_open(path, r->request,
				       SHM_CONTROL_SIZE,
				       O_CREAT | O_TRUNC | O_RDWR);
	if (fd_hdr < 0) {
		res = -errno;
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

void
qb_ipcs_sockets_disconnect(struct qb_ipcs_connection *c)
{
	int sock = -1;

	qb_enter();
	if (c->service->needs_sock_for_poll && c->setup.u.us.sock > 0) {
		sock = c->setup.u.us.sock;
		qb_ipcc_us_sock_close(sock);
		c->setup.u.us.sock = -1;
	}
	if (c->request.type == QB_IPC_SOCKET) {
		sock = c->request.u.us.sock;
	}
	if (sock > 0) {
		(void)c->service->poll_fns.dispatch_del(sock);
		qb_ipcs_connection_unref(c);
	}
}

static void
qb_ipcs_us_disconnect(struct qb_ipcs_connection *c)
{
	qb_enter();
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	unlink(c->request.u.us.shared_file_name);

	qb_ipcc_us_sock_close(c->request.u.us.sock);
	qb_ipcc_us_sock_close(c->event.u.us.sock);
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
