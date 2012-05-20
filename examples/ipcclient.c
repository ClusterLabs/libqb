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

static int do_benchmark = QB_FALSE;

#ifndef timersub
#define timersub(a, b, result)						\
	do {								\
		(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
		(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
		if ((result)->tv_usec < 0) {				\
			--(result)->tv_sec;				\
			(result)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif /* timersub */

static int alarm_notice;
#define ONE_MEG 1048576
#define MAX_MSG_SIZE ONE_MEG
static char data[ONE_MEG];

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[256];
};

struct my_res {
	struct qb_ipc_response_header hdr;
	char message[256];
};

static void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

static void
_benchmark(qb_ipcc_connection_t *conn, int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	struct iovec iov[2];
	unsigned int res;
	struct qb_ipc_request_header hdr;
	int write_count = 0;

	alarm_notice = 0;
	hdr.size = write_size;
	hdr.id = QB_IPC_MSG_USER_START + 1;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(struct qb_ipc_request_header);

	iov[1].iov_base = data;
	iov[1].iov_len = write_size - sizeof(struct qb_ipc_request_header);

	alarm (10);

	gettimeofday (&tv1, NULL);
	do {
		res = qb_ipcc_sendv(conn, iov, 2);
		if (res == write_size) {
			write_count++;
		}
	} while (alarm_notice == 0 && (res == write_size || res == -EAGAIN));
	if (res < 0) {
		perror("qb_ipcc_sendv");
	}
	gettimeofday (&tv2, NULL);
	timersub (&tv2, &tv1, &tv_elapsed);

	printf ("%5d messages sent ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ",
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf ("%7.3f MB/s.\n",
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}


static void
do_throughput_benchmark(qb_ipcc_connection_t *conn)
{
	ssize_t size = 64;
	int i;

	signal (SIGALRM, sigalrm_handler);

	for (i = 0; i < 10; i++) { /* number of repetitions - up to 50k */
		_benchmark (conn, size);
		signal (SIGALRM, sigalrm_handler);
		size *= 5;
		if (size >= (ONE_MEG - 100)) {
			break;
		}
	}
}

static void
do_echo(qb_ipcc_connection_t *conn)
{
	struct my_req req;
	struct my_res res;
	char *newline;
	int32_t rc;

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
}

int
main(int argc, char *argv[])
{
	qb_ipcc_connection_t *conn;
	const char *options = "b";
	int32_t opt;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'b':
			do_benchmark = QB_TRUE;
			break;
		default:
			break;
		}
	}


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

	if (do_benchmark) {
		do_throughput_benchmark(conn);
	} else {
		do_echo(conn);
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}
