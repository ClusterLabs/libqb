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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include <qb/qbrb.h>
#include <qb/qbdefs.h>
#include <qb/qbutil.h>

static qb_ringbuffer_t *rb = NULL;
#define ITERATIONS 100000
#define BUFFER_CHUNK_SIZE (50*50*10)

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

static void sigterm_handler(int32_t num)
{
	printf("writer: %s(%d)\n", __func__, num);
	qb_rb_close(rb);
	exit(0);
}

static void libqb_log_writer(const char *file_name,
			     int32_t file_line,
			     int32_t severity, const char *msg)
{
	printf("libqb:writer: %s:%d %s\n", file_name, file_line, msg);
}

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

	printf("write size %d OPs/sec %9.3f ", size, ops_per_sec);
	printf("MB/sec %9.3f\n", mbs_per_sec);
}

static void bmc_connect(void)
{
	rb = qb_rb_open("tester", BUFFER_CHUNK_SIZE * 3,
			QB_RB_FLAG_SHARED_PROCESS, 0);

	if (rb == NULL) {
		printf("writer: failed to create ringbuffer\n");
		exit(1);
	}

}

static char buffer[1024 * 1024];
static void bmc_send_nozc(size_t size)
{
	ssize_t res = 0;

repeat_send:
	res = qb_rb_chunk_write(rb, buffer, size);
	if (res < size) {
		//usleep(1);
		goto repeat_send;
	}
}

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

int32_t main(int32_t argc, char *argv[])
{
	const char *options = "vh";
	int32_t opt;
	int32_t i, j;
	int32_t size;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'v':
			qb_util_set_log_function(libqb_log_writer);
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}

	signal(SIGINT, sigterm_handler);

	bmc_connect();

	for (j = 1; j < 49; j++) {
		bm_start();
		size = 7 * (j + 1) * j;

		if (size > BUFFER_CHUNK_SIZE) {
			size = BUFFER_CHUNK_SIZE;
		}

		for (i = 0; i < ITERATIONS; i++) {
			bmc_send_nozc(size);
		}
		bm_finish("ringbuffer", size);
	}
	qb_rb_close(rb);
	return EXIT_SUCCESS;
}
