/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>
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

#include <config.h>

#include "os_base.h"
#include <sys/shm.h>
#include <sys/mman.h>

#include <qb/qbipcc.h>
#include <qb/qbhdb.h>
#include <qb/qbrb.h>
#include "ipc_int.h"
#include "util_int.h"

#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#else
#include <sys/sem.h>
#endif

/*
 * Define sem_wait timeout (real timeout will be (n-1;n) )
 */
#define IPC_SEMWAIT_TIMEOUT 2

struct ipc_instance {
	int32_t fd;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	int32_t semid;
#endif
	int32_t flow_control_state;
	struct control_buffer *control_buffer;
	qb_ringbuffer_t *request_rb;
	char *response_buffer;
	char *dispatch_buffer;
	size_t control_size;
	size_t request_size;
	size_t response_size;
	size_t dispatch_size;
	uid_t euid;
	pthread_mutex_t mutex;
};

void ipc_hdb_destructor(void *context);

DECLARE_HDB_DATABASE(ipc_hdb, ipc_hdb_destructor);

#if defined(QB_LINUX) || defined(QB_SOLARIS)
#define QB_SUN_LEN(a) sizeof(*(a))
#else
#define QB_SUN_LEN(a) SUN_LEN(a)
#endif

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

static int32_t socket_send(int32_t s, void *msg, size_t len)
{
	int32_t res = 0;
	int32_t result;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *rbuf = msg;
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
	if (result == -1) {
		switch (errno) {
		case EINTR:
			res = EAGAIN;
			goto res_exit;
		case EAGAIN:
			goto retry_send;
			break;
		default:
			res = EBADE;
			goto res_exit;
		}
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}

	return 0;

res_exit:
	return (res);
}

static int32_t socket_recv(int32_t s, void *msg, size_t len)
{
	int32_t res = 0;
	int32_t result;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	char *rbuf = msg;
	int32_t processed = 0;

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

retry_recv:
	iov_recv.iov_base = (void *)&rbuf[processed];
	iov_recv.iov_len = len - processed;

	result = recvmsg(s, &msg_recv, MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1) {
		switch (errno) {
		case EINTR:
			res = EAGAIN;
			goto res_exit;
		case EAGAIN:
			goto retry_recv;
			break;
		default:
			res = EBADE;
			goto res_exit;
		}
	}
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
	/* On many OS poll never return POLLHUP or POLLERR.
	 * EOF is detected when recvmsg return 0.
	 */
	if (result == 0) {
		res = EBADE;
		goto res_exit;
	}
#endif

	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert(processed == len);
res_exit:
	return (res);
}

#if _POSIX_THREAD_PROCESS_SHARED < 1
static int32_t priv_change_send(struct ipc_instance *ipc_instance)
{
	char buf_req;
	mar_req_priv_change req_priv_change;
	int32_t res;

	req_priv_change.euid = geteuid();
	/*
	 * Don't resend request unless euid has changed
	 */
	if (ipc_instance->euid == req_priv_change.euid) {
		return (0);
	}
	req_priv_change.egid = getegid();

	buf_req = MESSAGE_REQ_CHANGE_EUID;
	res = socket_send(ipc_instance->fd, &buf_req, 1);
	if (res == -1) {
		return (-1);
	}

	res = socket_send(ipc_instance->fd, &req_priv_change,
			  sizeof(req_priv_change));
	if (res == -1) {
		return (-1);
	}

	ipc_instance->euid = req_priv_change.euid;
	return (0);
}

#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int32_t val;
	struct semid_ds *buf;
	unsigned short int32_t *array;
	struct seminfo *__buf;
};
#endif
#endif

