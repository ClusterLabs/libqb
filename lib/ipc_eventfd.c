/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#include "ipc_int.h"
#include "util_int.h"
#include "ringbuffer_int.h"
#include <qb/qbdefs.h>
#include <qb/qbatomic.h>
#include <qb/qbloop.h>
#include <qb/qbrb.h>

#ifdef HAVE_SYS_EVENTFD_H
#include <sys/eventfd.h>
#endif /*HAVE_SYS_EVENTFD_H*/


/*
 * read_eventfd: tracks bytes read
 *  - reclaim_fn writes to it (reader)
 *  - space_used reads it.
 *
 * write_eventfd: tracks bytes written.
 *  - post_fn writes to it (writer)
 *  - timedwait_fn polls and reads it (reader)
 *  - space_used reads it.
 *
 * reader:
 *  timedwait_fn: polls write_eventfd only
 *  reclaim_fn: 1) writes the amount read to read_eventfd
 *              2) reads/writes to write_eventfd (- amount read)
 *
 * writer:
 *  alloc calls get space_used_fn
 *    1) reads read_eventfd and adjusts space_used
 *    2) returns space_used .
 *  post_fn: writes to write_eventfd (+space_used)
 *  q_len_fn: walks the ringbuffer backwards until bad magic (TODO).
 */

static int32_t
_ipc_eventfd_timedwait(void * instance, int32_t ms_timeout)
{
	struct pollfd ufds;
	int32_t poll_events;
	struct qb_ipc_one_way *one_way = (struct qb_ipc_one_way *)instance;

	qb_enter();

	ufds.fd = one_way->u.shm.write_eventfd;
	ufds.events = POLLIN;
	ufds.revents = 0;

	poll_events = poll(&ufds, 1, ms_timeout);
	if ((poll_events == -1 && errno == EINTR) || poll_events == 0) {
		return -ETIMEDOUT;
	} else if (poll_events == -1) {
		if (errno == EAGAIN) {
			return -ETIMEDOUT;
		} else {
			return -errno;
		}
	} else if (poll_events == 1 && (ufds.revents & POLLERR)) {
		qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLERR", ufds.fd);
		return -ENOTCONN;
	} else if (poll_events == 1 && (ufds.revents & POLLHUP)) {
		qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLHUP", ufds.fd);
		return -ENOTCONN;
	} else if (poll_events == 1 && (ufds.revents & POLLNVAL)) {
		qb_util_log(LOG_DEBUG, "poll(fd %d) got POLLNVAL", ufds.fd);
		return -ENOTCONN;
	} else if (poll_events == 1 && (ufds.revents & POLLIN)) {
		return 0;
	}

	return -ETIMEDOUT;
}

static int32_t
_ipc_eventfd_reclaim(void * instance, size_t msg_len)
{
	int32_t res;
	uint64_t v = msg_len;
	struct qb_ipc_one_way *one_way = (struct qb_ipc_one_way *)instance;

	qb_util_log(LOG_TRACE, "reclaiming %d",
		    msg_len);

	/* 1) write the amount read from read_eventfd
	 */
	res = eventfd_write(one_way->u.shm.read_eventfd, v);
	if (res != 0) {
		qb_util_perror(LOG_TRACE, "eventfd write %d (%d)", msg_len, res);
	}
	if (errno == EPIPE) {
		res = -ENOTCONN;
	}

	/* 2) read the amount read from write_eventfd
	 *    this is so we can still have a usable poll()
	 *    on any remaining messages.
	 */
	res = eventfd_read(one_way->u.shm.write_eventfd, &v);
	if (res == 0) {
		eventfd_t msg_v = msg_len;
		eventfd_t old_v = v;
		if (v > msg_v) {
			v -= msg_v;
			res = eventfd_write(one_way->u.shm.write_eventfd, v);
			qb_util_log(LOG_DEBUG,
				    "reclaim_fn: reduced bytes written  %lld -> %lld (res:%d)",
				    old_v, v, res);
		}
	} else {
		res = -errno;
		qb_util_perror(LOG_ERR, "eventfd %d read write_eventfd %d",
			       one_way->u.shm.write_eventfd, res);
	}

	return res;
}


static int32_t
_ipc_eventfd_post(void * instance, size_t msg_len)
{
	int32_t res;
	uint64_t v = msg_len;
	struct qb_ipc_one_way *one_way = (struct qb_ipc_one_way *)instance;

	qb_enter();
	if (msg_len == 0) {
		return 0;
	}

	do {
		errno = 0;
		res = eventfd_write(one_way->u.shm.write_eventfd, v);
		if (res != 0) {
			qb_util_perror(LOG_DEBUG, "eventfd (%d) write %d (%d)",
				       one_way->u.shm.write_eventfd,
				       msg_len, res);
		}
	} while (res == -1 && errno == EAGAIN);
	if (errno == EPIPE) {
		res = -ENOTCONN;
	}
	if (res != 0) {
		qb_util_perror(LOG_ERR, "eventfd (%d) write %d failed! (%d)",
			       one_way->u.shm.write_eventfd,
			       msg_len, res);
	}
	one_way->u.shm.space_used += msg_len;
	return res;
}

