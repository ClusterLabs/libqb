/*
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>,
 *         Angus Salkeld <asalkeld@redhat.com>
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

#ifndef QB_IPCS_H_DEFINED
#define QB_IPCS_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <sys/types.h>  /* size_t, ssize_t */
#include <sys/uio.h>  /* iovec */

#include <qb/qbipc_common.h>  /* qb_ipc_type */
#include <qb/qbloop.h> /* qb_loop_priority */

/**
 * @file qbipcs.h
 *
 * Server IPC API.
 *
 * @example ipcserver.c
 */

enum qb_ipcs_rate_limit {
	QB_IPCS_RATE_FAST,
	QB_IPCS_RATE_NORMAL,
	QB_IPCS_RATE_SLOW,
	QB_IPCS_RATE_OFF,
	QB_IPCS_RATE_OFF_2,
};

struct qb_ipcs_connection;
typedef struct qb_ipcs_connection qb_ipcs_connection_t;

struct qb_ipcs_service;
typedef struct qb_ipcs_service qb_ipcs_service_t;

struct qb_ipcs_stats {
	uint32_t active_connections;
	uint32_t closed_connections;
};

struct qb_ipcs_connection_stats {
	int32_t client_pid;
	uint64_t requests;
	uint64_t responses;
	uint64_t events;
	uint64_t send_retries;
	uint64_t recv_retries;
	int32_t flow_control_state;
	uint64_t flow_control_count;
};

struct qb_ipcs_connection_stats_2 {
	int32_t client_pid;
	uint64_t requests;
	uint64_t responses;
	uint64_t events;
	uint64_t send_retries;
	uint64_t recv_retries;
	int32_t flow_control_state;
	uint64_t flow_control_count;
	uint32_t event_q_length;
};

typedef int32_t (*qb_ipcs_dispatch_fn_t) (int32_t fd, int32_t revents,
					  void *data);

typedef int32_t (*qb_ipcs_dispatch_add_fn)(enum qb_loop_priority p,
					   int32_t fd,
					   int32_t events,
					   void *data,
					   qb_ipcs_dispatch_fn_t fn);
typedef int32_t (*qb_ipcs_dispatch_mod_fn)(enum qb_loop_priority p,
					   int32_t fd,
					   int32_t events,
					   void *data,
					   qb_ipcs_dispatch_fn_t fn);
typedef int32_t (*qb_ipcs_dispatch_del_fn)(int32_t fd);

typedef int32_t (*qb_ipcs_job_add_fn)(enum qb_loop_priority p,
				      void *data,
				      qb_loop_job_dispatch_fn dispatch_fn);

struct qb_ipcs_poll_handlers {
	qb_ipcs_job_add_fn job_add;
	qb_ipcs_dispatch_add_fn dispatch_add;
	qb_ipcs_dispatch_mod_fn dispatch_mod;
	qb_ipcs_dispatch_del_fn dispatch_del;
};

/**
 * This callback is to check whether you want to accept a new connection.
 *
 * The type of checks you should do are authentication, service availability
 * or process resource constraints. 
 * @return 0 to accept or -errno to indicate a failure (sent back to the client)
 *
 * @note If connection state data is allocated as a result of this callback
 * being invoked, that data must be freed in the destroy callback. Just because
 * the accept callback returns 0, that does not guarantee the
 * create and closed callback functions will follow.
 * @note you can call qb_ipcs_connection_auth_set() within this function.
 */
typedef int32_t (*qb_ipcs_connection_accept_fn) (qb_ipcs_connection_t *c,
						 uid_t uid, gid_t gid);

/**
 * This is called after a new connection has been created.
 *
 * @note A client connection is not considered connected until
 * this callback is invoked.
 */
typedef void (*qb_ipcs_connection_created_fn) (qb_ipcs_connection_t *c);

/**
 * This is called after a connection has been disconnected.
 *
 * @note This callback will only be invoked if the connection is
 * successfully created.
 * @note if you return anything but 0 this function will be
 * repeatedly called (until 0 is returned).
 *
 * With SHM connections libqb will briefly trap SIGBUS during the
 * disconnect process to guard against server crashes if the mapped
 * file is truncated. The signal will be restored afterwards.
 */
typedef int32_t (*qb_ipcs_connection_closed_fn) (qb_ipcs_connection_t *c);

/**
 * This is called just before a connection is freed.
 */
typedef void (*qb_ipcs_connection_destroyed_fn) (qb_ipcs_connection_t *c);

/**
 * This is the message processing calback.
 * It is called with the message data.
 */
typedef int32_t (*qb_ipcs_msg_process_fn) (qb_ipcs_connection_t *c,
		void *data, size_t size);

