/*
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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

#include <qb/qblog.h>
#include <qb/qbutil.h>
#include <qb/qbipcc.h>

#define ITERATIONS 10000
pid_t mypid;
int32_t blocking = QB_TRUE;
int32_t events = QB_FALSE;
int32_t verbose = 0;
static qb_ipcc_connection_t *conn;
#define MAX_MSG_SIZE (8192*128)
static qb_util_stopwatch_t *sw;

static void bm_finish(const char *operation, int32_t size)
{
	float ops_per_sec;
	float mbs_per_sec;
	float elapsed;

	qb_util_stopwatch_stop(sw);
	elapsed = qb_util_stopwatch_sec_elapsed_get(sw);
	ops_per_sec = ((float)ITERATIONS) / elapsed;
	mbs_per_sec = ((((float)ITERATIONS) * size) / elapsed) / (1024.0 * 1024.0);

	qb_log(LOG_INFO, "write size, %d, OPs/sec, %9.3f, MB/sec, %9.3f",
	       size, ops_per_sec, mbs_per_sec);
}

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[1024 * 1024];
};

static struct my_req request;

static int32_t bmc_send_nozc(uint32_t size)
{
	struct qb_ipc_response_header res_header;
	int32_t res;

	request.hdr.id = QB_IPC_MSG_USER_START + 3;
	request.hdr.size = sizeof(struct qb_ipc_request_header) + size;

repeat_send:
	res = qb_ipcc_send(conn, &request, request.hdr.size);
	if (res < 0) {
		if (res == -EAGAIN) {
			goto repeat_send;
		} else if (res == -EINVAL || res == -EINTR || res == -ENOTCONN) {
			qb_perror(LOG_ERR, "qb_ipcc_send");
			return -1;
		} else {
			errno = -res;
			qb_perror(LOG_ERR, "qb_ipcc_send");
			goto repeat_send;
		}
	}

	if (blocking) {
		res = qb_ipcc_recv(conn,
				&res_header,
				sizeof(struct qb_ipc_response_header), -1);
		if (res == -EINTR) {
			return -1;
		}
		if (res < 0) {
			qb_perror(LOG_ERR, "qb_ipcc_recv");
		}
		assert(res == sizeof(struct qb_ipc_response_header));
		assert(res_header.id == 13);
		assert(res_header.size == sizeof(struct qb_ipc_response_header));
	}
	if (events) {
		res = qb_ipcc_event_recv(conn,
				&res_header,
				sizeof(struct qb_ipc_response_header), -1);
		if (res == -EINTR) {
			return -1;
		}
		if (res < 0) {
			qb_perror(LOG_ERR, "qb_ipcc_event_recv");
		}
		assert(res == sizeof(struct qb_ipc_response_header));
		assert(res_header.id == 13);
		assert(res_header.size == sizeof(struct qb_ipc_response_header));
	}
	return 0;
}

struct qb_ipc_request_header *global_zcb_buffer;

static void show_usage(const char *name)
{
	qb_log(LOG_INFO, "usage: \n");
	qb_log(LOG_INFO, "%s <options>\n", name);
	qb_log(LOG_INFO, "\n");
	qb_log(LOG_INFO, "  options:\n");
	qb_log(LOG_INFO, "\n");
	qb_log(LOG_INFO, "  -n             non-blocking ipc (default blocking)\n");
	qb_log(LOG_INFO, "  -e             receive events\n");
	qb_log(LOG_INFO, "  -v             verbose\n");
	qb_log(LOG_INFO, "  -h             show this help text\n");
	qb_log(LOG_INFO, "\n");
}

static void sigterm_handler(int32_t num)
{
	qb_log(LOG_INFO, "bmc: %s(%d)\n", __func__, num);
	qb_ipcc_disconnect(conn);
	exit(0);
}

int32_t
main(int32_t argc, char *argv[])
{
	const char *options = "nevh";
	int32_t opt;
	int32_t i, j;
	size_t size;

	mypid = getpid();

	qb_log_init("bmc", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'n':
			blocking = QB_FALSE;
			break;
		case 'e':
			events = QB_TRUE;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}

	signal(SIGINT, sigterm_handler);
	signal(SIGILL, sigterm_handler);
	signal(SIGTERM, sigterm_handler);
	conn = qb_ipcc_connect("bm1", MAX_MSG_SIZE);
	if (conn == NULL) {
		qb_perror(LOG_ERR, "qb_ipcc_connect");
		exit(1);
	}

	sw =  qb_util_stopwatch_create();
	size = QB_MAX(sizeof(struct qb_ipc_request_header), 64);
	for (j = 0; j < 20; j++) {
		if (size >= MAX_MSG_SIZE)
			break;
		qb_util_stopwatch_start(sw);
		for (i = 0; i < ITERATIONS; i++) {
			if (bmc_send_nozc(size) == -1) {
				break;
			}
		}
		bm_finish("send_nozc", size);
		size *= 2;
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}