static ssize_t
_ipc_eventfd_space_used_zero(void * instance)
{
	/* space used not needed on the reader side */
	return 0;
}

static ssize_t
_ipc_eventfd_space_used(void * instance)
{
	struct qb_ipc_one_way *one_way = (struct qb_ipc_one_way *)instance;
	eventfd_t v;
	int res;

/*
 *    1) reads read_eventfd and adjusts space_used
 *    2) returns space_used .
 */
	res = eventfd_read(one_way->u.shm.read_eventfd, &v);
	if (res == 0) {
		uint32_t was = one_way->u.shm.space_used;
		one_way->u.shm.space_used -= v;
		qb_util_log(LOG_TRACE, "space_used was %d, now %d",
			    was, one_way->u.shm.space_used);
	}

	return one_way->u.shm.space_used;
}


static int32_t
_ipc_eventfd_destroy(void * instance)
{
	struct qb_ipc_one_way *one_way = (struct qb_ipc_one_way *)instance;
	qb_enter();
	//(void)c->service->poll_fns.dispatch_del(c->request.u.shm.eventfd);
	close(one_way->u.shm.read_eventfd);
	close(one_way->u.shm.write_eventfd);
	return 0;
}


int32_t
qb_ipc_efd_create(struct qb_ipcs_service *s,
		      struct qb_ipcs_connection *c,
		      struct qb_ipc_one_way *one_way,
		      uint32_t flags,
		      struct qb_rb_notifier *notifier_cb)
{
#ifndef HAVE_EVENTFD
	return -ENOSYS;
#endif
	qb_enter();

	if ((flags & QB_RB_FLAG_OVERWRITE) == QB_RB_FLAG_OVERWRITE) {
		return -ENOSYS;
	}

	if ((flags & QB_RB_FLAG_CREATE) == 0) {
		notifier_cb->post_fn =		_ipc_eventfd_post;
		notifier_cb->reclaim_fn =	_ipc_eventfd_reclaim;
		notifier_cb->q_len_fn =		NULL;
		notifier_cb->space_used_fn =	_ipc_eventfd_space_used_zero;
		notifier_cb->timedwait_fn =	_ipc_eventfd_timedwait;
		notifier_cb->destroy_fn =	_ipc_eventfd_destroy;
		notifier_cb->instance =		one_way;

		return 0;
	}

	one_way->u.shm.read_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (one_way->u.shm.read_eventfd < 0) {
		qb_util_perror(LOG_ERR,
			       "Error creating eventfd");
		return -errno;
	}
	qb_util_log(LOG_TRACE, "creating read_eventfd %d",
		    one_way->u.shm.read_eventfd);
	one_way->u.shm.write_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (one_way->u.shm.write_eventfd < 0) {
		qb_util_perror(LOG_ERR,
			       "Error creating eventfd");
		return -errno;
	}
	qb_util_log(LOG_TRACE, "creating write_eventfd %d",
		    one_way->u.shm.write_eventfd);

	notifier_cb->post_fn =		_ipc_eventfd_post;
	notifier_cb->reclaim_fn =	_ipc_eventfd_reclaim;
	notifier_cb->q_len_fn =		NULL;
	notifier_cb->space_used_fn =	_ipc_eventfd_space_used;
	notifier_cb->timedwait_fn =	_ipc_eventfd_timedwait;
	notifier_cb->destroy_fn =	_ipc_eventfd_destroy;
	notifier_cb->instance =		one_way;

	return 0;
}


int32_t
qb_ipc_efd_send_fds(struct qb_ipcs_connection *c)
{
#ifndef HAVE_EVENTFD
	return 0;
#else
	char buffer[2048];
	struct msghdr msghdr;
	char nothing = '!';
	struct iovec nothing_ptr;
	struct cmsghdr *cmsg;
	int rc;

	if (c->request.type != QB_IPC_SHM) {
		return 0;
	}

	qb_util_log(LOG_TRACE, "sending %d %d %d %d %d %d",
		    c->request.u.shm.read_eventfd,
		    c->request.u.shm.write_eventfd,
		    c->response.u.shm.read_eventfd,
		    c->response.u.shm.write_eventfd,
		    c->event.u.shm.read_eventfd,
		    c->event.u.shm.write_eventfd);

	nothing_ptr.iov_base = &nothing;
	nothing_ptr.iov_len = 1;
	msghdr.msg_name = NULL;
	msghdr.msg_namelen = 0;
	msghdr.msg_iov = &nothing_ptr;
	msghdr.msg_iovlen = 1;
	msghdr.msg_flags = 0;
	msghdr.msg_control = &buffer;
	msghdr.msg_controllen = sizeof(struct cmsghdr) + sizeof(int) * 6;
	cmsg = CMSG_FIRSTHDR(&msghdr);
	cmsg->cmsg_len = msghdr.msg_controllen;
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	((int *)CMSG_DATA(cmsg))[0] = c->request.u.shm.read_eventfd;
	((int *)CMSG_DATA(cmsg))[1] = c->request.u.shm.write_eventfd;
	((int *)CMSG_DATA(cmsg))[2] = c->response.u.shm.read_eventfd;
	((int *)CMSG_DATA(cmsg))[3] = c->response.u.shm.write_eventfd;
	((int *)CMSG_DATA(cmsg))[4] = c->event.u.shm.read_eventfd;
	((int *)CMSG_DATA(cmsg))[5] = c->event.u.shm.write_eventfd;

	rc = sendmsg(c->setup.u.us.sock, &msghdr, 0);
	if (rc < 0) {
		qb_util_perror(LOG_TRACE, "sendmsg");
		return -errno;
	}
	return 0;
#endif /* HAVE_EVENTFD */
}


