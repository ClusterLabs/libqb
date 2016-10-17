/*
 * Copyright (C) 2006-2007, 2009 Red Hat, Inc.
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
#ifndef QB_IPCC_H_DEFINED
#define QB_IPCC_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <sys/types.h>  /* size_t, ssize_t */
#include <sys/uio.h>  /* iovec */

#include <qb/qbipc_common.h>

/**
 * @file qbipcc.h
 *
 * Client IPC API.
 *
 * @par Lifecycle of an IPC connection.
 * An IPC connection is made to the server with qb_ipcc_connect(). This function
 * connects to the server and requests channels be created for communication.
 * To disconnect, the client either exits or executes the function qb_ipcc_disconnect().
 *
 * @par Synchronous communication
 * The function qb_ipcc_sendv_recv() sends an iovector request and receives a response.
 *
 * @par Asynchronous requests from the client
 * The function qb_ipcc_sendv() sends an iovector request.
 * The function qb_ipcc_send() sends an message buffer request.
 *
 * @par Asynchronous events from the server
 * The qb_ipcc_event_recv() function receives an out-of-band asynchronous message.
 * The asynchronous messages are queued and can provide very high out-of-band performance.
 * To determine when to call qb_ipcc_event_recv() the qb_ipcc_fd_get() call is
 * used to obtain a file descriptor used in the poll() or select() system calls.
 *
 * @example ipcclient.c
 * This is an example of how to use the client.
 */

typedef struct qb_ipcc_connection qb_ipcc_connection_t;

/**
 * Create a connection to an IPC service.
 *
 * @param name name of the service.
 * @param max_msg_size biggest msg size.
 * @return NULL (error: see errno) or a connection object.
 *
 * @note It is recommended to do a one time check on the
 * max_msg_size value using qb_ipcc_verify_dgram_max_msg_size
 * _BEFORE_ calling the connect function when IPC_SOCKET is in use.
 * Some distributions while allow large message buffers to be
 * set on the socket, but not actually honor them because of
 * kernel state values.  The qb_ipcc_verify_dgram_max_msg_size
 * function both sets the socket buffer size and verifies it by
 * doing a send/recv.
 */
qb_ipcc_connection_t*
qb_ipcc_connect(const char *name, size_t max_msg_size);

/**
 * Test kernel dgram socket buffers to verify the largest size up
 * to the max_msg_size value a single msg can be. Rounds down to the
 * nearest 1k.
 *
 * @param max_msg_size biggest msg size.
 * @return -1 if max size can not be detected, positive value
 *         representing the largest single msg up to max_msg_size
 *         that can successfully be sent over a unix dgram socket.
 */
int32_t
qb_ipcc_verify_dgram_max_msg_size(size_t max_msg_size);


/**
 * Disconnect an IPC connection.
 *
 * @param c connection instance
 */
void qb_ipcc_disconnect(qb_ipcc_connection_t* c);

/**
 * Get the file descriptor to poll.
 *
 * @param c connection instance
 * @param fd (out) file descriptor to poll
 */
int32_t qb_ipcc_fd_get(qb_ipcc_connection_t* c, int32_t * fd);

/**
 * Set the maximum allowable flowcontrol value.
 *
 * @note the default is 1
 *
 * @param c connection instance
 * @param max the max allowable flowcontrol value (1 or 2)
 */
int32_t qb_ipcc_fc_enable_max_set(qb_ipcc_connection_t * c, uint32_t max);

/**
 * Send a message.
 *
 * @param c connection instance
 * @param msg_ptr pointer to a message to send
 * @param msg_len the size of the message
 * @return (size sent, -errno == error)
 *
 * @note the msg_ptr must include a qb_ipc_request_header at
 * the top of the message. The server will read the size field
 * to determine how much to recv.
 */
ssize_t qb_ipcc_send(qb_ipcc_connection_t* c, const void *msg_ptr,
                     size_t msg_len);
