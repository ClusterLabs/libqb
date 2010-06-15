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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "os_base.h"
#include <sys/mman.h>
#include <sys/poll.h>
#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
#endif
#include <sys/shm.h>

#include <qb/qblist.h>
#include <qb/qbhdb.h>
#include <qb/qbipcs.h>
#include <qb/qbrb.h>
#include "ipc_int.h"
#include "util_int.h"

#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#else
#include <sys/sem.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define SERVER_BACKLOG 5

#define MSG_SEND_LOCKED		0
#define MSG_SEND_UNLOCKED	1

static struct qb_ipcs_init_state *api = NULL;

QB_DECLARE_LIST_INIT(conn_info_qb_list_head);

QB_DECLARE_LIST_INIT(conn_info_exit_qb_list_head);

struct outq_item {
	void *msg;
	size_t mlen;
	struct qb_list_head list;
};

struct zcb_mapped {
	struct qb_list_head list;
	void *addr;
	size_t size;
};

#if _POSIX_THREAD_PROCESS_SHARED < 1
#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int32_t val;
	struct semid_ds *buf;
	unsigned short int32_t *array;
	struct seminfo *__buf;
};
#endif
#endif

enum conn_state {
	CONN_STATE_THREAD_INACTIVE = 0,
	CONN_STATE_THREAD_ACTIVE = 1,
	CONN_STATE_THREAD_REQUEST_EXIT = 2,
	CONN_STATE_THREAD_DESTROYED = 3,
	CONN_STATE_LIB_EXIT_CALLED = 4,
	CONN_STATE_DISCONNECT_INACTIVE = 5
};

struct conn_info {
	int32_t fd;
	pthread_t thread;
	pid_t client_pid;
	pthread_attr_t thread_attr;
	uint32_t service;
	enum conn_state state;
	int32_t notify_flow_control_enabled;
	int32_t flow_control_state;
	int32_t refcount;
	qb_hdb_handle_t stats_handle;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	key_t semkey;
	int32_t semid;
#endif
	uint32_t pending_semops;
	pthread_mutex_t mutex;
	struct control_buffer *control_buffer;
	qb_ringbuffer_t *request_rb;
	char *response_buffer;
	char *dispatch_buffer;
	size_t control_size;
	size_t request_size;
	size_t response_size;
	size_t dispatch_size;
	struct qb_list_head outq_head;
	void *private_data;
	struct qb_list_head list;
	char setup_msg[sizeof(mar_req_setup_t)];
	uint32_t setup_bytes_read;
	struct qb_list_head zcb_mapped_qb_list_head;
	char *sending_allowed_private_data[64];
};

static int32_t shared_mem_dispatch_bytes_left(const struct conn_info
					      *conn_info);

static void outq_flush(struct conn_info *conn_info);

static int32_t priv_change(struct conn_info *conn_info);

static void ipc_disconnect(struct conn_info *conn_info);

static void msg_send(void *conn, const struct iovec *iov, uint32_t iov_len,
		     int32_t locked);

static qb_hdb_handle_t dummy_stats_create_connection(const char *name,
						     pid_t pid, int32_t fd)
{
	return (0ULL);
}

static void dummy_stats_destroy_connection(qb_hdb_handle_t handle)
{
}

static void dummy_stats_update_value(qb_hdb_handle_t handle,
				     const char *name,
				     const void *value, size_t value_size)
{
}

static void dummy_stats_increment_value(qb_hdb_handle_t handle,
					const char *name)
{
}

/*
 * Just rite some junk to the ringbuffer to kick it out of a sem_wait
 */
static void sem_post_exit_thread(struct conn_info *conn_info)
{
	qb_rb_chunk_write(conn_info->request_rb, conn_info, 4);
}

