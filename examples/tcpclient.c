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

int
main(int argc, char *argv[])
{
	int sock;
	int32_t res;
	char send_data[1024];
	char recv_data[1024];
	char *newline;
	struct sockaddr_in server_addr;
	struct hostent *host = gethostbyname("127.0.0.1");

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket");
		exit(1);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(5000);
	memcpy(&server_addr.sin_addr, host->h_addr_list[0],
	       sizeof(server_addr.sin_addr));
	bzero(&(server_addr.sin_zero), 8);

	if (connect(sock, (struct sockaddr *)&server_addr,
		    sizeof(struct sockaddr)) == -1) {
		perror("Connect");
		exit(1);
	}

	while (1) {
		printf("\nSEND (q or Q to quit) : ");
		if (fgets(send_data, 1024, stdin) == NULL) {
			continue;
		}
		newline = strrchr(send_data, '\n');
		if (newline) {
			*newline = '\0';
		}
		res = send(sock, send_data, strlen(send_data), 0);
		if (strcasecmp(send_data, "q") == 0) {
			close(sock);
			printf("you typed QUIT\n");
			break;
		}

		if (res > 0) {
			res = recv(sock, recv_data, 1024, 0);
			if (res > 0) {
				recv_data[res] = '\0';
				printf("\nResponse: %s ", recv_data);
			} else {
				perror("recv");
			}
		}
	}
	return EXIT_SUCCESS;
}