int32_t
qb_ipc_efd_recv_fds(struct qb_ipcc_connection *c)
{
#ifndef HAVE_EVENTFD
	return 0;
#else
	char buffer[2048];
	struct msghdr msghdr;
	char nothing;
	struct iovec nothing_ptr;
	struct cmsghdr *cmsg;
	int rc;
	int i;

	if (c->request.type != QB_IPC_SHM) {
		return 0;
	}

	nothing_ptr.iov_base = &nothing;
	nothing_ptr.iov_len = 1;
	msghdr.msg_name = NULL;
	msghdr.msg_namelen = 0;
	msghdr.msg_iov = &nothing_ptr;
	msghdr.msg_iovlen = 1;
	msghdr.msg_flags = 0;
	msghdr.msg_control = &buffer;
	msghdr.msg_controllen = sizeof(struct cmsghdr) + sizeof(int) * 6;
	cmsg = CMSG_FIRSTHDR(&msghdr);
	cmsg->cmsg_len = msghdr.msg_controllen;
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	for (i = 0; i < 6; i++) {
		((int *)CMSG_DATA(cmsg))[i] = -1;
	}
	rc = recvmsg(c->setup.u.us.sock, &msghdr, 0);
	if (rc < 0) {
		return -errno;
	}
	rc = (cmsg->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
	if (rc != 6) {
		return -EBADMSG;
	}
	c->request.u.shm.read_eventfd   = ((int *)CMSG_DATA(cmsg))[0];
	c->request.u.shm.write_eventfd  = ((int *)CMSG_DATA(cmsg))[1];
	c->response.u.shm.read_eventfd  = ((int *)CMSG_DATA(cmsg))[2];
	c->response.u.shm.write_eventfd = ((int *)CMSG_DATA(cmsg))[3];
	c->event.u.shm.read_eventfd     = ((int *)CMSG_DATA(cmsg))[4];
	c->event.u.shm.write_eventfd    = ((int *)CMSG_DATA(cmsg))[5];
	return 0;
#endif /* HAVE_EVENTFD */
}

#ifdef HAVE_EVENTFD
static int32_t
_ipcs_connection_liveliness(int32_t fd, int32_t revents, void *data)
{
	struct qb_ipcs_connection *c = (struct qb_ipcs_connection *)data;

	qb_util_log(LOG_DEBUG, "LIVENESS: fd %d event %d conn (%s)",
		    fd, revents, c->description);
	if (revents & POLLNVAL) {
		qb_util_log(LOG_DEBUG, "NVAL conn (%s)", c->description);
		return -EINVAL;
	}
	if (revents & POLLHUP) {
		qb_util_log(LOG_DEBUG, "HUP conn (%s)", c->description);
		qb_ipcs_disconnect(c);
		return -ESHUTDOWN;
	}
	return 0;
}
#endif /* HAVE_EVENTFD */


int32_t
qb_ipc_efd_add_to_mainloop(struct qb_ipcs_connection *c)
{
#ifndef HAVE_EVENTFD
	return 0;
#else
	int res;

	if (c->request.type != QB_IPC_SHM) {
		return 0;
	}
	qb_ipcs_connection_ref(c);
	res = c->service->poll_fns.dispatch_add(c->service->poll_priority,
						c->request.u.shm.write_eventfd,
						POLLIN | POLLPRI | POLLNVAL,
						c,
						qb_ipcs_dispatch_connection_request);
	qb_util_log(LOG_TRACE, "added %d to poll loop",
		    c->request.u.shm.write_eventfd);
	if (res < 0) {
		qb_util_log(LOG_ERR,
			    "Error adding eventfd to mainloop");
		return res;
	}

	qb_ipcs_connection_ref(c);
	res = c->service->poll_fns.dispatch_add(c->service->poll_priority,
						c->setup.u.us.sock,
						POLLIN | POLLPRI | POLLNVAL,
						c,
						_ipcs_connection_liveliness);
	qb_util_log(LOG_TRACE, "added %d to poll loop (liveness)",
		    c->setup.u.us.sock);
	if (res < 0) {
		qb_util_perror(LOG_ERR,
			       "Error adding setupfd to mainloop");
	}
	return res;
#endif /* HAVE_EVENTFD */
}
