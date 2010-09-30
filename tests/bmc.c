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
#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <qb/qbipcc.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

#define ITERATIONS 10000
pid_t mypid;
int32_t blocking = 1;
int32_t verbose = 0;
static qb_ipcc_connection_t *conn;
#define MAX_MSG_SIZE (8192*128)

static struct timeval tv1, tv2, tv_elapsed;

#ifndef QB_BSD
#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)
#endif

static void bm_start(void)
{
	gettimeofday(&tv1, NULL);
}

static void bm_finish(const char *operation, int32_t size)
{
	float ops_per_sec;
	float mbs_per_sec;

	gettimeofday(&tv2, NULL);
	timersub(&tv2, &tv1, &tv_elapsed);

	ops_per_sec =
	    ((float)ITERATIONS) / (((float)tv_elapsed.tv_sec) +
				   (((float)tv_elapsed.tv_usec) / 1000000.0));

	mbs_per_sec =
	    ((((float)ITERATIONS) * size) /
	     (((float)tv_elapsed.tv_sec) +
	      (((float)tv_elapsed.tv_usec) / 1000000.0))) / (1024.0 * 1024.0);

	printf("write size, %d, OPs/sec, %9.3f, ", size, ops_per_sec);
	printf("MB/sec, %9.3f\n", mbs_per_sec);
}


static char buffer[1024 * 1024];
static int32_t bmc_send_nozc(uint32_t size)
{
	struct qb_ipc_request_header *req_header = (struct qb_ipc_request_header *)buffer;
	struct qb_ipc_response_header res_header;
	int32_t res;

	req_header->id = QB_IPC_MSG_USER_START + 3;
	req_header->size = sizeof(struct qb_ipc_request_header) + size;

repeat_send:
	res = qb_ipcc_send(conn, req_header, req_header->size);
	if (res < 0) {
		if (res == -EAGAIN || res == -ENOMEM) {
			goto repeat_send;
		} else if (res == -EINVAL || res == -EINTR) {
			perror("qb_ipcc_send");
			return -1;
		} else {
			errno = -res;
			perror("qb_ipcc_send");
			goto repeat_send;
		}
	}

	if (blocking) {
 repeat_recv:
		res = qb_ipcc_recv(conn,
				&res_header,
				sizeof(struct qb_ipc_response_header));
		if (res == -EAGAIN) {
			goto repeat_recv;
		}
		if (res == -EINTR) {
			return -1;
		}
		if (res < 0) {
			perror("qb_ipcc_recv");
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
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -n             non-blocking ipc (default blocking)\n");
	printf("  -v             verbose\n");
	printf("  -h             show this help text\n");
	printf("\n");
}

static void sigterm_handler(int32_t num)
{
	printf("bmc: %s(%d)\n", __func__, num);
	qb_ipcc_disconnect(conn);
	exit(0);
}

static void libqb_log_writer(const char *file_name,
			     int32_t file_line,
			     int32_t severity, const char *msg)
{
	printf("libqb: %s:%d [%d] %s\n", file_name, file_line, severity, msg);
}

int32_t main(int32_t argc, char *argv[])
{
	const char *options = "nvh";
	int32_t opt;
	int32_t i, j;
	size_t size;

	mypid = getpid();

	qb_util_set_log_function(libqb_log_writer);

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'n':	/* non-blocking */
			blocking = 0;
			break;
		case 'v':
			verbose = 1;
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
		perror("qb_ipcc_connect");
		exit(1);
	}

	for (j = 1; j < 49; j++) {
		size = (10 * j * j * j) + sizeof(struct qb_ipc_request_header);
		if (size >= MAX_MSG_SIZE)
			break;
		bm_start();
		for (i = 0; i < ITERATIONS; i++) {
			if (bmc_send_nozc(size) == -1) {
				break;
			}
		}
		bm_finish("send_nozc", size);
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}

