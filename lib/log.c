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
#include <link.h>
#include <stdarg.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include <qb/qbarray.h>
#include "log_int.h"

static struct qb_log_target conf[32];
static int32_t in_logger = QB_FALSE;
static int32_t logger_inited = QB_FALSE;

static QB_LIST_DECLARE(active_targets);
static QB_LIST_DECLARE(tags_head);
static QB_LIST_DECLARE(callsite_sections);

struct callsite_section {
	struct qb_log_callsite * start;
	struct qb_log_callsite * stop;
	struct qb_list_head list;
};

static int32_t _log_target_enable(struct qb_log_target *t);
static void _log_target_disable(struct qb_log_target *t);
static void _log_filter_apply(struct callsite_section *sect,
			      uint32_t t, enum qb_log_filter_conf c,
			      enum qb_log_filter_type type,
			      const char *text, uint32_t priority);

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
		if (strstr(cs->filename, text)) {
			match = QB_TRUE;
		}
	} else if (type == QB_LOG_FILTER_FUNCTION) {
		if (strstr(cs->function, text)) {
			match = QB_TRUE;
		}
	} else if (type == QB_LOG_FILTER_FORMAT) {
		if (strstr(cs->format, text)) {
			match = QB_TRUE;
		}
	}
	return match;
}

static void _log_real_msg(struct qb_log_callsite *cs, const char *msg)
{
	int32_t found_threaded;
	struct qb_log_target *t;
	struct timeval tv;
	struct qb_list_head *pos;

	if (in_logger) {
		return;
	}
	in_logger = QB_TRUE;

	gettimeofday(&tv, NULL);

	if (old_internal_log_fn) {
		if (qb_bit_is_set(cs->tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
			old_internal_log_fn(cs->filename, cs->lineno, cs->priority, msg);
		}
	}

	/*
	 * 1 if we can find a threaded target that needs this log then post it
	 * 2 foreach non-threaded target call it's logger function
	 */
	found_threaded = QB_FALSE;

	for (pos = active_targets.next; pos != &active_targets; pos = pos->next) {
		t = qb_list_entry(pos, struct qb_log_target, active_list);
		if (t->threaded) {
			if (!found_threaded && qb_bit_is_set(cs->targets, t->pos)) {
				found_threaded = QB_TRUE;
			}
		} else {
			if (qb_bit_is_set(cs->targets, t->pos) && t->logger) {
				t->logger(t, cs, tv.tv_sec, msg);
			}
		}
	}
	if (found_threaded) {
		qb_log_thread_log_post(cs, tv.tv_sec, msg);
	}
	in_logger = QB_FALSE;
}

void qb_log_thread_log_write(struct qb_log_callsite *cs,
			     time_t timestamp, const char *buffer)
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
}

void qb_log_from_external_source(const char *function,
				 const char *filename,
				 const char *format,
				 uint8_t priority,
				 uint32_t lineno,
				 uint32_t tags,
				 const char *msg)
{
	struct qb_log_target *t;
	struct qb_log_filter *flt;
	struct qb_log_callsite *cs;
	int32_t new_dcs = QB_FALSE;

	cs = qb_log_dcs_get(&new_dcs, function, filename,
			    format, priority, lineno, tags);
	assert(cs != NULL);

	if (new_dcs) {
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
		if (tags != 0) {
			qb_list_for_each_entry(flt, &tags_head, list) {
				if (_cs_matches_filter_(cs,
							flt->type,
							flt->text,
							flt->priority)) {
					cs->tags = tags;
					break;
				}
			}
		}

	}
	_log_real_msg(cs, msg);
}

void qb_log_real_(struct qb_log_callsite *cs, ...)
{
	va_list ap;
	char buf[COMBINE_BUFFER_SIZE];
	size_t len;

	va_start(ap, cs);
	len = vsnprintf(buf, COMBINE_BUFFER_SIZE, cs->format, ap);
	va_end(ap);

	if (buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len -= 1;
	}
	_log_real_msg(cs, buf);
}

