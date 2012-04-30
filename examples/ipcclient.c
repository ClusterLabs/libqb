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
#include <qb/qblog.h>

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

	qb_log_init("ipcclient", LOG_USER, LOG_TRACE);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_format_set(QB_LOG_STDERR, "%f:%l [%p] %b");
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	conn = qb_ipcc_connect("ipcserver", MAX_MSG_SIZE);
	if (conn == NULL) {
		perror("qb_ipcc_connect");
		exit(1);
	}

	while (1) {
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
				exit(0);
			}
		}

		if (rc > 0) {
			rc = qb_ipcc_recv(conn, &res, sizeof(res), -1);
			if (rc < 0) {
				perror("qb_ipcc_recv");
				exit(0);
			}
			if (strcasecmp(req.message, "events") == 0) {
				int32_t i;
				printf("waiting for 10 events\n");
				for (i = 0; i < 10; i++) {
					rc = qb_ipcc_event_recv(conn, &res, sizeof(res), -1);
					if (rc < 0) {
						perror("qb_ipcc_event_recv");
					} else {
						printf("got event %d rc:%d\n", i, rc);
					}
				}
			}
			printf("Response[%d]: %s \n", res.hdr.id, res.message);
		}
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}
