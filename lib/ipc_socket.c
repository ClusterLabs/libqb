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

static void
set_sock_addr(struct sockaddr_un *address, const char *socket_name)
{
	memset(address, 0, sizeof(struct sockaddr_un));
	address->sun_family = AF_UNIX;
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	address->sun_len = QB_SUN_LEN(address);
#endif

#if defined(QB_LINUX) || defined(QB_CYGWIN)
	snprintf(address->sun_path + 1, UNIX_PATH_MAX - 1, "%s", socket_name);
#else
	snprintf(address->sun_path, sizeof(address->sun_path), "%s/%s", SOCKETDIR,
		 socket_name);
#endif
}

static int32_t
qb_ipc_dgram_sock_setup(const char *base_name,
			const char *service_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un local_address;
	int32_t res = 0;
	char sock_path[PATH_MAX];

	request_fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (request_fd == -1) {
		return -errno;
	}

	qb_socket_nosigpipe(request_fd);
	res = qb_sys_fd_nonblock_cloexec_set(request_fd);
	if (res < 0) {
		goto error_connect;
	}
	snprintf(sock_path, PATH_MAX, "%s-%s", base_name, service_name);
	set_sock_addr(&local_address, sock_path);
	res = bind(request_fd, (struct sockaddr *)&local_address,
		   sizeof(local_address));
	if (res < 0) {
		goto error_connect;
	}

	*sock_pt = request_fd;
	return 0;

error_connect:
	close(request_fd);
	*sock_pt = -1;

	return res;
}

static int32_t
set_sock_size(int sockfd, size_t max_msg_size)
{
	int32_t rc;
	unsigned int optval;
	socklen_t optlen = sizeof(optval);

	rc = getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &optval, &optlen);

	qb_util_log(LOG_TRACE, "%d: getsockopt(%d, needed:%d) actual:%d",
		rc, sockfd, max_msg_size, optval);

	/* The optvat <= max_msg_size check is weird...
	 * during testing it was discovered in some instances if the
	 * default optval is exactly equal to our max_msg_size, we couldn't
	 * actually send a message that large unless we explicilty set
	 * it using setsockopt... there is no good explaination for this. Most
	 * likely this is hitting some sort of "off by one" error in the kernel. */
	if (rc == 0 && optval <= max_msg_size) {
		optval = max_msg_size;
		optlen = sizeof(optval);
		rc = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &optval, optlen);
	}
	return rc;
}

static int32_t
dgram_verify_msg_size(size_t max_msg_size)
{
	int32_t rc = -1;
	int32_t sockets[2];
	int32_t tries = 0;
	int32_t write_passed = 0;
	int32_t read_passed = 0;
	char buf[max_msg_size];

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) < 0) {
		goto cleanup_socks;
	}

	if (set_sock_size(sockets[0], max_msg_size) != 0) {
		goto cleanup_socks;
	}
	if (set_sock_size(sockets[1], max_msg_size) != 0) {
		goto cleanup_socks;
	}

	for (tries = 0; tries < 3; tries++) {

		if (write_passed == 0) {
			rc = write(sockets[1], buf, max_msg_size);

			if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
				continue;
			} else if (rc == max_msg_size) {
				write_passed = 1;
			} else {
				break;
			}
		}

		if (read_passed == 0) {
			rc = read(sockets[0], buf, max_msg_size);

			if (rc < 0 && (errno == EAGAIN || errno == EINTR)) {
				continue;
			} else if (rc == max_msg_size) {
				read_passed = 1;
			} else {
				break;
			}
		}

		if (read_passed && write_passed) {
			rc = 0;
			break;
		}
	}


cleanup_socks:
	close(sockets[0]);
	close(sockets[1]);
	return rc;
}

int32_t
qb_ipcc_verify_dgram_max_msg_size(size_t max_msg_size)
{
	int32_t i;
	int32_t last = -1;

	if (dgram_verify_msg_size(max_msg_size) == 0) {
		return max_msg_size;
	}

	for (i = 1024; i < max_msg_size; i+=1024) {
		if (dgram_verify_msg_size(i) != 0) {
			break;
		}
		last = i;
	}

	return last;
}

/*
 * bind to "base_name-local_name"
 * connect to "base_name-remote_name"
 * output sock_pt
 */
