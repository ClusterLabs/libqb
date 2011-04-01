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

static struct qb_log_target conf[32];

static QB_LIST_DECLARE(active_targets);

static int32_t _log_target_enable(struct qb_log_target *t);
static void _log_target_disable(struct qb_log_target *t);


/* deprecated method of getting internal log messages */
static qb_util_log_fn_t old_internal_log_fn = NULL;
void qb_util_set_log_function(qb_util_log_fn_t fn)
{
	old_internal_log_fn = fn;
}

static int32_t _cs_matches_filter_(struct qb_log_callsite *cs,
				   enum qb_log_filter_type type,
				   const char * text,
				   uint32_t priority)
{
	int32_t match = QB_FALSE;

	if (cs->priority > priority) {
		return QB_FALSE;
	}
	if (strcmp(text, "*") == 0) {
		return QB_TRUE;
	}
	if (type == QB_LOG_FILTER_FILE) {
		if (strcmp(text, cs->filename) == 0) {
			match = QB_TRUE;
		}
	} else if (type == QB_LOG_FILTER_FUNCTION) {
		if (strcmp(text, cs->function) == 0) {
			match = QB_TRUE;
		}
	} else if (type == QB_LOG_FILTER_FORMAT) {
		if (strcmp(text, cs->format) == 0) {
			match = QB_TRUE;
		}
	}
	return match;
}

/*
 * 1 alloc a new callsite
 * 2 apply correct targets based on current filters
 * 3 pass onto qb_log_real_()
 *
 * Later:
 *  if (cs < __start___verbose in && cs > __stop___verbosed) {
 *	free(cs)
 *  }
 */
void qb_log_from_external_source(const char *function,
				 const char *filename,
				 const char *format,
				 uint8_t priority,
				 uint32_t lineno,
				 uint32_t tags,
				 const char *msg)
{
	struct qb_log_callsite *cs = calloc(1, sizeof(struct qb_log_callsite));
	struct qb_log_target *t;
	struct qb_log_filter *flt;

	cs->function = function;
	cs->filename = filename;
	cs->format = format;
	cs->priority = priority;
	cs->lineno = lineno;
	cs->tags = tags;

	qb_list_for_each_entry(t, &active_targets, active_list) {
		qb_list_for_each_entry(flt, &t->filter_head, list) {
			if (_cs_matches_filter_(cs,
						flt->type,
						flt->text,
						flt->priority)) {
				qb_bit_set(cs->targets, t->pos);
				break;
			}
		}
	}
	qb_log_real_(cs, msg);
}

static void qb_log_external_source_free(struct qb_log_callsite *cs)
{
	if (qb_bit_is_set(cs->tags, QB_LOG_TAG_EXTERNAL_BIT)) {
		free(cs);
	}
}

void qb_log_real_(struct qb_log_callsite *cs, ...)
{
	va_list ap;
	char buf[COMBINE_BUFFER_SIZE];
	size_t len;
	static int32_t in_logger = 0;
	int32_t found_threaded;
	struct qb_log_target *t;
	struct timeval tv;

	if (in_logger) {
		return;
	}
	in_logger = QB_TRUE;;

	va_start(ap, cs);
	len = vsnprintf(buf, COMBINE_BUFFER_SIZE, cs->format, ap);
	va_end(ap);

	if (buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len -= 1;
	}

	gettimeofday(&tv, NULL);

	if (old_internal_log_fn) {
		if (qb_bit_is_set(cs->tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
			old_internal_log_fn(cs->filename, cs->lineno, cs->priority, buf);
		}
	}

	/*
	 * 1 if we can find a threaded target that needs this log then post it
	 * 2 foreach non-threaded target call it's logger function
	 */
	found_threaded = QB_FALSE;
	qb_list_for_each_entry(t, &active_targets, active_list) {
		if (t->threaded) {
			if (!found_threaded && qb_bit_is_set(cs->targets, t->pos)) {
				found_threaded = QB_TRUE;
			}
		} else {
			if (qb_bit_is_set(cs->targets, t->pos) && t->logger) {
				t->logger(t, cs, tv.tv_sec, buf);
			}
		}
	}
	if (found_threaded) {
		qb_log_thread_log_post(cs, tv.tv_sec, buf);
	} else {
		qb_log_external_source_free(cs);
	}
	in_logger = QB_FALSE;
}

void qb_log_thread_log_write(struct qb_log_callsite *cs,
			     time_t timestamp,
			     const char *buffer)
{
	struct qb_log_target *t;

	qb_list_for_each_entry(t, &active_targets, active_list) {
		if (t->threaded) {
			continue;
		}
		if (qb_bit_is_set(cs->targets, t->pos)) {
			t->logger(t, cs, timestamp, buffer);
		}
	}
	qb_log_external_source_free(cs);
}


int32_t qb_log_filter_ctl(uint32_t t, enum qb_log_filter_conf c,
			  enum qb_log_filter_type type,
			  const char * text,
			  uint32_t priority)
{
	struct qb_log_callsite *cs;
	struct qb_log_filter *flt;
	struct qb_list_head* iter;
	struct qb_list_head* next;

	if (c == QB_LOG_FILTER_ADD) {
		flt = calloc(1, sizeof(struct qb_log_filter));
		qb_list_init(&flt->list);
		flt->type = type;
		flt->text = text;
		flt->priority = priority;
		qb_list_add_tail(&flt->list, &conf[t].filter_head);
	} else if (c == QB_LOG_FILTER_REMOVE) {

		qb_list_for_each_safe(iter, next, &conf[t].filter_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			if (flt->type == type && flt->priority == priority &&
			    strcmp(flt->text, text) == 0) {
				qb_list_del(iter);
				free(flt);
			}
		}

	} else {
		qb_list_for_each_safe(iter, next, &conf[t].filter_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			qb_list_del(iter);
			free(flt);
		}
	}

	for (cs = __start___verbose; cs < __stop___verbose; cs++) {

		if (c == QB_LOG_FILTER_CLEAR_ALL) {
			qb_bit_clear(cs->targets, t);
			continue;
		}

		if (_cs_matches_filter_(cs, type, text, priority)) {
			if (c == QB_LOG_FILTER_ADD) {
				qb_bit_set(cs->targets, t);
			} else {
				qb_bit_clear(cs->targets, t);
			}
#if 0
			printf("matched: %-12s %20s:%u fmt:<%d>%s\n",
				cs->function, cs->filename, cs->lineno,
				cs->priority, cs->format);
#endif
		}
	}
	return 0;
}

void qb_log_init(const char *name,
		 int32_t facility,
		 int32_t priority)
{
	int32_t i;

	for (i = 0; i < 32; i++) {
		conf[i].pos = i;
		conf[i].debug = QB_FALSE;
		conf[i].state = QB_LOG_STATE_UNUSED;
		conf[i].name[0] = '\0';
		conf[i].facility = facility;
		qb_list_init(&conf[i].filter_head);
		qb_list_init(&conf[i].active_list);
		qb_log_format_set(i, NULL);
	}
	conf[QB_LOG_SYSLOG].state = QB_LOG_STATE_ENABLED;
	qb_list_add(&conf[QB_LOG_SYSLOG].active_list, &active_targets);

	conf[QB_LOG_STDERR].state = QB_LOG_STATE_DISABLED;
	conf[QB_LOG_BLACKBOX].state = QB_LOG_STATE_DISABLED;
	strncpy(conf[QB_LOG_SYSLOG].name, name, PATH_MAX);
	snprintf(conf[QB_LOG_BLACKBOX].name, PATH_MAX, "%s-blackbox", name);

	(void)qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
				QB_LOG_FILTER_FILE, "*", priority);

	(void)qb_log_syslog_open(&conf[QB_LOG_SYSLOG]);
}

