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
#include <qb/qbutil.h>
#include <qb/qblog.h>

#define ITERATIONS 50000
static qb_util_stopwatch_t *sw;

extern void log_dict_words(void);

static void
bm_finish (const char *operation)
{
	qb_util_stopwatch_stop(sw);

	if (strlen (operation) > 22) {
		printf ("%s\t\t", operation);
	} else {
		printf ("%s\t\t\t", operation);
	}
	printf("%9.3f operations/sec\n",
	       ((float)ITERATIONS) /  qb_util_stopwatch_sec_elapsed_get(sw));
}

int
main(void)
{
	int i;

	sw =  qb_util_stopwatch_create();
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
	qb_util_stopwatch_start(sw);
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "RecordA");
	}
	bm_finish ("qb_log 1 arguments:");
	qb_util_stopwatch_start(sw);
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "%s%s", "RecordA", "RecordB");
	}
	bm_finish ("qb_log 2 args(str):");
	qb_util_stopwatch_start(sw);
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "%s%s%s", "RecordA", "RecordB", "RecordC");
	}
	bm_finish ("qb_log 3 args(str):");
	qb_util_stopwatch_start(sw);
	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_DEBUG, "%i %u %p", -534, 4508, &i);
	}
	bm_finish ("qb_log 3 args(int):");
#if defined(HAVE_DICT_WORDS) && defined(HAVE_SLOW_TESTS)
	qb_util_stopwatch_start(sw);
	log_dict_words();
	bm_finish ("qb_log /usr/share/dict/words:");
#endif /* HAVE_DICT_WORDS */

	/* this will close the ringbuffer
	 */
	qb_log_fini();

	return 0;
}
