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

static void my_log_writer(struct qb_log_callsite *cs,
			  const char *timestamp_str,
			  const char *msg)
{
	qb_log_blackbox_append(cs, timestamp_str, msg);
}

int32_t main(int32_t argc, char *argv[])
{
	qb_log_handler_set(my_log_writer);
	qb_log_blackbox_start(4096);

	qb_log(LOG_DEBUG, "hello");
	qb_log(LOG_INFO, "hello");
	qb_log(LOG_NOTICE, "hello");
	func_one();
	func_two();
	qb_log(LOG_WARNING, "no syslog");
	qb_log(LOG_ERR, "no syslog");
	func_two();

	qb_log_blackbox_write_to_file("bb");
	qb_log_blackbox_print_from_file("bb");

	return 0;
}

