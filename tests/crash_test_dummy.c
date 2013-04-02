/*
 * Copyright (c) 2013 Red Hat, Inc.
 *
 * All rights reserved.
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
#include "os_base.h"
#include <signal.h>
#include <syslog.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>


static void
func_one(void)
{
	FILE *fd;

	qb_enter();
	qb_log(LOG_DEBUG, "arf arf?");
	qb_log(LOG_CRIT, "arrrg!");
	qb_log(134, "big priority");
	qb_log(LOG_ERR, "oops, I did it again");
	qb_log(LOG_INFO, "are you aware ...");

	fd = fopen("/nothing.txt", "r+");
	if (fd == NULL) {
		qb_perror(LOG_ERR, "can't open(\"/nothing.txt\")");
	} else {
		fclose(fd);
	}
	qb_leave();
}

static void
func_two(void)
{
	qb_enter();
	qb_logt(LOG_DEBUG, 0, "arf arf?");
	qb_log(LOG_CRIT, "arrrg!");
	qb_log(LOG_ERR, "oops, I did it again");
	qb_log(LOG_INFO, "are you aware ...");
	qb_leave();
}


static void
sigsegv_handler(int sig)
{
	(void)signal(SIGSEGV, SIG_DFL);
	qb_log_blackbox_write_to_file("crash-test-dummy.fdata");
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_FALSE);
	raise(SIGSEGV);
}


int32_t
main(int32_t argc, char *argv[])
{
	char *logfile;
	int i;

	signal(SIGSEGV, sigsegv_handler);

	qb_log_init("crash-test-dummy", LOG_USER, LOG_INFO);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 4096);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_THREADED, QB_FALSE);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);

	for (i = 0; i < 1000; i++) {
		qb_log(LOG_DEBUG, "hello");
		qb_log(LOG_INFO, "this is an info");
		qb_log(LOG_NOTICE, "hello - notice?");
		{
			char * str = NULL;
			qb_log(LOG_ERR,
			       "%s-%d-%s-%u",
			       NULL, 952, str, 56);
		}
		func_one();
		func_two();
	}

	/* on purpose crash to make a blackbox.
	 */
       	logfile = NULL;
	logfile[5] = 'a';
	return 0;
}
