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

#include <sys/types.h>
#include <sys/socket.h>
#include <qb/qbipcc.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

int blocking = 1;
int verbose = 0;
#define ITERATIONS 10000

static struct timeval tv1, tv2, tv_elapsed;

#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)

FILE *mbs_fp;

FILE *ops_fp;

static void bm_start(void)
{
	gettimeofday(&tv1, NULL);
}

static void bm_finish(const char *operation, int size)
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

	fprintf(ops_fp, "%d %9.3f\n", size, ops_per_sec);
	fflush(ops_fp);
	fprintf(mbs_fp, "%d %9.3f\n", size, mbs_per_sec);
	fflush(mbs_fp);

	printf("write size %d OPs/sec %9.3f ", size, ops_per_sec);
	printf("MB/sec %9.3f\n", mbs_per_sec);
}

qb_handle_t bmc_ipc_handle;

static void bmc_connect(void)
{
	uint32_t res;

	res = qb_ipcc_service_connect("qb_ipcs_bm",
				      0,
				      8192 * 128,
				      8192 * 128, 8192 * 128, &bmc_ipc_handle);
}

static char buffer[1024 * 1024];
static void bmc_send_nozc(uint32_t size)
{
	struct iovec iov[2];
	qb_ipc_request_header_t req_header;
	qb_ipc_response_header_t res_header;
	int res;

	req_header.id = 0;
	req_header.size = sizeof(qb_ipc_request_header_t) + size;

	iov[0].iov_base = &req_header;
	iov[0].iov_len = sizeof(qb_ipc_request_header_t);
	iov[1].iov_base = buffer;
	iov[1].iov_len = size;

repeat_send:
	if (blocking) {
		res = qb_ipcc_msg_send_reply_receive(bmc_ipc_handle,
						     iov, 2,
						     &res_header,
						     sizeof
						     (qb_ipc_response_header_t));
	} else {
		res = qb_ipcc_msg_send(bmc_ipc_handle, iov, 2);
	}
	if (res == -1) {
		if (errno == ENOMEM) {
			goto repeat_send;
		} else {
			printf("qb_ipcc_msg_send: %d(%s)\n", res, strerror(res));
			goto repeat_send;
		}
	}
}

qb_ipc_request_header_t *global_zcb_buffer;

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

static void sigterm_handler(int num)
{
	printf("writer: %s(%d)\n", __func__, num);
	qb_ipcc_service_disconnect(bmc_ipc_handle);
	exit(0);
}

static void libqb_log_writer(const char *file_name,
			     int32_t file_line,
			     int32_t severity, const char *msg)
{
	printf("libqb: %s:%d %s\n", file_name, file_line, msg);
}

int main(int argc, char *argv[])
{
	const char *options = "nvh";
	int opt;
	int i, j;
	size_t size;

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
	bmc_connect();

	ops_fp = fopen("opsec", "w");
	mbs_fp = fopen("mbsec", "w");

	for (j = 1; j < 49; j++) {
		size = 10 * j * j;
		bm_start();
		for (i = 0; i < ITERATIONS; i++) {
			bmc_send_nozc(size);
		}
		bm_finish("send_nozc", size);
	}
	qb_ipcc_service_disconnect(bmc_ipc_handle);
	return EXIT_SUCCESS;
}