static int32_t
circular_memory_map(char *path, const char *file, void **buf, size_t bytes)
{
	int32_t fd;
	void *addr_orig;
	void *addr;
	int32_t res;

	sprintf(path, "/dev/shm/%s", file);

	fd = mkstemp(path);
	if (fd == -1) {
		sprintf(path, LOCALSTATEDIR "/run/%s", file);
		fd = mkstemp(path);
		if (fd == -1) {
			return (-1);
		}
	}

	res = ftruncate(fd, bytes);
	if (res == -1) {
		close(fd);
		return (-1);
	}

	addr_orig = mmap(NULL, bytes << 1, PROT_NONE,
			 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr_orig == MAP_FAILED) {
		close(fd);
		return (-1);
	}

	addr = mmap(addr_orig, bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		close(fd);
		return (-1);
	}
#ifdef QB_BSD
	madvise(addr_orig, bytes, MADV_NOSYNC);
#endif

	addr = mmap(((char *)addr_orig) + bytes,
		    bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		close(fd);
		return (-1);
	}
#ifdef QB_BSD
	madvise(((char *)addr_orig) + bytes, bytes, MADV_NOSYNC);
#endif

	res = close(fd);
	if (res) {
		return (-1);
	}
	*buf = addr_orig;
	return (0);
}

static void memory_unmap(void *addr, size_t bytes)
{
	int32_t res;

	res = munmap(addr, bytes);
}

void ipc_hdb_destructor(void *context)
{
	struct ipc_instance *ipc_instance = (struct ipc_instance *)context;

	/*
	 * << 1 (or multiplied by 2) because this is a wrapped memory buffer
	 */
	memory_unmap(ipc_instance->control_buffer, ipc_instance->control_size);
	qb_rb_close(ipc_instance->request_rb);
	memory_unmap(ipc_instance->response_buffer,
		     ipc_instance->response_size);
	memory_unmap(ipc_instance->dispatch_buffer,
		     (ipc_instance->dispatch_size) << 1);
}