static int32_t memory_map(const char *path, size_t bytes, void **buf)
{
	int32_t fd;
	void *addr_orig;
	void *addr;
	int32_t res;

	fd = open(path, O_RDWR, 0600);

	unlink(path);

	if (fd == -1) {
		return (-1);
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
	madvise(addr, bytes, MADV_NOSYNC);
#endif

	res = close(fd);
	if (res) {
		return (-1);
	}
	*buf = addr_orig;
	return (0);
}

static int32_t circular_memory_map(const char *path, size_t bytes, void **buf)
{
	int32_t fd;
	void *addr_orig;
	void *addr;
	int32_t res;

	fd = open(path, O_RDWR, 0600);

	unlink(path);

	if (fd == -1) {
		return (-1);
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

static inline int32_t circular_memory_unmap(void *buf, size_t bytes)
{
	int32_t res;

	res = munmap(buf, bytes << 1);

	return (res);
}

static inline int32_t zcb_free(struct zcb_mapped *zcb_mapped)
{
	uint32_t res;

	res = munmap(zcb_mapped->addr, zcb_mapped->size);
	qb_list_del(&zcb_mapped->list);
	free(zcb_mapped);
	return (res);
}

static inline int32_t zcb_by_addr_free(struct conn_info *conn_info, void *addr)
{
	struct qb_list_head *list;
	struct zcb_mapped *zcb_mapped;
	uint32_t res = 0;

	for (list = conn_info->zcb_mapped_qb_list_head.next;
	     list != &conn_info->zcb_mapped_qb_list_head; list = list->next) {

		zcb_mapped = qb_list_entry(list, struct zcb_mapped, list);

		if (zcb_mapped->addr == addr) {
			res = zcb_free(zcb_mapped);
			break;
		}

	}
	return (res);
}

static inline int32_t zcb_all_free(struct conn_info *conn_info)
{
	struct qb_list_head *list;
	struct zcb_mapped *zcb_mapped;

	for (list = conn_info->zcb_mapped_qb_list_head.next;
	     list != &conn_info->zcb_mapped_qb_list_head;) {

		zcb_mapped = qb_list_entry(list, struct zcb_mapped, list);

		list = list->next;

		zcb_free(zcb_mapped);
	}
	return (0);
}

static inline int32_t zcb_alloc(struct conn_info *conn_info,
				const char *path_to_file,
				size_t size, void **addr)
{
	struct zcb_mapped *zcb_mapped;
	uint32_t res;

	zcb_mapped = malloc(sizeof(struct zcb_mapped));
	if (zcb_mapped == NULL) {
		return (-1);
	}

	res = memory_map(path_to_file, size, addr);
	if (res == -1) {
		free(zcb_mapped);
		return (-1);
	}

	qb_list_init(&zcb_mapped->list);
	zcb_mapped->addr = *addr;
	zcb_mapped->size = size;
	qb_list_add_tail(&zcb_mapped->list,
			 &conn_info->zcb_mapped_qb_list_head);
	return (0);
}

static int32_t ipc_thread_active(void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int32_t retval = 0;

	pthread_mutex_lock(&conn_info->mutex);
	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		retval = 1;
	}
	pthread_mutex_unlock(&conn_info->mutex);
	return (retval);
}

static int32_t ipc_thread_exiting(void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	int32_t retval = 1;

	pthread_mutex_lock(&conn_info->mutex);
	if (conn_info->state == CONN_STATE_THREAD_INACTIVE) {
		retval = 0;
	} else if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		retval = 0;
	}
	pthread_mutex_unlock(&conn_info->mutex);
	return (retval);
}

/*
 * returns 0 if should be called again, -1 if finished
 */
static inline int32_t conn_info_destroy(struct conn_info *conn_info)
{
	uint32_t res;
	void *retval;

	qb_list_del(&conn_info->list);
	qb_list_init(&conn_info->list);
	qb_list_add(&conn_info->list, &conn_info_exit_qb_list_head);

	if (conn_info->state == CONN_STATE_THREAD_REQUEST_EXIT) {
		res = pthread_join(conn_info->thread, &retval);
		conn_info->state = CONN_STATE_THREAD_DESTROYED;
		return (0);
	}

	if (conn_info->state == CONN_STATE_THREAD_INACTIVE ||
	    conn_info->state == CONN_STATE_DISCONNECT_INACTIVE) {
		qb_list_del(&conn_info->list);
		close(conn_info->fd);
		api->free(conn_info);
		return (-1);
	}

	if (conn_info->state == CONN_STATE_THREAD_ACTIVE) {
		sem_post_exit_thread(conn_info);
		return (0);
	}

	api->serialize_lock();
	/*
	 * Retry library exit function if busy
	 */
	if (conn_info->state == CONN_STATE_THREAD_DESTROYED) {
		api->stats_destroy_connection(conn_info->stats_handle);
		res = api->exit_fn_get(conn_info->service) (conn_info);
		if (res == -1) {
			api->serialize_unlock();
			return (0);
		} else {
			conn_info->state = CONN_STATE_LIB_EXIT_CALLED;
		}
	}

	pthread_mutex_lock(&conn_info->mutex);
	if (conn_info->refcount > 0) {
		pthread_mutex_unlock(&conn_info->mutex);
		api->serialize_unlock();
		return (0);
	}
	qb_list_del(&conn_info->list);
	pthread_mutex_unlock(&conn_info->mutex);

#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_destroy(&conn_info->control_buffer->sem1);
	sem_destroy(&conn_info->control_buffer->sem2);
#else
	semctl(conn_info->semid, 0, IPC_RMID);
#endif
	/*
	 * Destroy shared memory segment and semaphore
	 */
	res =
	    munmap((void *)conn_info->control_buffer, conn_info->control_size);
	qb_rb_close(conn_info->request_rb);
	res =
	    munmap((void *)conn_info->response_buffer,
		   conn_info->response_size);

	/*
	 * Free allocated data needed to retry exiting library IPC connection
	 */
	if (conn_info->private_data) {
		api->free(conn_info->private_data);
	}
	close(conn_info->fd);
	res =
	    circular_memory_unmap(conn_info->dispatch_buffer,
				  conn_info->dispatch_size);
	zcb_all_free(conn_info);
	api->free(conn_info);
	api->serialize_unlock();
	return (-1);
}

union u {
	uint64_t server_addr;
	void *server_ptr;
};

static uint64_t void2serveraddr(void *server_ptr)
{
	union u u;

	u.server_ptr = server_ptr;
	return (u.server_addr);
}

static void *serveraddr2void(uint64_t server_addr)
{
	union u u;

	u.server_addr = server_addr;
	return (u.server_ptr);
};

static void zerocopy_operations_process(struct conn_info *conn_info,
					qb_ipc_request_header_t **
					header_out, uint32_t * new_message)
{
	qb_ipc_request_header_t *header;

	header = *header_out;
	if (header->id == ZC_ALLOC_HEADER) {
		mar_req_qb_ipcc_zc_alloc_t *hdr =
		    (mar_req_qb_ipcc_zc_alloc_t *) header;
		qb_ipc_response_header_t res_header;
		void *addr = NULL;
		struct qb_ipcs_zc_header *zc_header;
		uint32_t res;

		res = zcb_alloc(conn_info, hdr->path_to_file, hdr->map_size,
				&addr);

		zc_header = (struct qb_ipcs_zc_header *)addr;
		zc_header->server_address = void2serveraddr(addr);

		res_header.size = sizeof(qb_ipc_response_header_t);
		res_header.id = 0;
		qb_ipcs_response_send(conn_info, &res_header, res_header.size);
		*new_message = 0;
		return;
	} else if (header->id == ZC_FREE_HEADER) {
		mar_req_qb_ipcc_zc_free_t *hdr =
		    (mar_req_qb_ipcc_zc_free_t *) header;
		qb_ipc_response_header_t res_header;
		void *addr = NULL;

		addr = serveraddr2void(hdr->server_address);

		zcb_by_addr_free(conn_info, addr);

		res_header.size = sizeof(qb_ipc_response_header_t);
		res_header.id = 0;
		qb_ipcs_response_send(conn_info, &res_header, res_header.size);

		*new_message = 0;
		return;
	} else if (header->id == ZC_EXECUTE_HEADER) {
		mar_req_qb_ipcc_zc_execute_t *hdr =
		    (mar_req_qb_ipcc_zc_execute_t *) header;

		header =
		    (qb_ipc_request_header_t
		     *) (((char *)serveraddr2void(hdr->server_address) +
			  sizeof(struct qb_ipcs_zc_header)));
	}
	*header_out = header;
	*new_message = 1;
}

static void *pthread_ipc_consumer(void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	qb_ipc_request_header_t *header;
	qb_ipc_response_header_t response_header;
	int32_t send_ok;
	uint32_t new_message;
	ssize_t size;
	void *pri_data = NULL;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX)
	if (api->sched_policy != 0) {
		res = pthread_setschedparam(conn_info->thread,
					    api->sched_policy,
					    api->sched_param);
	}
#endif

	for (;;) {
		size = qb_rb_chunk_peek(conn_info->request_rb,
					(void **)&header, 2000);

		if (ipc_thread_active(conn_info) == 0) {
			qb_ipcs_refcount_dec(conn_info);
			pthread_exit(0);
		}
		if (size <= 0) {
			api->stats_increment_value(conn_info->stats_handle,
						   "sem_retry_count");
			continue;
		}

		zerocopy_operations_process(conn_info, &header, &new_message);
		/*
		 * There is no new message to process, continue for loop
		 */
		if (new_message == 0) {
			qb_rb_chunk_reclaim(conn_info->request_rb);
			continue;
		}

		qb_ipcs_refcount_inc(conn);

		pri_data = conn_info->sending_allowed_private_data;
		send_ok = api->sending_allowed(conn_info->service,
					       header->id, header, pri_data);

		/*
		 * This happens when the message contains some kind of invalid
		 * parameter, such as an invalid size
		 */
		if (send_ok == -1) {
			response_header.size = sizeof(qb_ipc_response_header_t);
			response_header.id = 0;
			response_header.error = EINVAL;
			qb_ipcs_response_send(conn_info,
					      &response_header,
					      sizeof(qb_ipc_response_header_t));
			qb_rb_chunk_reclaim(conn_info->request_rb);
		} else if (send_ok) {
			api->serialize_lock();
			api->stats_increment_value(conn_info->stats_handle,
						   "requests");
			api->handler_fn_get(conn_info->service,
					    header->id) (conn_info, header);
			qb_rb_chunk_reclaim(conn_info->request_rb);
			api->serialize_unlock();
		} else {
			/*
			 * Overload, don't call qb_rb_chunk_reclaim()
			 */
			api->stats_increment_value(conn_info->stats_handle,
						   "sem_retry_count");
			response_header.size = sizeof(qb_ipc_response_header_t);
			response_header.id = 0;
			response_header.error = EAGAIN;
			qb_ipcs_response_send(conn_info,
					      &response_header,
					      sizeof(qb_ipc_response_header_t));
			qb_rb_chunk_reclaim(conn_info->request_rb);
		}

		api->sending_allowed_release
		    (conn_info->sending_allowed_private_data);
		qb_ipcs_refcount_dec(conn);
	}
	pthread_exit(0);
}