void qb_log_callsites_register(struct qb_log_callsite *_start, struct qb_log_callsite *_stop)
{
	struct callsite_section *sect;
	struct qb_log_target *t;
	struct qb_log_filter *flt;

	if (_start == NULL || _stop == NULL) {
		return;
	}

	qb_list_for_each_entry(sect, &callsite_sections, list) {
		if (sect->start == _start || sect->stop == _stop) {
			return;
		}
	}

	sect = calloc(1, sizeof(struct callsite_section));
	sect->start = _start;
	sect->stop = _stop;
	qb_list_init(&sect->list);
	qb_list_add(&sect->list, &callsite_sections);

	/*
	 * Now apply the filters on these new callsites
	 */
	qb_list_for_each_entry(t, &active_targets, active_list) {
		qb_list_for_each_entry(flt, &t->filter_head, list) {
			_log_filter_apply(sect, t->pos, flt->conf,
					  flt->type, flt->text, flt->priority);
		}
	}
	qb_list_for_each_entry(flt, &tags_head, list) {
		_log_filter_apply(sect, flt->new_value, flt->conf,
				  flt->type, flt->text, flt->priority);
	}
}

void qb_log_callsites_dump(void)
{
	struct callsite_section *sect;
	struct qb_log_callsite *cs;
	int32_t l = qb_list_length(&callsite_sections);

	printf("Callsite Database [%d]\n", l);
	printf("---------------------\n");
	qb_list_for_each_entry(sect, &callsite_sections, list) {
		printf(" start %p - stop %p\n", sect->start, sect->stop);
		printf("filename    lineno targets         tags\n");
		for (cs = sect->start; cs < sect->stop; cs++) {
			if (cs->lineno == 0) {
				break;
			}
			printf("%12s %6d %16d %16d\n", cs->filename, cs->lineno,
			       cs->targets, cs->tags);
		}
	}
}

static int32_t _log_filter_exists(struct qb_list_head *list_head,
				  enum qb_log_filter_type type,
				  const char *text, uint32_t priority,
				  uint32_t new_value)
{
	struct qb_log_filter *flt;

	qb_list_for_each_entry(flt, list_head, list) {
		if (flt->type == type &&
		    flt->priority == priority &&
		    flt->new_value == new_value &&
		    strcmp(flt->text, text) == 0) {
			return QB_TRUE;
		}
	}
	return QB_FALSE;
}

static int32_t _log_filter_store(uint32_t t, enum qb_log_filter_conf c,
				 enum qb_log_filter_type type,
				 const char *text, uint32_t priority)
{
	struct qb_log_filter *flt;
	struct qb_list_head *iter;
	struct qb_list_head *next;
	struct qb_list_head *list_head;

	switch (c) {
	case QB_LOG_FILTER_ADD:
	case QB_LOG_FILTER_REMOVE:
	case QB_LOG_FILTER_CLEAR_ALL:
		list_head = &conf[t].filter_head;
		break;

	case QB_LOG_TAG_SET:
	case QB_LOG_TAG_CLEAR:
	case QB_LOG_TAG_CLEAR_ALL:
		list_head = &tags_head;
		break;
	default:
		return -ENOSYS;
	}

	if (c == QB_LOG_FILTER_ADD || c == QB_LOG_TAG_SET) {
		if (text == NULL) {
			return -EINVAL;
		}
		if (_log_filter_exists(list_head, type, text, priority, t)) {
			return -EEXIST;
		}
		flt = calloc(1, sizeof(struct qb_log_filter));
		qb_list_init(&flt->list);
		flt->conf = c;
		flt->type = type;
		flt->text = strdup(text);
		flt->priority = priority;
		flt->new_value = t;
		qb_list_add_tail(&flt->list, list_head);
	} else if (c == QB_LOG_FILTER_REMOVE ||
		   c == QB_LOG_TAG_CLEAR) {
		qb_list_for_each_safe(iter, next, list_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			if (flt->type == type &&
			    flt->priority <= priority &&
			    strcmp(flt->text, text) == 0) {
				qb_list_del(iter);
				free(flt->text);
				free(flt);
				return 0;
			}
		}

	} else if (c == QB_LOG_FILTER_CLEAR_ALL ||
		   c == QB_LOG_TAG_CLEAR_ALL) {
		qb_list_for_each_safe(iter, next, list_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			qb_list_del(iter);
			free(flt->text);
			free(flt);
		}
	}
	return 0;
}

