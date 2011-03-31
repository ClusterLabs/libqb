/*
 * Copyright (c) 2008, 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <qb/qbdefs.h>
#include <qb/qblog.h>

#define ITERATIONS 10000000

static struct timeval tv1, tv2, tv_elapsed;

#ifndef timersub
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

static void bm_start (void)
{
        gettimeofday (&tv1, NULL);
}
static void bm_finish (const char *operation)
{
        gettimeofday (&tv2, NULL);
        timersub (&tv2, &tv1, &tv_elapsed);

	if (strlen (operation) > 22) {
        	printf ("%s\t\t", operation);
	} else {
        	printf ("%s\t\t\t", operation);
	}
        printf ("%9.3f operations/sec\n",
                ((float)ITERATIONS) /  (tv_elapsed.tv_sec + (tv_elapsed.tv_usec / 1000000.0)));
}

int main (void)
{
	int i;

	qb_log_init("simple-log", LOG_USER, LOG_INFO);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_THREADED, QB_TRUE);

	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
	QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 128000);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_THREADED, QB_FALSE);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);

	printf ("heating up cache with qb_log functionality\n");
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, 0, "hello");
	}
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, 0, "RecordA");
	}
	bm_finish ("qb_log 1 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, 0, "%s%s", "RecordA", "RecordB");
	}
	bm_finish ("qb_log 2 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, 0, "%s%s%s", "RecordA", "RecordB", "RecordC");
	}
	bm_finish ("qb_log 3 arguments:");

	/* this will close the ringbuffer
	 */
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_FALSE);

	return 0;
}