struct qb_ipcs_service_handlers {
	qb_ipcs_connection_accept_fn connection_accept;
	qb_ipcs_connection_created_fn connection_created;
	qb_ipcs_msg_process_fn msg_process;
	qb_ipcs_connection_closed_fn connection_closed;
	qb_ipcs_connection_destroyed_fn connection_destroyed;
};

/**
 * Create a new IPC server.
 *
 * @param name for clients to connect to.
 * @param service_id an integer to associate with the service
 * @param type transport type.
 * @param handlers callbacks.
 * @return the new service instance.
 */
qb_ipcs_service_t* qb_ipcs_create(const char *name,
				  int32_t service_id,
				  enum qb_ipc_type type,
				  struct qb_ipcs_service_handlers *handlers);


/**
 * Increase the reference counter on the service object.
 *
 * @param s service instance
 */
void qb_ipcs_ref(qb_ipcs_service_t *s);

/**
 * Decrease the reference counter on the service object.
 *
 * @param s service instance
 */
void qb_ipcs_unref(qb_ipcs_service_t *s);

/**
 * Set your poll callbacks.
 *
 * @param s service instance
 * @param handlers the handlers that you want ipcs to use.
 */
void qb_ipcs_poll_handlers_set(qb_ipcs_service_t* s,
	struct qb_ipcs_poll_handlers *handlers);

/**
 * Associate a "user" pointer with this service.
 *
 * @param s service instance
 * @param context the pointer to associate with this service.
 * @see qb_ipcs_service_context_get()
 */
void qb_ipcs_service_context_set(qb_ipcs_service_t* s,
	void *context);

/**
 * Get the context (set previously)
 *
 * @param s service instance
 * @return the context
 * @see qb_ipcs_service_context_set()
 */
void *qb_ipcs_service_context_get(qb_ipcs_service_t* s);

/**
 * run the new IPC server.
 * @param s service instance
 * @return 0 == ok; -errno to indicate a failure. Service is destroyed on failure.
 */
int32_t qb_ipcs_run(qb_ipcs_service_t* s);

/**
 * Destroy the IPC server.
 *
 * @param s service instance to destroy
 */
void qb_ipcs_destroy(qb_ipcs_service_t* s);

/**
 * Limit the incoming request rate.
 * @param s service instance
 * @param rl the new rate
 */
void qb_ipcs_request_rate_limit(qb_ipcs_service_t* s,
			       	enum qb_ipcs_rate_limit rl);

/**
 * Send a response to a incoming request.
 *
 * @param c connection instance
 * @param data the message to send
 * @param size the size of the message
 * @return size sent or -errno for errors
 *
 * @note the data must include a qb_ipc_response_header at
 * the top of the message. The client will read the size field
 * to determine how much to recv.
 */
ssize_t qb_ipcs_response_send(qb_ipcs_connection_t *c, const void *data,
			      size_t size);

/**
 * Send a response to a incoming request.
 *
 * @param c connection instance
 * @param iov the iovec struct that points to the message to send
 * @param iov_len the number of iovecs.
 * @return size sent or -errno for errors
 *
 * @note the iov[0] must be a qb_ipc_response_header. The client will
 * read the size field to determine how much to recv.
 *
 * @note When send returns -EMSGSIZE, this means the msg is too
 * large and will never succeed. To determine the max msg size
 * a client can be sent, use qb_ipcs_connection_get_buffer_size()
 */
ssize_t qb_ipcs_response_sendv(qb_ipcs_connection_t *c,
			       const struct iovec * iov, size_t iov_len);

/**
 * Send an asynchronous event message to the client.
 *
 * @param c connection instance
 * @param data the message to send
 * @param size the size of the message
 * @return size sent or -errno for errors
 *
 * @note the data must include a qb_ipc_response_header at
 * the top of the message. The client will read the size field
 * to determine how much to recv.
 *
 * @note When send returns -EMSGSIZE, this means the msg is too
 * large and will never succeed. To determine the max msg size
 * a client can be sent, use qb_ipcs_connection_get_buffer_size()
 */
ssize_t qb_ipcs_event_send(qb_ipcs_connection_t *c, const void *data,
			   size_t size);

/**
 * Send an asynchronous event message to the client.
 *
 * @param c connection instance
 * @param iov the iovec struct that points to the message to send
 * @param iov_len the number of iovecs.
 * @return size sent or -errno for errors
 *
 * @note the iov[0] must be a qb_ipc_response_header. The client will
 * read the size field to determine how much to recv.
 *
 * @note When send returns -EMSGSIZE, this means the msg is too
 * large and will never succeed. To determine the max msg size
 * a client can be sent, use qb_ipcs_connection_get_buffer_size()
 */
