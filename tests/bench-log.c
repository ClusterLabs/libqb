/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake <sdake@redhat.com>
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

#include <qb/qbdefs.h>
#include <qb/qblog.h>

#define ITERATIONS 50000

extern void log_dict_words(void);

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
		qb_log(LOG_DEBUG, "hello");
	}
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "RecordA");
	}
	bm_finish ("qb_log 1 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "%s%s", "RecordA", "RecordB");
	}
	bm_finish ("qb_log 2 arguments:");
	bm_start();
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "%s%s%s", "RecordA", "RecordB", "RecordC");
	}
	bm_finish ("qb_log 3 arguments:");
#ifdef HAVE_DICT_WORDS
	bm_start();
	log_dict_words();
	bm_finish ("qb_log /usr/share/dict/words:");
#endif /* HAVE_DICT_WORDS */

	/* this will close the ringbuffer
	 */
	qb_log_fini();

	return 0;
}
