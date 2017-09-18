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

static int32_t do_benchmark = QB_FALSE;
static int32_t use_events = QB_FALSE;
static int alarm_notice;
static qb_util_stopwatch_t *sw;
#define ONE_MEG 1048576
static char *data;

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
	struct iovec iov[2];
	ssize_t res;
	struct qb_ipc_request_header hdr;
	int write_count = 0;
	float secs;

	alarm_notice = 0;
	hdr.size = write_size;
	hdr.id = QB_IPC_MSG_USER_START + 1;

	iov[0].iov_base = &hdr;
	iov[0].iov_len = sizeof(struct qb_ipc_request_header);

	iov[1].iov_base = data;
	iov[1].iov_len = write_size - sizeof(struct qb_ipc_request_header);

	alarm (10);

	qb_util_stopwatch_start(sw);
	do {
		res = qb_ipcc_sendv(conn, iov, 2);
		if (res == write_size) {
			write_count++;
		}
	} while (alarm_notice == 0 && (res == write_size || res == -EAGAIN));
	if (res < 0) {
		perror("qb_ipcc_sendv");
	}
	qb_util_stopwatch_stop(sw);
	secs = qb_util_stopwatch_sec_elapsed_get(sw);

	printf ("%5d messages sent ", write_count);
	printf ("%5d bytes per write ", write_size);
	printf ("%7.3f Seconds runtime ", secs);
	printf ("%9.3f TP/s ",
		((float)write_count) / secs);
	printf ("%7.3f MB/s.\n",
		((float)write_count) * ((float)write_size) / secs);
}


static void
do_throughput_benchmark(qb_ipcc_connection_t *conn)
{
	ssize_t size = 64;
	int i;

	signal (SIGALRM, sigalrm_handler);
	sw =  qb_util_stopwatch_create();

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
	int32_t send_ten_events;

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

		send_ten_events = (strcasecmp(req.message, "events") == 0);

		if (rc > 0) {
			if (use_events && !send_ten_events) {
				printf("waiting for event recv\n");
				rc = qb_ipcc_event_recv(conn, &res, sizeof(res), -1);
			} else {
				printf("waiting for recv\n");
				rc = qb_ipcc_recv(conn, &res, sizeof(res), -1);
			}
			printf("recv %d\n", rc);
			if (rc < 0) {
				perror("qb_ipcc_recv");
				exit(0);
			}
			if (send_ten_events) {
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

static void
show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -h             show this help text\n");
	printf("  -b             benchmark\n");
	printf("  -e             use events instead of responses\n");
	printf("\n");
}

int
main(int argc, char *argv[])
{
	qb_ipcc_connection_t *conn;
	const char *options = "ebh";
	int32_t opt;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'b':
			do_benchmark = QB_TRUE;
			break;
		case 'e':
			use_events = QB_TRUE;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}


	qb_log_init("ipcclient", LOG_USER, LOG_TRACE);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_format_set(QB_LOG_STDERR, "%f:%l [%p] %b");
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	/* Our example server is enforcing a buffer size minimum,
	 * so the client does not need to be concerned with setting
	 * the buffer size */
	conn = qb_ipcc_connect("ipcserver", 0);
	if (conn == NULL) {
		perror("qb_ipcc_connect");
		exit(1);
	}
	data = calloc(1, qb_ipcc_get_buffer_size(conn));

	if (do_benchmark) {
		do_throughput_benchmark(conn);
	} else {
		do_echo(conn);
	}

	qb_ipcc_disconnect(conn);
	free(data);
	qb_log_fini();
	return EXIT_SUCCESS;
}