static int32_t
qb_ipc_dgram_sock_connect(const char *base_name,
			  const char *local_name,
			  const char *remote_name,
			  int32_t max_msg_size, int32_t * sock_pt)
{
	char sock_path[PATH_MAX];
	struct sockaddr_un remote_address;
	int32_t res = qb_ipc_dgram_sock_setup(base_name, local_name,
					      sock_pt);
	if (res < 0) {
		return res;
	}

	snprintf(sock_path, PATH_MAX, "%s-%s", base_name, remote_name);
	set_sock_addr(&remote_address, sock_path);
	if (connect(*sock_pt, (struct sockaddr *)&remote_address,
		    QB_SUN_LEN(&remote_address)) == -1) {
		res = -errno;
		goto error_connect;
	}

	return set_sock_size(*sock_pt, max_msg_size);

error_connect:
	close(*sock_pt);
	*sock_pt = -1;

	return res;
}

static int32_t
_finish_connecting(struct qb_ipc_one_way *one_way)
{
	struct sockaddr_un remote_address;
	int res;
	int error;
	int retry = 0;

	set_sock_addr(&remote_address, one_way->u.us.sock_name);

	/* this retry loop is here to help connecting when trying to send
	 * an event right after connection setup.
	 */
	do {
		errno = 0;
		res = connect(one_way->u.us.sock,
			      (struct sockaddr *)&remote_address,
			      QB_SUN_LEN(&remote_address));
		if (res == -1) {
			error = -errno;
			qb_util_perror(LOG_DEBUG, "error calling connect()");
			retry++;
			usleep(100000);
		}
	} while (res == -1 && retry < 10);
	if (res == -1) {
		return error;
	}

	free(one_way->u.us.sock_name);
	one_way->u.us.sock_name = NULL;

	return set_sock_size(one_way->u.us.sock, one_way->max_msg_size);
}

/*
 * client functions
 * --------------------------------------------------------
 */
static void
qb_ipcc_us_disconnect(struct qb_ipcc_connection *c)
{
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	unlink(c->request.u.us.shared_file_name);
	qb_ipcc_us_sock_close(c->event.u.us.sock);
	qb_ipcc_us_sock_close(c->request.u.us.sock);
	qb_ipcc_us_sock_close(c->setup.u.us.sock);
}

static ssize_t
qb_ipc_socket_send(struct qb_ipc_one_way *one_way,
		   const void *msg_ptr, size_t msg_len)
{
	ssize_t rc = 0;
	struct ipc_us_control *ctl;
	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;

	if (one_way->u.us.sock_name) {
		rc = _finish_connecting(one_way);
		if (rc < 0) {
			qb_util_log(LOG_ERR, "socket connect-on-send");
			return rc;
		}
	}

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);
	rc = send(one_way->u.us.sock, msg_ptr, msg_len, MSG_NOSIGNAL);
	if (rc == -1) {
		rc = -errno;
		if (errno != EAGAIN && errno != ENOBUFS) {
			qb_util_perror(LOG_DEBUG, "socket_send:send");
		}
	}
	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);

	if (ctl && rc == msg_len) {
		qb_atomic_int_inc(&ctl->sent);
	}

	return rc;
}

static ssize_t
qb_ipc_socket_sendv(struct qb_ipc_one_way *one_way, const struct iovec *iov,
		    size_t iov_len)
{
	int32_t rc;
	struct ipc_us_control *ctl;
	ctl = (struct ipc_us_control *)one_way->u.us.shared_data;

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

	if (one_way->u.us.sock_name) {
		rc = _finish_connecting(one_way);
		if (rc < 0) {
			qb_util_perror(LOG_ERR, "socket connect-on-sendv");
			return rc;
		}
	}

	rc = writev(one_way->u.us.sock, iov, iov_len);

	if (rc == -1) {
		rc = -errno;
		if (errno != EAGAIN && errno != ENOBUFS) {
			qb_util_perror(LOG_DEBUG, "socket_sendv:writev %d",
				       one_way->u.us.sock);
		}
	}

	qb_sigpipe_ctl(QB_SIGPIPE_DEFAULT);

	if (ctl && rc > 0) {
		qb_atomic_int_inc(&ctl->sent);
	}
	return rc;
}