static int32_t req_setup_send(struct conn_info *conn_info, int32_t error)
{
	mar_res_setup_t res_setup;
	uint32_t res;

	memset(&res_setup, 0, sizeof(res_setup));
	res_setup.error = error;

retry_send:
	res =
	    send(conn_info->fd, &res_setup, sizeof(mar_res_setup_t),
		 MSG_WAITALL);
	if (res == -1 && errno == EINTR) {
		api->stats_increment_value(conn_info->stats_handle,
					   "send_retry_count");
		goto retry_send;
	} else if (res == -1 && errno == EAGAIN) {
		api->stats_increment_value(conn_info->stats_handle,
					   "send_retry_count");
		goto retry_send;
	}
	return (0);
}

static int32_t req_setup_recv(struct conn_info *conn_info)
{
	int32_t res;
	struct msghdr msg_recv;
	struct iovec iov_recv;
	int32_t authenticated = 0;

#ifdef QB_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE(sizeof(struct ucred))];
	int32_t off = 0;
	int32_t on = 1;
	struct ucred *cred;
#endif

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

	iov_recv.iov_base = &conn_info->setup_msg[conn_info->setup_bytes_read];
	iov_recv.iov_len =
	    sizeof(mar_req_setup_t) - conn_info->setup_bytes_read;
#ifdef QB_LINUX
	setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
#endif

retry_recv:
	res = recvmsg(conn_info->fd, &msg_recv, MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		api->stats_increment_value(conn_info->stats_handle,
					   "recv_retry_count");
		goto retry_recv;
	} else if (res == -1 && errno != EAGAIN) {
		return (0);
	} else if (res == 0) {
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		ipc_disconnect(conn_info);
		return 0;
#else
		return (-1);
#endif
	}
	conn_info->setup_bytes_read += res;

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
		uid_t euid = -1;
		gid_t egid = -1;

		if (getpeerucred(conn_info->fd, &uc) == 0) {
			euid = ucred_geteuid(uc);
			egid = ucred_getegid(uc);
			conn_info->client_pid = ucred_getpid(uc);
			if (api->security_valid(euid, egid)) {
				authenticated = 1;
			}
			ucred_free(uc);
		}
	}
