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

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[256];
};

struct my_res {
	struct qb_ipc_response_header hdr;
	char message[256];
};

int
main(int argc, char *argv[])
{
	qb_ipcc_connection_t *conn;
	int32_t rc;
	struct my_req req;
	struct my_res res;
	char *newline;

	conn = qb_ipcc_connect("ipcserver", MAX_MSG_SIZE);
	if (conn == NULL) {
		perror("qb_ipcc_connect");
		exit(1);
	}

	while(1) {
		printf("SEND (q or Q to quit) : ");
		if (fgets(req.message, 256, stdin) == NULL) {
			continue;
		}
		newline = strrchr(req.message, '\n');
		if (newline) {
			*newline = '\0';
		}

		if (strcasecmp(req.message, "q") == 0) {
			break;
		} else {
			req.hdr.id = QB_IPC_MSG_USER_START + 3;
			req.hdr.size = sizeof(struct my_req);
			rc = qb_ipcc_send(conn, &req, req.hdr.size);
			if (rc < 0) {
				perror("qb_ipcc_send");
			}
		}

		if (rc > 0) {
			rc = qb_ipcc_recv(conn,
					   &res,
					   sizeof(res), -1);
			if (rc < 0) {
				perror("qb_ipcc_recv");
			}
			printf("Response[%d]: %s \n", res.hdr.id, res.message);
		}
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}