ssize_t qb_ipcs_event_sendv(qb_ipcs_connection_t *c, const struct iovec * iov,
			    size_t iov_len);

/**
 * Increment the connection's reference counter.
 *
 * @param c connection instance
 */
void qb_ipcs_connection_ref(qb_ipcs_connection_t *c);

/**
 * Decrement the connection's reference counter.
 *
 * @param c connection instance
 */
void qb_ipcs_connection_unref(qb_ipcs_connection_t *c);

/**
 * Disconnect from this client.
 *
 * @param c connection instance
 */
void qb_ipcs_disconnect(qb_ipcs_connection_t *c);

/**
 * Get the service id related to this connection's service.
 * (as passed into qb_ipcs_create()
 *
 * @return service id.
 */
int32_t qb_ipcs_service_id_get(qb_ipcs_connection_t *c);

/**
 * Associate a "user" pointer with this connection.
 *
 * @param context the point to associate with this connection.
 * @param c connection instance
 * @see qb_ipcs_context_get()
 */
void qb_ipcs_context_set(qb_ipcs_connection_t *c, void *context);

/**
 * Get the context (set previously)
 *
 * @param c connection instance
 * @return the context
 * @see qb_ipcs_context_set()
 */
void *qb_ipcs_context_get(qb_ipcs_connection_t *c);

/**
 * Get the context previously set on the service backing this connection
 *
 * @param c connection instance
 * @return the context
 * @see qb_ipcs_service_context_set
 */
void *qb_ipcs_connection_service_context_get(qb_ipcs_connection_t *c);

/**
 * Get the connection statistics.
 *
 * @deprecated from v0.13.0 onwards, use qb_ipcs_connection_stats_get_2
 * @param stats (out) the statistics structure
 * @param clear_after_read clear stats after copying them into stats
 * @param c connection instance
 * @return 0 == ok; -errno to indicate a failure
 */
int32_t qb_ipcs_connection_stats_get(qb_ipcs_connection_t *c,
				     struct qb_ipcs_connection_stats* stats,
				     int32_t clear_after_read);
/**
 * Get (and allocate) the connection statistics.
 *
 * @param clear_after_read clear stats after copying them into stats
 * @param c connection instance
 * @retval NULL if no memory or invalid connection
 * @retval allocated statistics structure (user must free it).
 */
struct qb_ipcs_connection_stats_2*
qb_ipcs_connection_stats_get_2(qb_ipcs_connection_t *c,
			       int32_t clear_after_read);

/**
 * Get the service statistics.
 *
 * @param stats (out) the statistics structure
 * @param clear_after_read clear stats after copying them into stats
 * @param pt service instance
 * @return 0 == ok; -errno to indicate a failure
 */
int32_t qb_ipcs_stats_get(qb_ipcs_service_t* pt,
			  struct qb_ipcs_stats* stats,
			  int32_t clear_after_read);

/**
 * Get the first connection.
 *
 * @note call qb_ipcs_connection_unref() after using the connection.
 *
 * @param pt service instance
 * @return first connection
 */
qb_ipcs_connection_t * qb_ipcs_connection_first_get(qb_ipcs_service_t* pt);

/**
 * Get the next connection.
 *
 * @note call qb_ipcs_connection_unref() after using the connection.
 *
 * @param pt service instance
 * @param current current connection
 * @return next connection
 */
qb_ipcs_connection_t * qb_ipcs_connection_next_get(qb_ipcs_service_t* pt,
						   qb_ipcs_connection_t *current);

/**
 * Set the permissions on and shared memory files so that both processes can
 * read and write to them.
 *
 * @param conn connection instance
 * @param uid the user id to set.
 * @param gid the group id to set.
 * @param mode the mode to set.
 *
 * @see chmod() chown()
 * @note this must be called within the qb_ipcs_connection_accept_fn()
 * callback.
 */
void qb_ipcs_connection_auth_set(qb_ipcs_connection_t *conn, uid_t uid,
				 gid_t gid, mode_t mode);

/**
 * Retrieve the connection ipc buffer size. This reflects the
 * largest size msg that can be sent or received.
 *
 * @param conn connection instance
 * @return msg size in bytes, negative value on error.
 */
int32_t qb_ipcs_connection_get_buffer_size(qb_ipcs_connection_t *conn);

/**
 * Enforce the max buffer size clients must use from the server side.
 *
 * @note Setting this will force client connections to use at least
 * max_buf_size bytes as their buffer size.  If this value is not set
 * on the server, the clients enforce their own buffer sizes.
 *
 * @param s ipc server instance
 * @param max_buf_size represented in bytes
 */
void qb_ipcs_enforce_buffer_size(qb_ipcs_service_t *s, uint32_t max_buf_size);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCS_H_DEFINED */