static void _log_filter_apply(struct callsite_section *sect,
			      uint32_t t, enum qb_log_filter_conf c,
			      enum qb_log_filter_type type,
			      const char *text, uint32_t priority)
{
	struct qb_log_callsite *cs;

	for (cs = sect->start; cs < sect->stop; cs++) {
		if (cs->lineno == 0) {
			break;
		}

		if (c == QB_LOG_FILTER_CLEAR_ALL) {
			qb_bit_clear(cs->targets, t);
			continue;
		} else if (c == QB_LOG_TAG_CLEAR_ALL) {
			cs->tags = 0;
			continue;
		}

		if (_cs_matches_filter_(cs, type, text, priority)) {
#ifdef _QB_FILTER_DEBUGGING_
			uint32_t old_targets = cs->targets;
			uint32_t old_tags = cs->tags;
#endif /* _QB_FILTER_DEBUGGING_ */
			if (c == QB_LOG_FILTER_ADD) {
				qb_bit_set(cs->targets, t);
			} else if (c == QB_LOG_FILTER_REMOVE) {
				qb_bit_clear(cs->targets, t);
			} else if (c == QB_LOG_TAG_SET) {
				cs->tags = t;
			} else if (c == QB_LOG_TAG_CLEAR) {
				cs->tags = 0;
			}
#ifdef _QB_FILTER_DEBUGGING_
			if (old_targets != cs->targets) {
				printf("targets: %s:%u value(%d) %d -> %d\n",
				       cs->filename, cs->lineno, t,
				       old_targets, cs->targets);
			}
			if (old_tags != cs->tags) {
				printf("tags: %s:%u value(%d) %d -> %d\n",
				       cs->filename, cs->lineno, t,
				       old_tags, cs->tags);
			}
#endif /* _QB_FILTER_DEBUGGING_ */
		}
	}
}

int32_t qb_log_filter_ctl(int32_t t, enum qb_log_filter_conf c,
			  enum qb_log_filter_type type,
			  const char *text, uint32_t priority)
{
	struct callsite_section *sect;
	int32_t rc;

	if (!logger_inited) {
		return -EINVAL;
	}
	if (t < 0 || t >= 32) {
		return -EBADF;
	}
	if (conf[t].state == QB_LOG_STATE_UNUSED) {
		return -EBADFD;
	}
	if (text == NULL ||
	    priority > LOG_TRACE ||
	    type > QB_LOG_FILTER_FORMAT ||
	    c > QB_LOG_TAG_CLEAR_ALL) {
		return -EINVAL;
	}
	rc = _log_filter_store(t, c, type, text, priority);
	if (rc < 0) {
		return rc;
	}

	qb_list_for_each_entry(sect, &callsite_sections, list) {
		_log_filter_apply(sect, t, c, type, text, priority);
	}
	return 0;
}

#ifdef QB_HAVE_ATTRIBUTE_SECTION
static int32_t
_log_so_walk_callback(struct dl_phdr_info *info, size_t size, void *data)
{
	if (strlen(info->dlpi_name) > 0) {
		void *handle;
		void *start;
		void *stop;
		char *error;

		handle = dlopen(info->dlpi_name, RTLD_LAZY);
		error = dlerror();
		if (!handle || error) {
			qb_log(LOG_ERR, "%s", error);
			if(handle) {
				dlclose(handle);
			}
			return 0;
		}

		start = dlsym(handle, "__start___verbose");
		error = dlerror();
		if (error) {
			goto done;
		}

		stop = dlsym(handle, "__stop___verbose");
		error = dlerror();
		if (error) {
			goto done;

		} else {
			qb_log_callsites_register(start, stop);
		}
done:
		dlclose(handle);
	}
	return 0;
}
#endif /* QB_HAVE_ATTRIBUTE_SECTION */

