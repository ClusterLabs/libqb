/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif /* HAVE_ARPA_INET_H */
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif /* HAVE_NETDB_H */
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */
#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif /* HAVE_SYS_POLL_H */

#include <qb/qbdefs.h>
#include <qb/qbloop.h>

static int32_t
sock_read_fn(int32_t fd, int32_t revents, void *data)
{
	char recv_data[1024];
	char send_data[1024];
	int bytes_recieved;

	if (revents & POLLHUP) {
		printf("Socket %d peer closed\n", fd);
		close(fd);
		return QB_FALSE;
	}

	bytes_recieved = recv(fd, recv_data, 1024, 0);
	if (bytes_recieved < 0) {
		perror("recv");
		return QB_TRUE;
	}
	recv_data[bytes_recieved] = '\0';

	if (strcmp(recv_data, "q") == 0 || strcmp(recv_data, "Q") == 0) {
		printf("Quiting connection from socket %d\n", fd);
		close(fd);
		return QB_FALSE;
	} else {
		printf("Recieved: %s\n", recv_data);
		snprintf(send_data, 1024, "ACK %d bytes", bytes_recieved);
		if (send(fd, send_data, strlen(send_data), 0) < 0) {
			close(fd);
			return QB_FALSE;
		}
	}
	return QB_TRUE;
}

static int32_t
sock_accept_fn(int32_t fd, int32_t revents, void *data)
{
	struct sockaddr_in client_addr;
	qb_loop_t *ml = (qb_loop_t *) data;
	socklen_t sin_size = sizeof(struct sockaddr_in);
	int connected = accept(fd, (struct sockaddr *)&client_addr, &sin_size);

	if (connected < 0) {
		perror("accept");
		return QB_TRUE;
	}
	printf("I got a connection from (%s , %d)\n",
	       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

	qb_loop_poll_add(ml, QB_LOOP_MED, connected, POLLIN, ml, sock_read_fn);

	return QB_TRUE;
}

static int32_t
please_exit_fn(int32_t rsignal, void *data)
{
	qb_loop_t *ml = (qb_loop_t *) data;

	printf("Shutting down at you request...\n");
	qb_loop_stop(ml);
	return QB_FALSE;
}

int
main(int argc, char *argv[])
{
	int sock;
	int true_opt = 1;
	struct sockaddr_in server_addr;
	qb_loop_t *ml = qb_loop_create();

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket");
		exit(1);
	}

	if (setsockopt(sock,
		       SOL_SOCKET,
		       SO_REUSEADDR, &true_opt, sizeof(int)) == -1) {
		perror("Setsockopt");
		exit(1);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(5000);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server_addr.sin_zero), 8);

	printf("TCPServer binding to port 5000\n");
	if (bind(sock,
		 (struct sockaddr *)&server_addr,
		 sizeof(struct sockaddr)) == -1) {
		perror("Unable to bind");
		exit(1);
	}

	printf("TCPServer Waiting for client on port 5000\n");

	if (listen(sock, 5) == -1) {
		perror("Listen");
		exit(1);
	}

	qb_loop_poll_add(ml, QB_LOOP_MED, sock, POLLIN, ml, sock_accept_fn);

	qb_loop_signal_add(ml, QB_LOOP_HIGH, SIGINT, ml, please_exit_fn, NULL);
	qb_loop_run(ml);

	close(sock);
	return 0;
}
