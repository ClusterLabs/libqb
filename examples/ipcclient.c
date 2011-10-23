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
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbipcc.h>

#define MAX_MSG_SIZE (8192)

int
main(int argc, char *argv[])
{
	qb_ipcc_connection_t *conn;
	int32_t res;
	char *buffer[MAX_MSG_SIZE];

	conn = qb_ipcc_connect("ipcserver", MAX_MSG_SIZE);
	if (conn == NULL) {
		perror("qb_ipcc_connect");
		exit(1);
	}

	while(1) {
		struct qb_ipc_request_header  *req_header = (struct qb_ipc_request_header *)buffer;
		struct qb_ipc_response_header *res_header = (struct qb_ipc_response_header *)buffer;
		char *data = (char*)buffer + sizeof(struct qb_ipc_request_header);

		printf("SEND (q or Q to quit) : ");
		if (gets(data) == NULL) {
			continue;
		}

		if (strcmp(data , "q") != 0 &&
		    strcmp(data , "Q") != 0) {
			req_header->id = QB_IPC_MSG_USER_START + 3;
			req_header->size = sizeof(struct qb_ipc_request_header) + strlen(data) + 1;
			res = qb_ipcc_send(conn, req_header, req_header->size);
			if (res < 0) {
				perror("qb_ipcc_send");
			}
		} else {
			break;
		}

		if (res > 0) {
			res = qb_ipcc_recv(conn,
					   buffer,
					   MAX_MSG_SIZE, -1);
			if (res < 0) {
				perror("qb_ipcc_recv");
			}
			res_header = (struct qb_ipc_response_header*)buffer;
			data = (char*)buffer + sizeof(struct qb_ipc_response_header);
			data[res - sizeof(struct qb_ipc_response_header)] = '\0';

			printf("Response[%d]: %s \n", res_header->id, data);
		}
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}

