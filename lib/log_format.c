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
#include <ctype.h>

#include <qb/qbdefs.h>
#include "log_int.h"

static qb_log_tags_stringify_fn _user_tags_stringify_fn;

/*
 * syslog prioritynames, facility names to value mapping
 * Some C libraries build this in to their headers, but it is non-portable
 * so logsys supplies its own version.
 */
struct syslog_names {
	const char *c_name;
	int32_t c_val;
};

static struct syslog_names prioritynames[] = {
	{"emerg", LOG_EMERG},
	{"alert", LOG_ALERT},
	{"crit", LOG_CRIT},
	{"error", LOG_ERR},
	{"warning", LOG_WARNING},
	{"notice", LOG_NOTICE},
	{"info", LOG_INFO},
	{"debug", LOG_DEBUG},
	{"trace", LOG_TRACE},
	{NULL, -1}
};

static const char log_month_name[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void
qb_log_tags_stringify_fn_set(qb_log_tags_stringify_fn fn)
{
	_user_tags_stringify_fn = fn;
}

static int
_strcpy_cutoff(char *dest, const char *src, size_t cutoff, size_t buf_len)
{
	size_t len = strlen(src);
	if (buf_len <= 1) {
		if (buf_len == 0)
			dest[0] = 0;
		return 0;
	}

	if (cutoff == 0) {
		cutoff = len;
	}

	cutoff = QB_MIN(cutoff, buf_len - 1);
	len = QB_MIN(len, cutoff);
	memcpy(dest, src, len);
	memset(dest + len, ' ', cutoff - len);
	dest[cutoff] = '\0';

	return cutoff;
}

/*
 * %n FUNCTION NAME
 * %f FILENAME
 * %l FILELINE
 * %p PRIORITY
 * %t TIMESTAMP
 * %b BUFFER
 * %g SUBSYSTEM
 *
 * any number between % and character specify field length to pad or chop
 */
void
qb_log_target_format(int32_t target,
		     struct qb_log_callsite *cs,
		     time_t current_time,
		     const char *formatted_message, char *output_buffer)
{
	char char_time[128];
	struct tm tm_res;
	char line_no[30];
	unsigned int format_buffer_idx = 0;
	unsigned int output_buffer_idx = 0;
	size_t cutoff;
	uint32_t len;
	int c;
	struct qb_log_target *t = qb_log_target_get(target);

	while ((c = t->format[format_buffer_idx])) {
		cutoff = 0;
		if (c != '%') {
			output_buffer[output_buffer_idx++] = c;
			format_buffer_idx++;
		} else {
			const char *p;

			format_buffer_idx += 1;
			if (isdigit(t->format[format_buffer_idx])) {
				cutoff = atoi(&t->format[format_buffer_idx]);
			}
			while (isdigit(t->format[format_buffer_idx])) {
				format_buffer_idx += 1;
			}

			switch (t->format[format_buffer_idx]) {
			case 'g':
				if (_user_tags_stringify_fn) {
					p = _user_tags_stringify_fn(cs->tags);
				} else {
					p = "";
				}
				break;

			case 'n':
				p = cs->function;
				break;

			case 'f':
#ifdef BUILDING_IN_PLACE
				p = cs->filename;
#else
				p = strrchr(cs->filename, '/');
				if (p == NULL) {
					p = cs->filename;
				} else {
					p++; /* move past the "/" */
				}
#endif /* BUILDING_IN_PLACE */
				break;

			case 'l':
				snprintf(line_no, 30, "%d", cs->lineno);
				p = line_no;
				break;

			case 't':
				(void)localtime_r(&current_time, &tm_res);
				snprintf(char_time, TIME_STRING_SIZE,
					 "%s %02d %02d:%02d:%02d",
					 log_month_name[tm_res.tm_mon],
					 tm_res.tm_mday, tm_res.tm_hour,
					 tm_res.tm_min, tm_res.tm_sec);
				p = char_time;
				break;

			case 'b':
				p = formatted_message;
				break;

			case 'p':
				if (cs->priority > LOG_TRACE) {
					p = prioritynames[LOG_TRACE].c_name;
				} else {
					p = prioritynames[cs->priority].c_name;
				}
				break;

			default:
				p = "";
				break;
			}
			len = _strcpy_cutoff(output_buffer + output_buffer_idx,
					     p, cutoff,
					     (QB_LOG_MAX_LEN -
					      output_buffer_idx));
			output_buffer_idx += len;
			format_buffer_idx += 1;
		}
		if (output_buffer_idx >= QB_LOG_MAX_LEN - 1) {
			break;
		}
	}

	if (output_buffer[output_buffer_idx - 1] == '\n') {
		output_buffer[output_buffer_idx - 1] = '\0';
	} else {
		output_buffer[output_buffer_idx] = '\0';
	}
}
