/*
 * Copyright (C) 2011 Red Hat, Inc.
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
#include <syslog.h>
#include <qb/qbrb.h>
#include "log_int.h"

static void _syslog_logger(struct qb_log_target *t,
			   struct qb_log_callsite *cs,
			   time_t timestamp,
			   const char *msg)
{
	char output_buffer[COMBINE_BUFFER_SIZE];

	qb_log_target_format(t, cs, timestamp, msg, output_buffer);

	syslog(cs->priority, "%s", output_buffer);
}

static void _syslog_close(struct qb_log_target *t)
{
	closelog();
}

static void _syslog_reload(struct qb_log_target *t)
{
	closelog();
	openlog(t->name, LOG_PID, t->facility);
}

int32_t qb_log_syslog_open(struct qb_log_target *t)
{
	t->logger = _syslog_logger;
	t->reload = _syslog_reload;
	t->close = _syslog_close;

	openlog(t->name, LOG_PID, t->facility);
	return 0;
}


