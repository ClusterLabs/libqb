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
	qb_log(LOG_DEBUG, "hello");
	qb_log(LOG_CRIT, "hello");
	qb_log(LOG_ERR, "hello");
	qb_log(LOG_INFO, "hello");
}

static void func_two(void) {
	qb_log(LOG_DEBUG, "arrrg!");
	qb_log(LOG_CRIT,  "arrrg!");
	qb_log(LOG_ERR,   "arrrg!");
	qb_log(LOG_INFO,  "arrrg!");
}

static void show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -v             verbose\n");
	printf("  -h             show this help text\n");
	printf("\n");
}

#define MY_STDOUT_TAG 1
#define MY_LOG_TAG 2

static void my_log_writer(struct qb_log_callsite *cs, const char *msg)
{
	if (qb_bit_is_set(cs->tags, MY_STDOUT_TAG)) {
		fprintf(stdout, "[%s:%d] <%d> %s\n", cs->filename, cs->lineno, cs->priority, msg);
	}
	if (qb_bit_is_set(cs->tags, MY_LOG_TAG)) {
		syslog(cs->priority, "%s", msg);
	}
}

int32_t main(int32_t argc, char *argv[])
{
	const char *options = "vh";
	int32_t opt;
	uint8_t priority = LOG_NOTICE;
	qb_log_filter_t* ls1;
	qb_log_filter_t* ls2;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
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

	qb_log_handler_set(my_log_writer);

	/* stdout logging */
	ls1 = qb_log_filter_create();
	qb_log_filter_priority_set(ls1, priority);
	qb_log_filter_file_add(ls1, __FILE__, 1, 33); // line 1 to 33
	qb_log_filter_file_add(ls1, __FILE__, 75, INT32_MAX); // line 75 to EOF
	qb_log_tag(ls1, QB_TRUE, MY_STDOUT_TAG);

	/* syslog */
	ls2 = qb_log_filter_create();
	qb_log_filter_priority_set(ls2, LOG_WARNING);
	qb_log_tag(ls2, QB_TRUE, MY_LOG_TAG);

	qb_log(LOG_DEBUG, "hello");
	qb_log(LOG_INFO, "hello");
	qb_log(LOG_NOTICE, "hello");
	func_one();
	func_two();
	qb_log_tag(ls2, QB_FALSE, MY_LOG_TAG);
	qb_log(LOG_WARNING, "no syslog");
	qb_log(LOG_ERR, "no syslog");

	qb_log_filter_destroy(ls1);
	qb_log_filter_destroy(ls2);

	return 0;
}