#elif HAVE_GETPEEREID
/*
 * Usually MacOSX systems
 */

	{
		uid_t euid;
		gid_t egid;

		/*
		 * TODO get the peer's pid.
		 * conn_info->client_pid = ?;
		 */
		euid = -1;
		egid = -1;
		if (getpeereid(conn_info->fd, &euid, &egid) == 0) {
			if (api->security_valid(euid, egid)) {
				authenticated = 1;
			}
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
		conn_info->client_pid = cred->pid;
		if (api->security_valid(cred->uid, cred->gid)) {
			authenticated = 1;
		}
	}
#else /* no credentials */
	authenticated = 1;
	qb_util_log(LOG_ERR,
		    "Platform does not support IPC authentication.  Using no authentication\n");
#endif /* no credentials */

	if (authenticated == 0) {
		qb_util_log(LOG_ERR, "Invalid IPC credentials.\n");
		ipc_disconnect(conn_info);
		return (-1);
	}

	if (conn_info->setup_bytes_read == sizeof(mar_req_setup_t)) {
#ifdef QB_LINUX
		setsockopt(conn_info->fd, SOL_SOCKET, SO_PASSCRED,
			   &off, sizeof(off));
#endif
		return (1);
	}
	return (0);
}

static void ipc_disconnect(struct conn_info *conn_info)
{
	if (conn_info->state == CONN_STATE_THREAD_INACTIVE) {
		conn_info->state = CONN_STATE_DISCONNECT_INACTIVE;
		return;
	}
	if (conn_info->state != CONN_STATE_THREAD_ACTIVE) {
		return;
	}
	pthread_mutex_lock(&conn_info->mutex);
	conn_info->state = CONN_STATE_THREAD_REQUEST_EXIT;
	pthread_mutex_unlock(&conn_info->mutex);

	sem_post_exit_thread(conn_info);
}

static int32_t conn_info_create(int32_t fd)
{
	struct conn_info *conn_info;

	conn_info = api->malloc(sizeof(struct conn_info));
	if (conn_info == NULL) {
		return (-1);
	}
	memset(conn_info, 0, sizeof(struct conn_info));

	conn_info->fd = fd;
	conn_info->client_pid = 0;
	conn_info->service = SOCKET_SERVICE_INIT;
	conn_info->state = CONN_STATE_THREAD_INACTIVE;
	qb_list_init(&conn_info->outq_head);
	qb_list_init(&conn_info->list);
	qb_list_init(&conn_info->zcb_mapped_qb_list_head);
	qb_list_add(&conn_info->list, &conn_info_qb_list_head);

	api->poll_dispatch_add(fd, conn_info);

	return (0);
}

#if defined(QB_LINUX) || defined(QB_SOLARIS)
/* SUN_LEN is broken for abstract namespace
 */
#define QB_SUN_LEN(a) sizeof(*(a))
#else
#define QB_SUN_LEN(a) SUN_LEN(a)
#endif

/*
 * Exported functions
 */
