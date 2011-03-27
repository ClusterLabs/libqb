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
#include <stdarg.h>
#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include "log_int.h"

struct qb_log_destination *destination;

#define TIME_STRING_SIZE 128

/* deprecated method of getting internal log messages */
static qb_util_log_fn_t old_internal_log_fn = NULL;
void qb_util_set_log_function(qb_util_log_fn_t fn)
{
	old_internal_log_fn = fn;
}

static const char log_month_name[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void _log_timestamp(char * char_time)
{
	struct timeval tv;
	struct tm tm_res;

	gettimeofday (&tv, NULL);
	(void)localtime_r ((time_t *)&tv.tv_sec, &tm_res);
	snprintf (char_time, TIME_STRING_SIZE, "%s %02d %02d:%02d:%02d",
		log_month_name[tm_res.tm_mon], tm_res.tm_mday, tm_res.tm_hour,
		tm_res.tm_min, tm_res.tm_sec);
}

void qb_log_real_(struct qb_log_callsite *cs,
		  int32_t error_number, ...)
{
	va_list ap;
	char buf[COMBINE_BUFFER_SIZE];
	size_t len;
	static int32_t in_logger = 0;
	char char_time[TIME_STRING_SIZE];

	if (destination == NULL || destination->logger == NULL) {
		return;
	}
	if (in_logger) {
		return;
	}
	in_logger = QB_TRUE;;

	va_start(ap, error_number);
	len = vsnprintf(buf, COMBINE_BUFFER_SIZE, cs->format, ap);
	va_end(ap);

	if (buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len -= 1;
	}

	_log_timestamp(char_time);

	if (old_internal_log_fn) {
		if (qb_bit_is_set(cs->tags, 31)) {
			old_internal_log_fn(cs->filename, cs->lineno, cs->priority, buf);
		}
	}
	if (destination->threaded) {
		qb_log_thread_log_post(cs, char_time, buf);
	} else {
		destination->logger(cs, char_time, buf);
	}
	in_logger = QB_FALSE;
}

qb_log_filter_t* qb_log_filter_create(void)
{
	struct qb_log_filter *flt = calloc(1, sizeof(struct qb_log_filter));
	qb_list_init(&flt->files_head);
	flt->priority = LOG_INFO;
	return flt;
}

void qb_log_filter_destroy(struct qb_log_filter* flt)
{
	struct qb_log_filter_file *ff;
	struct qb_list_head *item;
	struct qb_list_head *next;

	qb_list_for_each_safe(item, next, &flt->files_head) {
		ff = qb_list_entry(item, struct qb_log_filter_file, list);
		qb_list_del(item);
		free(ff);
	}

	free(flt);
}

int32_t qb_log_filter_priority_set(struct qb_log_filter* flt, uint8_t priority)
{
	flt->priority = priority;
	return 0;
}

void qb_log_filter_file_add(struct qb_log_filter* flt,
			    const char* filename,
			    int32_t start, int32_t end)
{
	struct qb_log_filter_file *ff = calloc(1, sizeof(struct qb_log_filter_file));
	qb_list_init(&ff->list);
	ff->filename = (char*)filename;
	ff->start = start;
	ff->end = end;
	qb_list_add(&ff->list, &flt->files_head);
}

static void _set_bit_in_tags(struct qb_log_filter* flt, int32_t is_set, int32_t tag_bit)

{
	struct qb_log_callsite *cs;
	struct qb_log_filter_file *ff;
	int32_t match;

	for (cs = __start___verbose; cs < __stop___verbose; cs++) {
		if (cs->priority > flt->priority)
			continue;
		match = QB_FALSE;
		qb_list_for_each_entry(ff, &flt->files_head, list) {
			if (ff->start <= cs->lineno &&
			    ff->end >= cs->lineno &&
			    strcmp(ff->filename, cs->filename) == 0) {
				match = QB_TRUE;
			}
		}
		if (match || qb_list_empty(&flt->files_head)) {
			if (is_set) {
				qb_bit_set(cs->tags, tag_bit);
			} else {
				qb_bit_clear(cs->tags, tag_bit);
			}
#if 0
			printf("matched: %-12s %20s:%u fmt:<%d>%s\n",
				cs->function, cs->filename, cs->lineno,
				cs->priority, cs->format);
#endif
		}
	}

}

void qb_log_tag(struct qb_log_filter* flt, int32_t tag_bit)
{
	_set_bit_in_tags(flt, QB_TRUE, tag_bit);
}

void qb_log_untag(struct qb_log_filter* flt, int32_t tag_bit)
{
	_set_bit_in_tags(flt, QB_FALSE, tag_bit);
}

void qb_log_handler_set(qb_log_logger_fn logger_fn)
{
	if (destination == NULL) {
		destination = calloc(1, sizeof(struct qb_log_destination));
	}

	destination->logger = NULL;
	qb_log(LOG_DEBUG, " ");
	destination->logger = logger_fn;
	destination->threaded = QB_FALSE;
}

