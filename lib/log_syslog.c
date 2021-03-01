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

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif /* HAVE_SYSLOG_H */
#ifdef USE_JOURNAL
#define SD_JOURNAL_SUPPRESS_LOCATION
#include <systemd/sd-journal.h>
#endif

#include "log_int.h"

static void
_syslog_logger(int32_t target,
	       struct qb_log_callsite *cs, struct timespec *timestamp, const char *msg)
{
	char buffer[QB_LOG_MAX_LEN];
	char *output_buffer = buffer;
	struct qb_log_target *t = qb_log_target_get(target);
	int32_t final_priority = cs->priority;

	if (final_priority > LOG_INFO) {
		/*
		 * only bump the priority if it is greater than info.
		 */
		final_priority += t->priority_bump;
	}
	if (final_priority > LOG_DEBUG) {
		return;
	}

	if (t->max_line_length > QB_LOG_MAX_LEN) {
		output_buffer = malloc(t->max_line_length);
		if (!output_buffer) {
			return;
		}
	}

	output_buffer[0] = '\0';
	qb_log_target_format(target, cs, timestamp, msg, output_buffer);

	if (final_priority < LOG_EMERG) {
		final_priority = LOG_EMERG;
	}
#ifdef USE_JOURNAL
	if (t->use_journal) {
		if (cs->message_id) {
			sd_journal_send("MESSAGE_ID=%s", cs->message_id,
				"PRIORITY=%d", final_priority,
				"CODE_LINE=%d", cs->lineno,
				"CODE_FILE=%s", cs->filename,
				"CODE_FUNC=%s", cs->function,
				"SYSLOG_IDENTIFIER=%s", t->name,
				"MESSAGE=%s", output_buffer,
				NULL);
		} else {
			sd_journal_send("PRIORITY=%d", final_priority,
				"CODE_LINE=%d", cs->lineno,
				"CODE_FILE=%s", cs->filename,
				"CODE_FUNC=%s", cs->function,
				"SYSLOG_IDENTIFIER=%s", t->name,
				"MESSAGE=%s", output_buffer,
				NULL);
		}
	} else {
#endif
		syslog(final_priority, "%s", output_buffer);
#ifdef USE_JOURNAL
	}
#endif
	if (t->max_line_length > QB_LOG_MAX_LEN) {
		free(output_buffer);
	}
}

static void
_syslog_close(int32_t target)
{
	struct qb_log_target *t = qb_log_target_get(target);

	if (!t->use_journal) {
		closelog();
	}
}

static void
_syslog_reload(int32_t target)
{
	struct qb_log_target *t = qb_log_target_get(target);

	closelog();
	if (!t->use_journal) {
		openlog(t->name, LOG_PID, t->facility);
	}
}

int32_t
qb_log_syslog_open(struct qb_log_target *t)
{
	t->logger = _syslog_logger;
	t->reload = _syslog_reload;
	t->close = _syslog_close;

	if (!t->use_journal) {
		openlog(t->name, LOG_PID, t->facility);
	}
	return 0;
}