/*
 * recv a message of unknown size.
 */
static ssize_t
qb_ipc_us_recv_at_most(struct qb_ipc_one_way *one_way,
		       void *msg, size_t len, int32_t timeout)
{
	int32_t result;
	int32_t final_rc = 0;
	int32_t to_recv = 0;
	char *data = msg;
	struct ipc_us_control *ctl = NULL;
	int32_t time_waited = 0;
	int32_t time_to_wait = timeout;

	if (timeout == -1) {
		time_to_wait = 1000;
	}

	qb_sigpipe_ctl(QB_SIGPIPE_IGNORE);

retry_peek:
	result = recv(one_way->u.us.sock, data,
		      sizeof(struct qb_ipc_request_header),
		      MSG_NOSIGNAL | MSG_PEEK);

	if (result == -1) {
		if (errno == EAGAIN && (time_waited < timeout || timeout == -1)) {
			result = qb_ipc_us_ready(one_way, NULL,
						 time_to_wait, POLLIN);
			time_waited += time_to_wait;
			goto retry_peek;
		} else {
			return -errno;
		}
	}
	if (result >= sizeof(struct qb_ipc_request_header)) {
		struct qb_ipc_request_header *hdr = NULL;
		hdr = (struct qb_ipc_request_header *)msg;
		to_recv = hdr->size;
	}

	result = recv(one_way->u.us.sock, data, to_recv,
		      MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1) {
		final_rc = -errno;
		goto cleanup_sigpipe;
	} else if (result == 0) {
		qb_util_log(LOG_DEBUG, "recv == 0 -> ENOTCONN");

		final_rc = -ENOTCONN;
		goto cleanup_sigpipe;
	}

	final_rc = result;

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

int32_t
qb_ipcc_us_connect(struct qb_ipcc_connection * c,
		   struct qb_ipc_connection_response * r)
{
	int32_t res;
	char path[PATH_MAX];
	int32_t fd_hdr;
	char *shm_ptr;

	qb_atomic_init();

	c->needs_sock_for_poll = QB_FALSE;
	c->funcs.send = qb_ipc_socket_send;
	c->funcs.sendv = qb_ipc_socket_sendv;
	c->funcs.recv = qb_ipc_us_recv_at_most;
	c->funcs.fc_get = qb_ipc_us_fc_get;
	c->funcs.disconnect = qb_ipcc_us_disconnect;

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
	fd_hdr = -1;

	res = qb_ipc_dgram_sock_connect(r->response, "response", "request",
					r->max_msg_size, &c->request.u.us.sock);
	if (res != 0) {
		goto cleanup_hdr;
	}
	c->response.u.us.sock = c->request.u.us.sock;

	res = qb_ipc_dgram_sock_connect(r->response, "event", "event-tx",
					r->max_msg_size, &c->event.u.us.sock);
	if (res != 0) {
		goto cleanup_hdr;
	}

	return 0;

cleanup_hdr:
	if (fd_hdr >= 0) {
		close(fd_hdr);
	}
	close(c->event.u.us.sock);
	close(c->request.u.us.sock);
	unlink(r->request);
	munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
	return res;
}

/*
 * service functions
 * --------------------------------------------------------
 */
static int32_t
_sock_connection_liveliness(int32_t fd, int32_t revents, void *data)
{
	struct qb_ipcs_connection *c = (struct qb_ipcs_connection *)data;

	qb_util_log(LOG_DEBUG, "LIVENESS: fd %d event %d conn (%s)",
		    fd, revents, c->description);
	if (revents & POLLNVAL) {
		qb_util_log(LOG_DEBUG, "NVAL conn (%s)", c->description);
		qb_ipcs_disconnect(c);
		return -EINVAL;
	}
	if (revents & POLLHUP) {
		qb_util_log(LOG_DEBUG, "HUP conn (%s)", c->description);
		qb_ipcs_disconnect(c);
		return -ESHUTDOWN;
	}

	/* If we actually get POLLIN for some reason here, it most
	 * certainly means EOF. Do a recv on the fd to detect eof and
	 * then disconnect */
	if (revents & POLLIN) {
		char buf[10];
		int res;

		res = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			res = -errno;
		} else if (res == 0) {
			qb_util_log(LOG_DEBUG, "EOF conn (%s)", c->description);
			res = -ESHUTDOWN;
		}

		if (res < 0) {
			qb_ipcs_disconnect(c);
			return res;
		}
	}

	return 0;
}