extern void qb_ipcs_ipc_init(struct qb_ipcs_init_state *init_state)
{
	int32_t server_fd;
	struct sockaddr_un un_addr;
	int32_t res;

	api = init_state;
	api->stats_create_connection = dummy_stats_create_connection;
	api->stats_destroy_connection = dummy_stats_destroy_connection;
	api->stats_update_value = dummy_stats_update_value;
	api->stats_increment_value = dummy_stats_increment_value;

	/*
	 * Create socket for IPC clients, name socket, listen for connections
	 */
#if defined(QB_SOLARIS)
	server_fd = socket(PF_UNIX, SOCK_STREAM, 0);
#else
	server_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (server_fd == -1) {
		qb_util_log(LOG_CRIT,
			    "Cannot create client connections socket.\n");
		api->fatal_error("Can't create library listen socket");
	};

	res = fcntl(server_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		char error_str[100];
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT,
			    "Could not set non-blocking operation on server socket: %s\n",
			    error_str);
		api->fatal_error
		    ("Could not set non-blocking operation on server socket");
	}

	memset(&un_addr, 0, sizeof(struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	un_addr.sun_len = SUN_LEN(&un_addr);
#endif

#if defined(QB_LINUX)
	sprintf(un_addr.sun_path + 1, "%s", api->socket_name);
#else
	{
		struct stat stat_out;
		res = stat(SOCKETDIR, &stat_out);
		if (res == -1 || (res == 0 && !S_ISDIR(stat_out.st_mode))) {
			qb_util_log(LOG_CRIT,
				    "Required directory not present %s\n",
				    SOCKETDIR);
			api->fatal_error("Please create required directory.");
		}
		sprintf(un_addr.sun_path, "%s/%s", SOCKETDIR, api->socket_name);
		unlink(un_addr.sun_path);
	}
#endif

	res =
	    bind(server_fd, (struct sockaddr *)&un_addr, QB_SUN_LEN(&un_addr));
	if (res) {
		char error_str[100];
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT, "Could not bind AF_UNIX (%s): %s.\n",
			    un_addr.sun_path, error_str);
		api->fatal_error("Could not bind to AF_UNIX socket\n");
	}

	/*
	 * Allow eveyrone to write to the socket since the IPC layer handles
	 * security automatically
	 */
#if !defined(QB_LINUX)
	res = chmod(un_addr.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
	listen(server_fd, SERVER_BACKLOG);

	/*
	 * Setup connection dispatch routine
	 */
	api->poll_accept_add(server_fd);
}

void qb_ipcs_ipc_exit(void)
{
	struct qb_list_head *list;
	struct conn_info *conn_info;
	uint32_t res;

	for (list = conn_info_qb_list_head.next;
	     list != &conn_info_qb_list_head; list = list->next) {

		conn_info = qb_list_entry(list, struct conn_info, list);

		if (conn_info->state != CONN_STATE_THREAD_ACTIVE)
			continue;

		ipc_disconnect(conn_info);

#if _POSIX_THREAD_PROCESS_SHARED > 0
		sem_destroy(&conn_info->control_buffer->sem1);
		sem_destroy(&conn_info->control_buffer->sem2);
#else
		semctl(conn_info->semid, 0, IPC_RMID);
#endif

		/*
		 * Unmap memory segments
		 */
		res = munmap((void *)conn_info->control_buffer,
			     conn_info->control_size);
		qb_rb_close(conn_info->request_rb);
		res = munmap((void *)conn_info->response_buffer,
			     conn_info->response_size);
		res = circular_memory_unmap(conn_info->dispatch_buffer,
					    conn_info->dispatch_size);
	}
}

int32_t qb_ipcs_ipc_service_exit(uint32_t service)
{
	struct qb_list_head *list, *list_next;
	struct conn_info *conn_info;

	for (list = conn_info_qb_list_head.next;
	     list != &conn_info_qb_list_head; list = list_next) {

		list_next = list->next;

		conn_info = qb_list_entry(list, struct conn_info, list);

		if (conn_info->service != service ||
		    (conn_info->state != CONN_STATE_THREAD_ACTIVE
		     && conn_info->state != CONN_STATE_THREAD_REQUEST_EXIT)) {
			continue;
		}

		ipc_disconnect(conn_info);
		api->poll_dispatch_destroy(conn_info->fd, NULL);
		while (conn_info_destroy(conn_info) != -1) ;

		/*
		 * We will return to prevent token loss. Schedwrk will call us again.
		 */
		return (-1);
	}

	/*
	 * No conn info left in active list. We will traverse thru exit list. If there is any
	 * conn_info->service == service, we will wait to proper end -> return -1
	 */

	for (list = conn_info_exit_qb_list_head.next;
	     list != &conn_info_exit_qb_list_head; list = list->next) {
		conn_info = qb_list_entry(list, struct conn_info, list);

		if (conn_info->service == service) {
			return (-1);
		}
	}

	return (0);
}

/*
 * Get the conn info private data
 */
void *qb_ipcs_private_data_get(void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	return (conn_info->private_data);
}

int32_t qb_ipcs_response_send(void *conn, const void *msg, size_t mlen)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	struct sembuf sop;
#endif
	int32_t res;

	memcpy(conn_info->response_buffer, msg, mlen);

#if _POSIX_THREAD_PROCESS_SHARED > 0
	res = sem_post(&conn_info->control_buffer->sem1);
	if (res == -1) {
		return (-1);
	}
#else
	sop.sem_num = 1;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop(conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		api->stats_increment_value(conn_info->stats_handle,
					   "sem_retry_count");
		goto retry_semop;
	} else if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return (0);
	}
#endif
	api->stats_increment_value(conn_info->stats_handle, "responses");
	return (0);
}

int32_t qb_ipcs_response_iov_send(void *conn, const struct iovec * iov,
				  uint32_t iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	struct sembuf sop;
#endif
	int32_t res;
	int32_t write_idx = 0;
	int32_t i;

	for (i = 0; i < iov_len; i++) {
		memcpy(&conn_info->response_buffer[write_idx],
		       iov[i].iov_base, iov[i].iov_len);
		write_idx += iov[i].iov_len;
	}

#if _POSIX_THREAD_PROCESS_SHARED > 0
	res = sem_post(&conn_info->control_buffer->sem1);
	if (res == -1) {
		return (-1);
	}
#else
	sop.sem_num = 1;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop(conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		api->stats_increment_value(conn_info->stats_handle,
					   "sem_retry_count");
		goto retry_semop;
	} else if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return (0);
	}
#endif
	api->stats_increment_value(conn_info->stats_handle, "responses");
	return (0);
}

static int32_t shared_mem_dispatch_bytes_left(const struct conn_info *conn_info)
{
	uint32_t n_read;
	uint32_t n_write;
	uint32_t bytes_left;

	n_read = conn_info->control_buffer->read;
	n_write = conn_info->control_buffer->write;

	if (n_read <= n_write) {
		bytes_left = conn_info->dispatch_size - n_write + n_read;
	} else {
		bytes_left = n_read - n_write;
	}
	if (bytes_left > 0) {
		bytes_left--;
	}

	return (bytes_left);
}

static void memcpy_dwrap(struct conn_info *conn_info, void *msg, uint32_t len)
{
	uint32_t write_idx;

	write_idx = conn_info->control_buffer->write;

	memcpy(&conn_info->dispatch_buffer[write_idx], msg, len);
	conn_info->control_buffer->write =
	    (write_idx + len) % conn_info->dispatch_size;
}

/**
 * simulate the behaviour in qb_ipcc.c
 */
