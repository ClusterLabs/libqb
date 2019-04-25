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
static qb_util_stopwatch_t *sw;
#define ONE_MEG 1048576
static char buffer[ONE_MEG * 3];

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
_benchmark(ssize_t write_size)
{
	ssize_t res;
	int write_count = 0;
	float secs;

	alarm_notice = 0;

	alarm (10);

	qb_util_stopwatch_start(sw);
	do {
		res = qb_rb_chunk_write(rb, buffer, write_size);
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
	printf ("%5ld bytes per write ", (long int) write_size);
	printf ("%7.3f Seconds runtime ", secs);
	printf ("%9.3f TP/s ",
		((float)write_count) / secs);
	printf ("%7.3f MB/s.\n",
		((float)write_count) * ((float)write_size) / secs);
}


static void
do_throughput_benchmark(void)
{
	ssize_t size = 64;
	int i;

	signal (SIGALRM, sigalrm_handler);
	sw =  qb_util_stopwatch_create();

	for (i = 0; i < 10; i++) { /* number of repetitions - up to 50k */
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
			QB_RB_FLAG_SHARED_PROCESS, 0);
	do_throughput_benchmark();
	qb_rb_close(rb);
	qb_log_fini();
	return EXIT_SUCCESS;
}