static int32_t memory_map(char *path, const char *file, void **buf,
			  size_t bytes)
{
	int32_t fd;
	void *addr_orig;
	void *addr;
	int32_t res;

	sprintf(path, "/dev/shm/%s", file);

	fd = mkstemp(path);
	if (fd == -1) {
		sprintf(path, LOCALSTATEDIR "/run/%s", file);
		fd = mkstemp(path);
		if (fd == -1) {
			return (-1);
		}
	}

	res = ftruncate(fd, bytes);
	if (res == -1) {
		close(fd);
		return (-1);
	}

	addr_orig = mmap(NULL, bytes, PROT_NONE,
			 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (addr_orig == MAP_FAILED) {
		close(fd);
		return (-1);
	}

	addr = mmap(addr_orig, bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		close(fd);
		return (-1);
	}
#ifdef QB_BSD
	madvise(addr_orig, bytes, MADV_NOSYNC);
#endif

	res = close(fd);
	if (res) {
		return (-1);
	}
	*buf = addr_orig;
	return (0);
}

static int32_t
msg_send(struct ipc_instance *ipc_instance,
	 const struct iovec *iov, uint32_t iov_len)
{
	int32_t i;
	size_t size = 0;
	char *chunk_pt;

	for (i = 0; i < iov_len; i++) {
		if ((size + iov[i].iov_len) > ipc_instance->request_size) {
			errno = EINVAL;
			return -1;
		}
		size += iov[i].iov_len;
	}
	chunk_pt = qb_rb_chunk_alloc(ipc_instance->request_rb, size);
	if (chunk_pt == NULL) {
		errno = ENOMEM;
		return -1;
	}
	for (i = 0; i < iov_len; i++) {
		memcpy(chunk_pt, iov[i].iov_base, iov[i].iov_len);
		chunk_pt += iov[i].iov_len;
	}
	return qb_rb_chunk_commit(ipc_instance->request_rb, size);
}

static int32_t ipc_sem_wait(struct ipc_instance *ipc_instance, int32_t sem_num)
{
#if _POSIX_THREAD_PROCESS_SHARED < 1
	struct sembuf sop;
#else
	struct timespec timeout;
	struct pollfd pfd;
	sem_t *sem = NULL;
#endif
	int32_t res;

#if _POSIX_THREAD_PROCESS_SHARED > 0
	switch (sem_num) {
	case 1:
		sem = &ipc_instance->control_buffer->sem1;
		break;
	case 2:
		sem = &ipc_instance->control_buffer->sem2;
		break;
	}

retry_semwait:
	timeout.tv_sec = time(NULL) + IPC_SEMWAIT_TIMEOUT;
	timeout.tv_nsec = 0;

	res = sem_timedwait(sem, &timeout);
	if (res == -1 && errno == ETIMEDOUT) {
		pfd.fd = ipc_instance->fd;
		pfd.events = 0;

		res = poll(&pfd, 1, 0);

		if (res == -1 && errno == EINTR) {
			return EAGAIN;
		} else if (res == -1) {
			return EBADE;
		}

		if (res == 1) {
			if (pfd.revents == POLLERR || pfd.revents == POLLHUP
			    || pfd.revents == POLLNVAL) {
				return EBADE;
			}
		}

		goto retry_semwait;
	} else if (res == -1 && errno == EINTR) {
		return EAGAIN;
	} else if (res == -1) {
		return EBADE;
	}
#else
	/*
	 * Wait for semaphore indicating a new message from server
	 * to client in queue
	 */
	sop.sem_num = sem_num;
	sop.sem_op = -1;
	sop.sem_flg = 0;

retry_semop:
	res = semop(ipc_instance->semid, &sop, 1);
	if (res == -1 && errno == EINTR) {
		return (EAGAIN);
	} else if (res == -1 && errno == EACCES) {
		priv_change_send(ipc_instance);
		goto retry_semop;
	} else if (res == -1) {
		return EBAD;
	}
#endif
	return 0;
}

static int32_t
reply_receive(struct ipc_instance *ipc_instance, void *res_msg, size_t res_len)
{
	qb_ipc_response_header_t *response_header;
	int32_t err = 0;

	if ((err = ipc_sem_wait(ipc_instance, 1)) != 0) {
		return (err);
	}

	response_header =
	    (qb_ipc_response_header_t *) ipc_instance->response_buffer;
	if (response_header->error == EAGAIN) {
		return (EAGAIN);
	}

	memcpy(res_msg, ipc_instance->response_buffer, res_len);
	return 0;
}

static int32_t
reply_receive_in_buf(struct ipc_instance *ipc_instance, void **res_msg)
{
	int32_t err;

	if ((err = ipc_sem_wait(ipc_instance, 1)) != 0) {
		return (err);
	}

	*res_msg = (char *)ipc_instance->response_buffer;
	return 0;
}

/*
 * External API
 */
int32_t
qb_ipcc_service_connect(const char *socket_name,
			uint32_t service,
			size_t request_size,
			size_t response_size,
			size_t dispatch_size, qb_hdb_handle_t * handle)
{
	int32_t request_fd;
	struct sockaddr_un address;
	int32_t res;
	struct ipc_instance *ipc_instance;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	key_t semkey = 0;
	union semun semun;
#endif
	int32_t sys_res;
	mar_req_setup_t req_setup;
	mar_res_setup_t res_setup;
	char control_map_path[128];
	char request_map_path[128];
	char response_map_path[128];
	char dispatch_map_path[128];

	res = qb_hdb_handle_create(&ipc_hdb,
				   sizeof(struct ipc_instance), handle);
	if (res != 0) {
		return (res);
	}

	res = qb_hdb_handle_get(&ipc_hdb, *handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	res_setup.error = EBADE;

#if defined(QB_SOLARIS)
	request_fd = socket(PF_UNIX, SOCK_STREAM, 0);
#else
	request_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (request_fd == -1) {
		return (EBADE);
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
	sys_res = connect(request_fd, (struct sockaddr *)&address,
			  QB_SUN_LEN(&address));
	if (sys_res == -1) {
		res = EAGAIN;
		goto error_connect;
	}

	res = memory_map(control_map_path,
			 "control_buffer-XXXXXX",
			 (void *)&ipc_instance->control_buffer, 8192);
	if (res == -1) {
		res = EBADE;
		goto error_connect;
	}

	/* RB request */
	ipc_instance->request_rb = qb_rb_open("request_ringbuffer-XXXXXX",
					      request_size,
					      QB_RB_FLAG_CREATE |
					      QB_RB_FLAG_SHARED_PROCESS);
	if (ipc_instance->request_rb == NULL) {
		res = EBADE;
		goto error_request_buffer;
	}
	strcpy(request_map_path, qb_rb_name_get(ipc_instance->request_rb));

	res = memory_map(response_map_path,
			 "response_buffer-XXXXXX",
			 (void *)&ipc_instance->response_buffer, response_size);
	if (res == -1) {
		res = EBADE;
		goto error_response_buffer;
	}

	res = circular_memory_map(dispatch_map_path,
				  "dispatch_buffer-XXXXXX",
				  (void *)&ipc_instance->dispatch_buffer,
				  dispatch_size);
	if (res == -1) {
		res = EBADE;
		goto error_dispatch_buffer;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_init(&ipc_instance->control_buffer->sem1, 1, 0);
	sem_init(&ipc_instance->control_buffer->sem2, 1, 0);
#else
	/*
	 * Allocate a semaphore segment
	 */
	while (1) {
		semkey = random();
		ipc_instance->euid = geteuid();
		if ((ipc_instance->semid
		     = semget(semkey, 3, IPC_CREAT | IPC_EXCL | 0600)) != -1) {
			break;
		}
		/*
		 * EACCESS can be returned as non root user when opening a different
		 * users semaphore.
		 *
		 * EEXIST can happen when we are a root or nonroot user opening
		 * an existing shared memory segment for which we have access
		 */
		if (errno != EEXIST && errno != EACCES) {
			res = EBAD;
			goto error_exit;
		}
	}

	semun.val = 0;
	res = semctl(ipc_instance->semid, 0, SETVAL, semun);
	if (res != 0) {
		res = EBAD;
		goto error_exit;
	}

	res = semctl(ipc_instance->semid, 1, SETVAL, semun);
	if (res != 0) {
		res = EBAD;
		goto error_exit;
	}
#endif

	/*
	 * Initialize IPC setup message
	 */
	req_setup.service = service;
	strcpy(req_setup.control_file, control_map_path);
	strcpy(req_setup.request_file, request_map_path);
	strcpy(req_setup.response_file, response_map_path);
	strcpy(req_setup.dispatch_file, dispatch_map_path);
	req_setup.control_size = 8192;
	req_setup.request_size = request_size;
	req_setup.response_size = response_size;
	req_setup.dispatch_size = dispatch_size;

#if _POSIX_THREAD_PROCESS_SHARED < 1
	req_setup.semkey = semkey;
#endif

	res = socket_send(request_fd, &req_setup, sizeof(mar_req_setup_t));
	if (res != 0) {
		goto error_exit;
	}
	res = socket_recv(request_fd, &res_setup, sizeof(mar_res_setup_t));
	if (res != 0) {
		goto error_exit;
	}

	ipc_instance->fd = request_fd;
	ipc_instance->flow_control_state = 0;

	if (res_setup.error == EAGAIN) {
		res = res_setup.error;
		goto error_exit;
	}

	ipc_instance->control_size = 8192;
	ipc_instance->request_size = request_size;
	ipc_instance->response_size = response_size;
	ipc_instance->dispatch_size = dispatch_size;

	pthread_mutex_init(&ipc_instance->mutex, NULL);

	qb_hdb_handle_put(&ipc_hdb, *handle);

	return (res_setup.error);

error_exit:
#if _POSIX_THREAD_PROCESS_SHARED < 1
	if (ipc_instance->semid > 0)
		semctl(ipc_instance->semid, 0, IPC_RMID);
#endif
	memory_unmap(ipc_instance->dispatch_buffer, dispatch_size);
error_dispatch_buffer:
	memory_unmap(ipc_instance->response_buffer, response_size);
error_response_buffer:
	qb_rb_close(ipc_instance->request_rb);
error_request_buffer:
	memory_unmap(ipc_instance->control_buffer, 8192);
error_connect:
	close(request_fd);

	qb_hdb_handle_destroy(&ipc_hdb, *handle);
	qb_hdb_handle_put(&ipc_hdb, *handle);

	return (res);
}

int32_t qb_ipcc_service_disconnect(qb_hdb_handle_t handle)
{
	int32_t res;
	struct ipc_instance *ipc_instance;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	shutdown(ipc_instance->fd, SHUT_RDWR);
	close(ipc_instance->fd);
	qb_hdb_handle_destroy(&ipc_hdb, handle);
	qb_hdb_handle_put(&ipc_hdb, handle);
	return (0);
}

int32_t
qb_ipcc_dispatch_flow_control_get(qb_hdb_handle_t handle,
				  uint32_t * flow_control_state)
{
	struct ipc_instance *ipc_instance;
	int32_t res;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	*flow_control_state = ipc_instance->flow_control_state;

	qb_hdb_handle_put(&ipc_hdb, handle);
	return (res);
}

int32_t qb_ipcc_fd_get(qb_hdb_handle_t handle, int32_t * fd)
{
	struct ipc_instance *ipc_instance;
	int32_t res;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	*fd = ipc_instance->fd;

	qb_hdb_handle_put(&ipc_hdb, handle);
	return (res);
}

int32_t qb_ipcc_dispatch_get(qb_hdb_handle_t handle, void **data,
			     int32_t timeout)
{
	struct pollfd ufds;
	int32_t poll_events;
	char buf;
	struct ipc_instance *ipc_instance;
	int32_t res;
	char buf_two = 1;
	char *data_addr;
	int32_t error = 0;

	error = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (error != 0) {
		return (error);
	}

	*data = NULL;

	ufds.fd = ipc_instance->fd;
	ufds.events = POLLIN;
	ufds.revents = 0;

	poll_events = poll(&ufds, 1, timeout);
	if (poll_events == -1 && errno == EINTR) {
		error = EAGAIN;
		goto error_put;
	} else if (poll_events == -1) {
		error = EBADE;
		goto error_put;
	} else if (poll_events == 0) {
		error = EAGAIN;
		goto error_put;
	}
	if (poll_events == 1 && (ufds.revents & (POLLERR | POLLHUP))) {
		error = EBADE;
		goto error_put;
	}

	res = recv(ipc_instance->fd, &buf, 1, 0);
	if (res == -1 && errno == EINTR) {
		error = EAGAIN;
		goto error_put;
	} else if (res == -1) {
		error = EBADE;
		goto error_put;
	} else if (res == 0) {
		/* Means that the peer closed cleanly the socket. However, it should
		 * happen only on BSD and Darwing systems since poll() returns a
		 * POLLHUP event on other systems.
		 */
		error = EBADE;
		goto error_put;
	}
	ipc_instance->flow_control_state = 0;
	if (buf == MESSAGE_RES_OUTQ_NOT_EMPTY
	    || buf == MESSAGE_RES_ENABLE_FLOWCONTROL) {
		ipc_instance->flow_control_state = 1;
	}
	/*
	 * Notify executive to flush any pending dispatch messages
	 */
	if (ipc_instance->flow_control_state) {
		buf_two = MESSAGE_REQ_OUTQ_FLUSH;
		res = socket_send(ipc_instance->fd, &buf_two, 1);
		assert(res == 0);	/* TODO */
	}
	/*
	 * This is just a notification of flow control starting at the addition
	 * of a new pending message, not a message to dispatch
	 */
	if (buf == MESSAGE_RES_ENABLE_FLOWCONTROL) {
		error = EAGAIN;
		goto error_put;
	}
	if (buf == MESSAGE_RES_OUTQ_FLUSH_NR) {
		error = EAGAIN;
		goto error_put;
	}

	data_addr = ipc_instance->dispatch_buffer;

	data_addr = &data_addr[ipc_instance->control_buffer->read];

	*data = (void *)data_addr;

	return (0);
error_put:
	qb_hdb_handle_put(&ipc_hdb, handle);
	return (error);
}

int32_t qb_ipcc_dispatch_put(qb_hdb_handle_t handle)
{
	qb_ipc_response_header_t *header;
	struct ipc_instance *ipc_instance;
	int32_t res;
	char *addr;
	uint32_t read_idx;

	res =
	    qb_hdb_handle_get_always(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	if ((res = ipc_sem_wait(ipc_instance, 2)) != 0) {
		goto error_exit;
	}

	addr = ipc_instance->dispatch_buffer;

	read_idx = ipc_instance->control_buffer->read;
	header = (qb_ipc_response_header_t *) & addr[read_idx];
	ipc_instance->control_buffer->read =
	    (read_idx + header->size) % ipc_instance->dispatch_size;
	/*
	 * Put from dispatch get and also from this call's get
	 */
	res = 0;

error_exit:
	qb_hdb_handle_put(&ipc_hdb, handle);
	qb_hdb_handle_put(&ipc_hdb, handle);

	return (res);
}

int32_t
qb_ipcc_msg_send(qb_hdb_handle_t handle,
		 const struct iovec * iov, uint32_t iov_len)
{
	int32_t res;
	struct ipc_instance *ipc_instance;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	pthread_mutex_lock(&ipc_instance->mutex);

	res = msg_send(ipc_instance, iov, iov_len);

	qb_hdb_handle_put(&ipc_hdb, handle);
	pthread_mutex_unlock(&ipc_instance->mutex);

	return (res);
}

int32_t
qb_ipcc_msg_send_reply_receive(qb_hdb_handle_t handle,
			       const struct iovec * iov,
			       uint32_t iov_len, void *res_msg, size_t res_len)
{
	int32_t res;
	struct ipc_instance *ipc_instance;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	pthread_mutex_lock(&ipc_instance->mutex);

	res = msg_send(ipc_instance, iov, iov_len);
	if (res != 0) {
		goto error_exit;
	}

	res = reply_receive(ipc_instance, res_msg, res_len);

error_exit:
	qb_hdb_handle_put(&ipc_hdb, handle);
	pthread_mutex_unlock(&ipc_instance->mutex);

	return (res);
}

int32_t
qb_ipcc_msg_send_reply_receive_in_buf_get(qb_hdb_handle_t handle,
					  const struct iovec * iov,
					  uint32_t iov_len, void **res_msg)
{
	int32_t res;
	struct ipc_instance *ipc_instance;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	pthread_mutex_lock(&ipc_instance->mutex);

	res = msg_send(ipc_instance, iov, iov_len);
	if (res != 0) {
		goto error_exit;
	}

	res = reply_receive_in_buf(ipc_instance, res_msg);

error_exit:
	pthread_mutex_unlock(&ipc_instance->mutex);

	return (res);
}

int32_t qb_ipcc_msg_send_reply_receive_in_buf_put(qb_hdb_handle_t handle)
{
	int32_t res;
	struct ipc_instance *ipc_instance;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}
	qb_hdb_handle_put(&ipc_hdb, handle);
	qb_hdb_handle_put(&ipc_hdb, handle);

	return (res);
}

int32_t
qb_ipcc_zcb_alloc(qb_hdb_handle_t handle,
		  void **buffer, size_t size, size_t header_size)
{
	struct ipc_instance *ipc_instance;
	void *buf = NULL;
	char path[128];
	int32_t res;
	mar_req_qb_ipcc_zc_alloc_t req_qb_ipcc_zc_alloc;
	qb_ipc_response_header_t res_qb_ipcs_zc_alloc;
	size_t map_size;
	struct iovec iovec;
	struct qb_ipcs_zc_header *hdr;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}
	map_size = size + header_size + sizeof(struct qb_ipcs_zc_header);
	res = memory_map(path, "qb_zerocopy-XXXXXX", &buf, map_size);
	assert(res != -1);

	req_qb_ipcc_zc_alloc.header.size = sizeof(mar_req_qb_ipcc_zc_alloc_t);
	req_qb_ipcc_zc_alloc.header.id = ZC_ALLOC_HEADER;
	req_qb_ipcc_zc_alloc.map_size = map_size;
	strcpy(req_qb_ipcc_zc_alloc.path_to_file, path);

	iovec.iov_base = (void *)&req_qb_ipcc_zc_alloc;
	iovec.iov_len = sizeof(mar_req_qb_ipcc_zc_alloc_t);

	res = qb_ipcc_msg_send_reply_receive(handle,
					     &iovec,
					     1,
					     &res_qb_ipcs_zc_alloc,
					     sizeof(qb_ipc_response_header_t));

	hdr = (struct qb_ipcs_zc_header *)buf;
	hdr->map_size = map_size;
	*buffer = ((char *)buf) + sizeof(struct qb_ipcs_zc_header);

	qb_hdb_handle_put(&ipc_hdb, handle);
	return (res);
}

int32_t qb_ipcc_zcb_free(qb_hdb_handle_t handle, void *buffer)
{
	struct ipc_instance *ipc_instance;
	mar_req_qb_ipcc_zc_free_t req_qb_ipcc_zc_free;
	qb_ipc_response_header_t res_qb_ipcs_zc_free;
	struct iovec iovec;
	int32_t res;
	struct qb_ipcs_zc_header *header =
	    (struct qb_ipcs_zc_header *)((char *)buffer -
					 sizeof(struct qb_ipcs_zc_header));

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}

	req_qb_ipcc_zc_free.header.size = sizeof(mar_req_qb_ipcc_zc_free_t);
	req_qb_ipcc_zc_free.header.id = ZC_FREE_HEADER;
	req_qb_ipcc_zc_free.map_size = header->map_size;
	req_qb_ipcc_zc_free.server_address = header->server_address;

	iovec.iov_base = (void *)&req_qb_ipcc_zc_free;
	iovec.iov_len = sizeof(mar_req_qb_ipcc_zc_free_t);

	res = qb_ipcc_msg_send_reply_receive(handle,
					     &iovec,
					     1,
					     &res_qb_ipcs_zc_free,
					     sizeof(qb_ipc_response_header_t));

	munmap((void *)header, header->map_size);

	qb_hdb_handle_put(&ipc_hdb, handle);

	return (res);
}

int32_t
qb_ipcc_zcb_msg_send_reply_receive(qb_hdb_handle_t handle,
				   void *msg, void *res_msg, size_t res_len)
{
	struct ipc_instance *ipc_instance;
	mar_req_qb_ipcc_zc_execute_t req_qb_ipcc_zc_execute;
	struct qb_ipcs_zc_header *hdr;
	struct iovec iovec;
	int32_t res;

	res = qb_hdb_handle_get(&ipc_hdb, handle, (void **)&ipc_instance);
	if (res != 0) {
		return (res);
	}
	hdr =
	    (struct qb_ipcs_zc_header *)(((char *)msg) -
					 sizeof(struct qb_ipcs_zc_header));

	req_qb_ipcc_zc_execute.header.size =
	    sizeof(mar_req_qb_ipcc_zc_execute_t);
	req_qb_ipcc_zc_execute.header.id = ZC_EXECUTE_HEADER;
	req_qb_ipcc_zc_execute.server_address = hdr->server_address;

	iovec.iov_base = (void *)&req_qb_ipcc_zc_execute;
	iovec.iov_len = sizeof(mar_req_qb_ipcc_zc_execute_t);

	res = qb_ipcc_msg_send_reply_receive(handle,
					     &iovec, 1, res_msg, res_len);

	qb_hdb_handle_put(&ipc_hdb, handle);
	return (res);
}