static int32_t flow_control_event_send(struct conn_info *conn_info, char event)
{
	int32_t new_fc = 0;

	if (event == MESSAGE_RES_OUTQ_NOT_EMPTY ||
	    event == MESSAGE_RES_ENABLE_FLOWCONTROL) {
		new_fc = 1;
	}

	if (conn_info->flow_control_state != new_fc) {
		if (new_fc == 1) {
			qb_util_log(LOG_INFO,
				    "Enabling flow control for %d, event %d\n",
				    conn_info->client_pid, event);
		} else {
			qb_util_log(LOG_INFO,
				    "Disabling flow control for %d, event %d\n",
				    conn_info->client_pid, event);
		}
		conn_info->flow_control_state = new_fc;
		api->stats_update_value(conn_info->stats_handle, "flow_control",
					&conn_info->flow_control_state,
					sizeof(conn_info->flow_control_state));
		api->stats_increment_value(conn_info->stats_handle,
					   "flow_control_count");
	}

	return send(conn_info->fd, &event, 1, MSG_NOSIGNAL);
}

static void msg_send(void *conn, const struct iovec *iov, uint32_t iov_len,
		     int32_t locked)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	struct sembuf sop;
#endif
	int32_t res;
	int32_t i;

	for (i = 0; i < iov_len; i++) {
		memcpy_dwrap(conn_info, iov[i].iov_base, iov[i].iov_len);
	}

	if (qb_list_empty(&conn_info->outq_head))
		res =
		    flow_control_event_send(conn_info, MESSAGE_RES_OUTQ_EMPTY);
	else
		res =
		    flow_control_event_send(conn_info,
					    MESSAGE_RES_OUTQ_NOT_EMPTY);

	if (res == -1 && errno == EAGAIN) {
		if (locked == 0) {
			pthread_mutex_lock(&conn_info->mutex);
		}
		conn_info->pending_semops += 1;
		if (locked == 0) {
			pthread_mutex_unlock(&conn_info->mutex);
		}
		api->poll_dispatch_modify(conn_info->fd,
					  POLLIN | POLLOUT | POLLNVAL);
	} else if (res == -1) {
		ipc_disconnect(conn_info);
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	res = sem_post(&conn_info->control_buffer->sem2);
#else
	sop.sem_num = 2;
	sop.sem_op = 1;
	sop.sem_flg = 0;

retry_semop:
	res = semop(conn_info->semid, &sop, 1);
	if ((res == -1) && (errno == EINTR || errno == EAGAIN)) {
		api->stats_increment_value(conn_info->stats_handle,
					   "sem_retry_count");
		goto retry_semop;
	} else if ((res == -1) && (errno == EINVAL || errno == EIDRM)) {
		return;
	}
#endif
	api->stats_increment_value(conn_info->stats_handle, "dispatched");
}

static void outq_flush(struct conn_info *conn_info)
{
	struct qb_list_head *list, *list_next;
	struct outq_item *outq_item;
	uint32_t bytes_left;
	struct iovec iov;
	int32_t res;

	pthread_mutex_lock(&conn_info->mutex);
	if (qb_list_empty(&conn_info->outq_head)) {
		res =
		    flow_control_event_send(conn_info,
					    MESSAGE_RES_OUTQ_FLUSH_NR);
		pthread_mutex_unlock(&conn_info->mutex);
		return;
	}
	for (list = conn_info->outq_head.next;
	     list != &conn_info->outq_head; list = list_next) {

		list_next = list->next;
		outq_item = qb_list_entry(list, struct outq_item, list);
		bytes_left = shared_mem_dispatch_bytes_left(conn_info);
		if (bytes_left > outq_item->mlen) {
			iov.iov_base = outq_item->msg;
			iov.iov_len = outq_item->mlen;
			msg_send(conn_info, &iov, 1, MSG_SEND_UNLOCKED);
			qb_list_del(list);
			api->free(iov.iov_base);
			api->free(outq_item);
			api->stats_decrement_value(conn_info->stats_handle,
						   "queue_size");
		} else {
			break;
		}
	}
	pthread_mutex_unlock(&conn_info->mutex);
}

static int32_t priv_change(struct conn_info *conn_info)
{
	mar_req_priv_change req_priv_change;
	uint32_t res;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	union semun semun;
	struct semid_ds ipc_set;
	int32_t i;
#endif

retry_recv:
	res = recv(conn_info->fd, &req_priv_change,
		   sizeof(mar_req_priv_change), MSG_NOSIGNAL);
	if (res == -1 && errno == EINTR) {
		api->stats_increment_value(conn_info->stats_handle,
					   "recv_retry_count");
		goto retry_recv;
	}
	if (res == -1 && errno == EAGAIN) {
		api->stats_increment_value(conn_info->stats_handle,
					   "recv_retry_count");
		goto retry_recv;
	}
	if (res == -1 && errno != EAGAIN) {
		return (-1);
	}
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
	/* Error on socket, EOF is detected when recv return 0
	 */
	if (res == 0) {
		return (-1);
	}
#endif

#if _POSIX_THREAD_PROCESS_SHARED < 1
	ipc_set.sem_perm.uid = req_priv_change.euid;
	ipc_set.sem_perm.gid = req_priv_change.egid;
	ipc_set.sem_perm.mode = 0600;

	semun.buf = &ipc_set;

	for (i = 0; i < 3; i++) {
		res = semctl(conn_info->semid, 0, IPC_SET, semun);
		if (res == -1) {
			return (-1);
		}
	}
#endif
	return (0);
}