void qb_log_init(const char *name, int32_t facility, int32_t priority)
{
	int32_t i;

	qb_log_dcs_init();

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

#ifdef QB_HAVE_ATTRIBUTE_SECTION
	qb_log_callsites_register(__start___verbose, __stop___verbose);
	dl_iterate_phdr(_log_so_walk_callback, NULL);
#endif /* QB_HAVE_ATTRIBUTE_SECTION */

	conf[QB_LOG_STDERR].state = QB_LOG_STATE_DISABLED;
	conf[QB_LOG_BLACKBOX].state = QB_LOG_STATE_DISABLED;
	strncpy(conf[QB_LOG_SYSLOG].name, name, PATH_MAX);
	snprintf(conf[QB_LOG_BLACKBOX].name, PATH_MAX, "%s-blackbox", name);

	logger_inited = QB_TRUE;
	(void)qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
				QB_LOG_FILTER_FILE, "*", priority);

	(void)qb_log_syslog_open(&conf[QB_LOG_SYSLOG]);
}

void qb_log_fini(void)
{
	struct qb_log_target *t;
	struct qb_log_filter *flt;
	struct callsite_section *s;
	struct qb_list_head *iter;
	struct qb_list_head *iter2;
	struct qb_list_head *next;
	struct qb_list_head *next2;

	logger_inited = QB_FALSE;
	qb_log_thread_stop();

	qb_list_for_each_safe(iter, next, &active_targets) {
		t = qb_list_entry(iter, struct qb_log_target, active_list);
		_log_target_disable(t);
		qb_list_for_each_safe(iter2, next2, &t->filter_head) {
			flt = qb_list_entry(iter2, struct qb_log_filter, list);
			qb_list_del(iter2);
			free(flt->text);
			free(flt);
		}
	}
	qb_log_dcs_fini();
	qb_list_for_each_safe(iter, next, &callsite_sections) {
		s = qb_list_entry(iter, struct callsite_section, list);
		qb_list_del(iter);
		free(s);
	}
	qb_list_for_each_safe(iter, next, &tags_head) {
		flt = qb_list_entry(iter, struct qb_log_filter, list);
		qb_list_del(iter);
		free(flt->text);
		free(flt);
	}
}

struct qb_log_target *qb_log_target_alloc(void)
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

struct qb_log_target *qb_log_target_get(int32_t pos)
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
	t->state = QB_LOG_STATE_DISABLED;
	qb_list_del(&t->active_list);
	if (t->close) {
		in_logger = QB_TRUE;
		t->close(t);
		in_logger = QB_FALSE;
	}
}

int32_t qb_log_ctl(int32_t t, enum qb_log_conf c, int32_t arg)
{
	int32_t rc = 0;
	int32_t need_reload = QB_FALSE;

	if (!logger_inited) {
		return -EINVAL;
	}
	if (t < 0 || t >= 32) {
		return -EBADF;
	}
	if (conf[t].state == QB_LOG_STATE_UNUSED) {
		return -EBADFD;
	}
	switch (c) {
	case QB_LOG_CONF_ENABLED:
		if (arg) {
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
	case QB_LOG_CONF_PRIORITY_BUMP:
		conf[t].priority_bump = arg;
		break;
	case QB_LOG_CONF_SIZE:
		if (t == QB_LOG_BLACKBOX) {
			if (arg <= 0) {
				return -EINVAL;
			}
			conf[t].size = arg;
			need_reload = QB_TRUE;
		} else {
			return -ENOSYS;
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

void qb_log_format_set(int32_t t, const char *format)
{
	if (conf[t].format) {
		free(conf[t].format);
		conf[t].format = NULL;
	}

	conf[t].format = strdup(format ? format : "[%p] %b");
	assert(conf[t].format != NULL);
}