static int32_t
_sock_add_to_mainloop(struct qb_ipcs_connection *c)
{
	int res;

	res = c->service->poll_fns.dispatch_add(c->service->poll_priority,
						c->request.u.us.sock,
						POLLIN | POLLPRI | POLLNVAL,
						c,
						qb_ipcs_dispatch_connection_request);

	if (res < 0) {
		qb_util_log(LOG_ERR,
			    "Error adding socket to mainloop (%s).",
			    c->description);
		return res;
	}
	qb_ipcs_connection_ref(c);

	res = c->service->poll_fns.dispatch_add(c->service->poll_priority,
						c->setup.u.us.sock,
						POLLIN | POLLPRI | POLLNVAL,
						c, _sock_connection_liveliness);
	qb_util_log(LOG_DEBUG, "added %d to poll loop (liveness)",
		    c->setup.u.us.sock);
	if (res < 0) {
		qb_util_perror(LOG_ERR, "Error adding setupfd to mainloop");
		(void)c->service->poll_fns.dispatch_del(c->request.u.us.sock);
		return res;
	}
	qb_ipcs_connection_ref(c);
	return res;
}

static void
_sock_rm_from_mainloop(struct qb_ipcs_connection *c)
{
	(void)c->service->poll_fns.dispatch_del(c->request.u.us.sock);
	qb_ipcs_connection_unref(c);

	(void)c->service->poll_fns.dispatch_del(c->setup.u.us.sock);
	qb_ipcs_connection_unref(c);
}

static void
qb_ipcs_us_disconnect(struct qb_ipcs_connection *c)
{
	qb_enter();

	if (c->state == QB_IPCS_CONNECTION_ESTABLISHED ||
	    c->state == QB_IPCS_CONNECTION_ACTIVE) {
		_sock_rm_from_mainloop(c);

		qb_ipcc_us_sock_close(c->setup.u.us.sock);
		qb_ipcc_us_sock_close(c->request.u.us.sock);
		qb_ipcc_us_sock_close(c->event.u.us.sock);
	}
	if (c->state == QB_IPCS_CONNECTION_SHUTTING_DOWN ||
	    c->state == QB_IPCS_CONNECTION_ACTIVE) {
		munmap(c->request.u.us.shared_data, SHM_CONTROL_SIZE);
		unlink(c->request.u.us.shared_file_name);
	}
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
	char *shm_ptr;

	qb_util_log(LOG_DEBUG, "connecting to client (%s)", c->description);

	c->request.u.us.sock = c->setup.u.us.sock;
	c->response.u.us.sock = c->setup.u.us.sock;

	snprintf(r->request, NAME_MAX, "qb-%s-control-%s",
		 s->name, c->description);
	snprintf(r->response, NAME_MAX, "qb-%s-%s", s->name, c->description);

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
	fd_hdr = -1;

	/* request channel */
	res = qb_ipc_dgram_sock_setup(r->response, "request",
				      &c->request.u.us.sock);
	if (res < 0) {
		goto cleanup_hdr;
	}
	c->setup.u.us.sock_name = NULL;
	c->request.u.us.sock_name = NULL;

	/* response channel */
	c->response.u.us.sock = c->request.u.us.sock;
	snprintf(path, PATH_MAX, "%s-%s", r->response, "response");
	c->response.u.us.sock_name = strdup(path);

	/* event channel */
	res = qb_ipc_dgram_sock_setup(r->response, "event-tx",
				      &c->event.u.us.sock);
	if (res < 0) {
		goto cleanup_hdr;
	}
	snprintf(path, PATH_MAX, "%s-%s", r->response, "event");
	c->event.u.us.sock_name = strdup(path);

	res = _sock_add_to_mainloop(c);
	if (res < 0) {
		goto cleanup_hdr;
	}

	return res;

cleanup_hdr:
	free(c->response.u.us.sock_name);
	free(c->event.u.us.sock_name);

	if (fd_hdr >= 0) {
		close(fd_hdr);
	}
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