static void msg_send_or_queue(void *conn, const struct iovec *iov,
			      uint32_t iov_len)
{
	struct conn_info *conn_info = (struct conn_info *)conn;
	uint32_t bytes_left;
	uint32_t bytes_msg = 0;
	int32_t i;
	struct outq_item *outq_item;
	char *write_buf = 0;

	/*
	 * Exit transmission if the connection is dead
	 */
	if (ipc_thread_active(conn) == 0) {
		return;
	}

	bytes_left = shared_mem_dispatch_bytes_left(conn_info);
	for (i = 0; i < iov_len; i++) {
		bytes_msg += iov[i].iov_len;
	}
	if (bytes_left < bytes_msg || qb_list_empty(&conn_info->outq_head) == 0) {
		outq_item = api->malloc(sizeof(struct outq_item));
		if (outq_item == NULL) {
			ipc_disconnect(conn);
			return;
		}
		outq_item->msg = api->malloc(bytes_msg);
		if (outq_item->msg == 0) {
			api->free(outq_item);
			ipc_disconnect(conn);
			return;
		}

		write_buf = outq_item->msg;
		for (i = 0; i < iov_len; i++) {
			memcpy(write_buf, iov[i].iov_base, iov[i].iov_len);
			write_buf += iov[i].iov_len;
		}
		outq_item->mlen = bytes_msg;
		qb_list_init(&outq_item->list);
		pthread_mutex_lock(&conn_info->mutex);
		if (qb_list_empty(&conn_info->outq_head)) {
			conn_info->notify_flow_control_enabled = 1;
			api->poll_dispatch_modify(conn_info->fd,
						  POLLIN | POLLOUT | POLLNVAL);
		}
		qb_list_add_tail(&outq_item->list, &conn_info->outq_head);
		pthread_mutex_unlock(&conn_info->mutex);
		api->stats_increment_value(conn_info->stats_handle,
					   "queue_size");
		return;
	}
	msg_send(conn, iov, iov_len, MSG_SEND_LOCKED);
}

void qb_ipcs_refcount_inc(void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock(&conn_info->mutex);
	conn_info->refcount++;
	pthread_mutex_unlock(&conn_info->mutex);
}

void qb_ipcs_refcount_dec(void *conn)
{
	struct conn_info *conn_info = (struct conn_info *)conn;

	pthread_mutex_lock(&conn_info->mutex);
	conn_info->refcount--;
	pthread_mutex_unlock(&conn_info->mutex);
}

int32_t qb_ipcs_dispatch_send(void *conn, const void *msg, size_t mlen)
{
	struct iovec iov;

	iov.iov_base = (void *)msg;
	iov.iov_len = mlen;

	msg_send_or_queue(conn, &iov, 1);
	return (0);
}

int32_t qb_ipcs_dispatch_iov_send(void *conn, const struct iovec * iov,
				  uint32_t iov_len)
{
	msg_send_or_queue(conn, iov, iov_len);
	return (0);
}