void qb_log_fini(void)
{
	struct qb_log_target *t;
	struct qb_list_head* iter;
	struct qb_list_head* next;

	qb_log_thread_stop();

	qb_list_for_each_safe(iter, next, &active_targets) {
		t = qb_list_entry(iter, struct qb_log_target, active_list);
		_log_target_disable(t);
	}
}

struct qb_log_target * qb_log_target_alloc(void)
{
	int32_t i;
	for (i = 0; i < 32; i++) {
		if (conf[i].state == QB_LOG_STATE_UNUSED) {
			return &conf[i];
		}
	}
	return NULL;
}

void qb_log_target_free(struct qb_log_target *t)
{
	(void)qb_log_filter_ctl(t->pos, QB_LOG_FILTER_CLEAR_ALL,
				QB_LOG_FILTER_FILE, NULL, 0);
	t->debug = QB_FALSE;
	t->state = QB_LOG_STATE_UNUSED;
	t->name[0] = '\0';
	qb_log_format_set(t->pos, NULL);
}

struct qb_log_target * qb_log_target_get(int32_t pos)
{
	return &conf[pos];
}

static int32_t _log_target_enable(struct qb_log_target *t)
{
	int32_t rc = 0;

	if (t->state == QB_LOG_STATE_ENABLED) {
		return 0;
	}
	if (t->pos == QB_LOG_STDERR) {
		rc = qb_log_stderr_open(t);
	} else if (t->pos == QB_LOG_SYSLOG) {
		rc = qb_log_syslog_open(t);
	} else if (t->pos == QB_LOG_BLACKBOX) {
		rc = qb_log_blackbox_open(t);
	}
	if (rc == 0) {
		t->state = QB_LOG_STATE_ENABLED;
		qb_list_add(&t->active_list, &active_targets);
	}
	return rc;
}

static void _log_target_disable(struct qb_log_target *t)
{
	if (t->state != QB_LOG_STATE_ENABLED) {
		return;
	}
	if (t->close) {
		t->close(t);
	}
	t->state = QB_LOG_STATE_DISABLED;
	qb_list_del(&t->active_list);
}

int32_t qb_log_ctl(uint32_t t, enum qb_log_conf c, int32_t arg)
{
	int32_t rc = 0;
	int32_t need_reload = QB_FALSE;

	if (t > 31) {
		return -EINVAL;
	}
	switch (c) {
	case QB_LOG_CONF_ENABLED:
		if (arg == QB_TRUE) {
			rc = _log_target_enable(&conf[t]);
		} else {
			_log_target_disable(&conf[t]);
		}
		break;
	case QB_LOG_CONF_FACILITY:
		conf[t].facility = arg;
		if (t == QB_LOG_SYSLOG) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_SIZE:
		conf[t].size = arg;
		if (t == QB_LOG_BLACKBOX) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_THREADED:
		conf[t].threaded = arg;
		break;

	default:
		rc = -EINVAL;
	}
	if (rc == 0 && need_reload && conf[t].reload) {
		conf[t].reload(&conf[t]);
	}
	return rc;
}

void qb_log_format_set(int32_t t, const char* format)
{
	if (conf[t].format) {
		free(conf[t].format);
		conf[t].format = NULL;
	}

	conf[t].format = strdup(format ? format : "[%p] %b");
	assert(conf[t].format != NULL);
}


