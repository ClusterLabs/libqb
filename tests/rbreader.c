/*
 * Copyright (C) 2012 Red Hat, Inc.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbrb.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

#define ONE_MEG 1048576
#define NUM_RX 100000
static qb_ringbuffer_t *rb = NULL;
static int keep_reading = QB_TRUE;
int8_t buffer[ONE_MEG];

#define timersub_ts(a, b, result)                                          \
        do {                                                            \
                (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;           \
                (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;        \
                if ((result)->tv_nsec < 0) {                            \
                        --(result)->tv_sec;                             \
                        (result)->tv_nsec += 1000000000;                \
                }                                                       \
        } while (0)


static void sigterm_handler(int32_t num)
{
	qb_log(LOG_INFO, "signal %d", num);
	keep_reading = QB_FALSE;
}

int32_t
main(int32_t argc, char *argv[])
{
	ssize_t num_read;
	uint32_t num_rx = 0;
	long int acumulated_time = 0;

	signal(SIGINT, sigterm_handler);

	qb_log_init("rbreader", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	rb = qb_rb_open("tester", ONE_MEG * 3,
			QB_RB_FLAG_SHARED_PROCESS |
			QB_RB_FLAG_NO_SEMAPHORE |
			QB_RB_FLAG_CREATE, 0);

	if (rb == NULL) {
		qb_perror(LOG_ERR, "failed to create ringbuffer");
		return -1;
	}
	while (keep_reading) {
		num_read = qb_rb_chunk_read(rb, buffer,
					    ONE_MEG, 0);
		if (num_read == -ETIMEDOUT) {
			//usleep(1);
		} else if (num_read < 0) {
			errno = -num_read;
			qb_perror(LOG_ERR, "nothing to read");
		} else if (num_read > 0) {
			struct timespec *sent = (struct timespec *)buffer;
			struct timespec received;
			struct timespec delta;
			clock_gettime(CLOCK_REALTIME, &received);
			timersub_ts(&received, sent, &delta);

			num_rx++;
			acumulated_time += delta.tv_nsec;
			if (num_rx > NUM_RX) {
				printf("ave latency %ld ns\n", acumulated_time/NUM_RX);
				acumulated_time = 0;
				num_rx = 0;
			}
		}

	}
	qb_rb_close(rb);
	return 0;
}
