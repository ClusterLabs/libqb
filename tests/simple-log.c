/*
 * Copyright (c) 2011 Red Hat, Inc.
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

static void func_one(void) {
	FILE* fd;
	qb_log(LOG_DEBUG, "arf arf?");
	qb_log(LOG_CRIT,  "arrrg!");
	qb_log(LOG_ERR,   "oops, I did it again");
	qb_log(LOG_INFO,  "are you aware ...");

	fd = fopen("/nothing.txt", "r+");
	if (fd == NULL) {
		qb_perror(LOG_ERR, "can't open(\"/nothing.txt\")");
	}
}

static void func_two(void) {
	qb_log(LOG_DEBUG, "arf arf?");
	qb_log(LOG_CRIT,  "arrrg!");
	qb_log(LOG_ERR,   "oops, I did it again");
	qb_log(LOG_INFO,  "are you aware ...");
}

static void show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -v             verbose\n");
	printf("  -t             threaded logging\n");
	printf("  -e             log to stderr\n");
	printf("  -b             log to blackbox\n");
	printf("  -f <filename>  log to a file\n");
	printf("  -h             show this help text\n");
	printf("\n");
}

static int32_t do_blackbox = QB_FALSE;
static int32_t do_threaded = QB_FALSE;

static void sigsegv_handler(int sig)
{
	(void)signal (SIGSEGV, SIG_DFL);
	if (do_threaded) {
		qb_log_thread_stop();
	}
	if (do_blackbox) {
		qb_log_blackbox_write_to_file("simple-log.fdata");
	}
	raise(SIGSEGV);
}

int32_t main(int32_t argc, char *argv[])
{
	const char *options = "vhtebdf:";
	int32_t opt;
	int32_t priority = LOG_WARNING;
	int32_t do_stderr = QB_FALSE;
	int32_t do_dump_blackbox = QB_FALSE;
	char *logfile = NULL;
	int32_t log_fd = -1;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'd':
			do_dump_blackbox = QB_TRUE;
			break;
		case 't':
			do_threaded = QB_TRUE;
			break;
		case 'e':
			do_stderr = QB_TRUE;
			break;
		case 'b':
			do_blackbox = QB_TRUE;
			break;
		case 'f':
			logfile = optarg;
			break;
		case 'v':
			priority++;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}

	if (do_dump_blackbox) {
		qb_log_blackbox_print_from_file("simple-log.fdata");
		exit(0);
	}

	signal(SIGSEGV, sigsegv_handler);

	qb_log_init("simple-log", LOG_USER, LOG_INFO);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_THREADED, do_threaded);

	if (do_stderr) {
		qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
				  QB_LOG_FILTER_FUNCTION, "func_one", LOG_DEBUG);
		qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
				  QB_LOG_FILTER_FILE, __FILE__, priority);
		qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	}
	if (do_blackbox) {
		qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
				  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
		qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 4096);
		qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_THREADED, QB_FALSE);
		qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
	}
	if (logfile) {
		log_fd = qb_log_file_open(logfile);
		qb_log_filter_ctl(log_fd, QB_LOG_FILTER_ADD,
				  QB_LOG_FILTER_FILE, __FILE__, priority);
		qb_log_ctl(log_fd, QB_LOG_CONF_THREADED, do_threaded);
	}
	if (do_threaded) {
		qb_log_thread_start();
	}
	qb_log(LOG_DEBUG, "hello");
	qb_log(LOG_INFO, "hello");
	qb_log(LOG_NOTICE, "hello");
	func_one();
	func_two();

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	qb_log(LOG_WARNING, "no syslog");
	qb_log(LOG_ERR, "no syslog");

#if 0
	// test blackbox
	logfile = NULL;
	logfile[5] = 'a';
#endif
	if (do_threaded) {
		qb_log_thread_stop();
	}
	return 0;
}

