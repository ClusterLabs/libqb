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
#include <qb/qblog.h>

static int alarm_notice = 0;
static qb_ringbuffer_t *rb = NULL;
#define ONE_MEG 1048576
static char buffer[ONE_MEG * 3];

#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)

static void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

static void sigterm_handler(int32_t num)
{
	qb_log(LOG_INFO, "writer: %s(%d)\n", __func__, num);
	qb_rb_close(rb);
	exit(0);
}

static void
_benchmark(int write_size)
{
	struct timeval tv1, tv2, tv_elapsed;
	int res;
	int write_count = 0;
	char *dest;

	alarm_notice = 0;
	alarm (10);
	gettimeofday (&tv1, NULL);
	do {
		dest = qb_rb_chunk_alloc(rb, write_size);
		//usleep(10000);

		res = -EAGAIN;
		if (dest) {
			memcpy(dest, buffer, write_size);
			clock_gettime(CLOCK_REALTIME, (struct timespec*)dest);
			res = qb_rb_chunk_commit(rb, write_size);
			//printf("res %d\n", res);
			if (res == 0) {
				write_count++;
			}
		}
	} while (alarm_notice == 0 && (res == 0 || res == -EAGAIN));
	write_count -= qb_rb_chunks_used(rb);
	if (res < 0) {
		perror("qb_ipcc_sendv");
	}
	gettimeofday(&tv2, NULL);
	timersub(&tv2, &tv1, &tv_elapsed);

	printf("%5d messages sent ", write_count);
	printf("%5d bytes per write ", write_size);
	printf("%7.3f Seconds runtime ",
		(tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf("%9.3f TP/s ",
		((float)write_count) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
	printf("%7.3f MB/s.\n",
		((float)write_count) * ((float)write_size) /  ((tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)) * 1000000.0));
}

static void
do_throughput_benchmark(void)
{
	ssize_t size = 64;
	int i;

	signal (SIGALRM, sigalrm_handler);

	for (i = 0; i < 10; i++) {
		_benchmark(size);
		signal (SIGALRM, sigalrm_handler);
		size *= 5;
		if (size >= ONE_MEG) {
			break;
		}
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
	int32_t verbose = 0;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
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

	qb_log_init("rbwriter", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO + verbose);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	rb = qb_rb_open("tester", ONE_MEG * 3,
			QB_RB_FLAG_SHARED_PROCESS | 
			QB_RB_FLAG_NO_SEMAPHORE, 0);
	do_throughput_benchmark();
	qb_rb_close(rb);
	return EXIT_SUCCESS;
}