/**
 * Send a message (iovec).
 *
 * @param c connection instance
 * @param iov pointer to an iovec struct to send
 * @param iov_len the number of iovecs used
 * @return (size sent, -errno == error)
 *
 * @note the iov[0] must be a qb_ipc_request_header. The server will
 * read the size field to determine how much to recv.
 */
ssize_t qb_ipcc_sendv(qb_ipcc_connection_t* c, const struct iovec* iov,
	size_t iov_len);
/**
 * Receive a response.
 *
 * @param c connection instance
 * @param msg_ptr pointer to a message buffer to receive into
 * @param msg_len the size of the buffer
 * @param ms_timeout max time to wait for a response
 * @return (size recv'ed, -errno == error)
 *
 * @note that msg_ptr will include a qb_ipc_response_header at
 * the top of the message.
 */
ssize_t qb_ipcc_recv(qb_ipcc_connection_t* c, void *msg_ptr,
                     size_t msg_len, int32_t ms_timeout);

/**
 * This is a convenience function that simply sends and then recvs.
 *
 * @param c connection instance
 * @param iov pointer to an iovec struct to send
 * @param iov_len the number of iovecs used
 * @param msg_ptr pointer to a message buffer to receive into
 * @param msg_len the size of the buffer
 * @param ms_timeout max time to wait for a response
 *
 * @note the iov[0] must include a qb_ipc_request_header at
 * the top of the message. The server will read the size field
 * to determine how much to recv.
 * @note that msg_ptr will include a qb_ipc_response_header at
 * the top of the message.
 *
 * @see qb_ipcc_sendv() qb_ipcc_recv()
 */
ssize_t qb_ipcc_sendv_recv(qb_ipcc_connection_t *c,
			   const struct iovec *iov, uint32_t iov_len,
			   void *msg_ptr, size_t msg_len,
			   int32_t ms_timeout);

/**
 * Receive an event.
 *
 * @param c connection instance
 * @param msg_ptr pointer to a message buffer to receive into
 * @param msg_len the size of the buffer
 * @param ms_timeout time in milli seconds to wait for a message
 *        0 == no wait, negative == block, positive == wait X ms.
 * @param ms_timeout max time to wait for a response
 * @return size of the message or error (-errno)
 *
 * @note that msg_ptr will include a qb_ipc_response_header at
 * the top of the message.
 */
ssize_t qb_ipcc_event_recv(qb_ipcc_connection_t* c, void *msg_ptr,
			   size_t msg_len, int32_t ms_timeout);

/**
 * Associate a "user" pointer with this connection.
 *
 * @param context the point to associate with this connection.
 * @param c connection instance
 * @see qb_ipcc_context_get()
 */
void qb_ipcc_context_set(qb_ipcc_connection_t *c, void *context);

/**
 * Get the context (set previously)
 *
 * @param c connection instance
 * @return the context
 * @see qb_ipcc_context_set()
 */
void *qb_ipcc_context_get(qb_ipcc_connection_t *c);

/**
 * Is the connection connected?
 *
 * @param c connection instance
 * @retval QB_TRUE when connected
 * @retval QB_FALSE when not connected
 */
int32_t qb_ipcc_is_connected(qb_ipcc_connection_t *c);

/**
 * What is the actual buffer size used after the connection.
 *
 * @note The buffer size is guaranteed to be at least the size
 * of the value given in qb_ipcc_connect, but it is possible
 * the server will enforce a larger size depending on the
 * implementation.  If the server side is known to enforce
 * a buffer size, use this function after the client connection
 * is established to retrieve the buffer size in use.  It is
 * important for the client side to know the buffer size in use
 * so the client can successfully retrieve large server events.
 *
 * @param c connection instance
 * @retval connection size in bytes or -error code
 */
int32_t qb_ipcc_get_buffer_size(qb_ipcc_connection_t * c);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCC_H_DEFINED */