int32_t qb_ipcs_handler_accept(int32_t fd, int32_t revent, void *data)
{
	socklen_t addrlen;
	struct sockaddr_un un_addr;
	int32_t new_fd;
#ifdef QB_LINUX
	int32_t on = 1;
#endif
	int32_t res;

	addrlen = sizeof(struct sockaddr_un);

retry_accept:
	new_fd = accept(fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1) {
		char error_str[100];
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Could not accept Library connection: %s\n",
			    error_str);
		return (0);	/* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = fcntl(new_fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		char error_str[100];
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

	/*
	 * Request credentials of sender provided by kernel
	 */
#ifdef QB_LINUX
	setsockopt(new_fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
#endif

	res = conn_info_create(new_fd);
	if (res != 0) {
		close(new_fd);
	}

	return (0);
}

static char *pid_to_name(pid_t pid, char *out_name, size_t name_len)
{
	char *name;
	char *rest;
	FILE *fp;
	char fname[32];
	char buf[256];

	snprintf(fname, 32, "/proc/%d/stat", pid);
	fp = fopen(fname, "r");
	if (!fp) {
		return NULL;
	}

	if (fgets(buf, sizeof(buf), fp) == NULL) {
		fclose(fp);
		return NULL;
	}
	fclose(fp);

	name = strrchr(buf, '(');
	if (!name) {
		return NULL;
	}

	/* move past the bracket */
	name++;

	rest = strrchr(buf, ')');

	if (rest == NULL || rest[1] != ' ') {
		return NULL;
	}

	*rest = '\0';
	/* move past the NULL and space */
	rest += 2;

	/* copy the name */
	strncpy(out_name, name, name_len);
	out_name[name_len - 1] = '\0';
	return out_name;
}

static void qb_ipcs_init_conn_stats(struct conn_info *conn)
{
	char conn_name[42];
	char proc_name[32];

	if (conn->client_pid > 0) {
		if (pid_to_name(conn->client_pid, proc_name, sizeof(proc_name)))
			snprintf(conn_name, sizeof(conn_name), "%s:%d:%d",
				 proc_name, conn->client_pid, conn->fd);
		else
			snprintf(conn_name, sizeof(conn_name), "%d:%d",
				 conn->client_pid, conn->fd);
	} else
		snprintf(conn_name, sizeof(conn_name), "%d", conn->fd);

	conn->stats_handle =
	    api->stats_create_connection(conn_name, conn->client_pid, conn->fd);
	api->stats_update_value(conn->stats_handle, "service_id",
				&conn->service, sizeof(conn->service));
}

int32_t qb_ipcs_handler_dispatch(int32_t fd, int32_t revent, void *context)
{
	mar_req_setup_t *req_setup;
	struct conn_info *conn_info = (struct conn_info *)context;
	int32_t res;
	char buf;

	if (ipc_thread_exiting(conn_info)) {
		return conn_info_destroy(conn_info);
	}

	/*
	 * If an error occurs, request exit
	 */
	if (revent & (POLLERR | POLLHUP)) {
		ipc_disconnect(conn_info);
		return (0);
	}

	/*
	 * Read the header and process it
	 */
	if (conn_info->service == SOCKET_SERVICE_INIT && (revent & POLLIN)) {
		/*
		 * Receive in a nonblocking fashion the request
		 * IF security invalid, send ERR_SECURITY, otherwise
		 * send OK
		 */
		res = req_setup_recv(conn_info);
		if (res == -1) {
			req_setup_send(conn_info, EACCES);
		}
		if (res != 1) {
			return (0);
		}

		pthread_mutex_init(&conn_info->mutex, NULL);
		req_setup = (mar_req_setup_t *) conn_info->setup_msg;
		/*
		 * Is the service registered ?
		 */
		if (api->service_available(req_setup->service) == 0) {
			req_setup_send(conn_info, EEXIST);
			ipc_disconnect(conn_info);
			return (0);
		}
		req_setup_send(conn_info, 0);

#if _POSIX_THREAD_PROCESS_SHARED < 1
		conn_info->semkey = req_setup->semkey;
#endif
		res = memory_map(req_setup->control_file,
				 req_setup->control_size,
				 (void *)&conn_info->control_buffer);
		conn_info->control_size = req_setup->control_size;

		conn_info->request_rb = qb_rb_open(req_setup->request_file,
						   req_setup->request_size,
						   QB_RB_FLAG_SHARED_PROCESS);
		conn_info->request_size = req_setup->request_size;

		res = memory_map(req_setup->response_file,
				 req_setup->response_size,
				 (void *)&conn_info->response_buffer);
		conn_info->response_size = req_setup->response_size;

		res = circular_memory_map(req_setup->dispatch_file,
					  req_setup->dispatch_size,
					  (void *)&conn_info->dispatch_buffer);
		conn_info->dispatch_size = req_setup->dispatch_size;

		conn_info->service = req_setup->service;
		conn_info->refcount = 0;
		conn_info->notify_flow_control_enabled = 0;
		conn_info->setup_bytes_read = 0;

#if _POSIX_THREAD_PROCESS_SHARED < 1
		conn_info->semid = semget(conn_info->semkey, 3, 0600);
#endif
		conn_info->pending_semops = 0;

		/*
		 * ipc thread is the only reference at startup
		 */
		conn_info->refcount = 1;
		conn_info->state = CONN_STATE_THREAD_ACTIVE;

		conn_info->private_data =
		    api->malloc(api->private_data_size_get(conn_info->service));
		memset(conn_info->private_data, 0,
		       api->private_data_size_get(conn_info->service));

		api->init_fn_get(conn_info->service) (conn_info);

		/* create stats objects */
		qb_ipcs_init_conn_stats(conn_info);

		pthread_attr_init(&conn_info->thread_attr);
		/*
		 * IA64 needs more stack space then other arches
		 */
#if defined(__ia64__)
		pthread_attr_setstacksize(&conn_info->thread_attr, 400000);
#else
		pthread_attr_setstacksize(&conn_info->thread_attr, 200000);
#endif

		pthread_attr_setdetachstate(&conn_info->thread_attr,
					    PTHREAD_CREATE_JOINABLE);
		res =
		    pthread_create(&conn_info->thread, &conn_info->thread_attr,
				   pthread_ipc_consumer, conn_info);

		/*
		 * Security check - disallow multiple configurations of
		 * the ipc connection
		 */
		if (conn_info->service == SOCKET_SERVICE_INIT) {
			conn_info->service = -1;
		}
	} else if (revent & POLLIN) {
		qb_ipcs_refcount_inc(conn_info);
		res = recv(fd, &buf, 1, MSG_NOSIGNAL);
		if (res == 1) {
			switch (buf) {
			case MESSAGE_REQ_OUTQ_FLUSH:
				outq_flush(conn_info);
				break;
			case MESSAGE_REQ_CHANGE_EUID:
				if (priv_change(conn_info) == -1) {
					ipc_disconnect(conn_info);
				}
				break;
			default:
				res = 0;
				break;
			}
		}
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
		/* On many OS poll never return POLLHUP or POLLERR.
		 * EOF is detected when recvmsg return 0.
		 */
		if (res == 0) {
			ipc_disconnect(conn_info);
			qb_ipcs_refcount_dec(conn_info);
			return (0);
		}
#endif
		qb_ipcs_refcount_dec(conn_info);
	}

	qb_ipcs_refcount_inc(conn_info);
	pthread_mutex_lock(&conn_info->mutex);
	if ((conn_info->state == CONN_STATE_THREAD_ACTIVE)
	    && (revent & POLLOUT)) {
		if (qb_list_empty(&conn_info->outq_head))
			buf = MESSAGE_RES_OUTQ_EMPTY;
		else
			buf = MESSAGE_RES_OUTQ_NOT_EMPTY;

		for (; conn_info->pending_semops;) {
			res = flow_control_event_send(conn_info, buf);
			if (res == 1) {
				conn_info->pending_semops--;
			} else {
				break;
			}
		}
		if (conn_info->notify_flow_control_enabled) {
			res =
			    flow_control_event_send(conn_info,
						    MESSAGE_RES_ENABLE_FLOWCONTROL);
			if (res == 1) {
				conn_info->notify_flow_control_enabled = 0;
			}
		}
		if (conn_info->notify_flow_control_enabled == 0 &&
		    conn_info->pending_semops == 0) {

			api->poll_dispatch_modify(conn_info->fd,
						  POLLIN | POLLNVAL);
		}
	}
	pthread_mutex_unlock(&conn_info->mutex);
	qb_ipcs_refcount_dec(conn_info);

	return (0);
}
