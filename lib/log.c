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
#ifdef HAVE_LINK_H
#include <link.h>
#endif /* HAVE_LINK_H */
#include <stdarg.h>
#include <pthread.h>
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif /* HAVE_DLFCN_H */
#include <stdarg.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include <qb/qbarray.h>
#include "log_int.h"
#include "util_int.h"

static struct qb_log_target conf[QB_LOG_TARGET_MAX];
static uint32_t conf_active_max = 0;
static int32_t in_logger = QB_FALSE;
static int32_t logger_inited = QB_FALSE;
static pthread_rwlock_t _listlock;
static qb_log_filter_fn _custom_filter_fn = NULL;

static QB_LIST_DECLARE(tags_head);
static QB_LIST_DECLARE(callsite_sections);

struct callsite_section {
	struct qb_log_callsite *start;
	struct qb_log_callsite *stop;
	struct qb_list_head list;
};

static int32_t _log_target_enable(struct qb_log_target *t);
static void _log_target_disable(struct qb_log_target *t);
static void _log_filter_apply(struct callsite_section *sect,
			      uint32_t t, enum qb_log_filter_conf c,
			      enum qb_log_filter_type type,
			      const char *text,
			      uint8_t high_priority, uint8_t low_priority);
static void _log_filter_apply_to_cs(struct qb_log_callsite *cs,
				    uint32_t t, enum qb_log_filter_conf c,
				    enum qb_log_filter_type type,
				    const char *text,
				    uint8_t high_priority, uint8_t low_priority);

/* deprecated method of getting internal log messages */
static qb_util_log_fn_t old_internal_log_fn = NULL;
void
qb_util_set_log_function(qb_util_log_fn_t fn)
{
	old_internal_log_fn = fn;
}

static int32_t
_cs_matches_filter_(struct qb_log_callsite *cs,
		    enum qb_log_filter_type type,
		    const char *text,
		    uint8_t high_priority,
		    uint8_t low_priority)
{
	int32_t match = QB_FALSE;

	if (cs->priority > low_priority ||
	    cs->priority < high_priority) {
		return QB_FALSE;
	}
	if (strcmp(text, "*") == 0) {
		return QB_TRUE;
	}
	if (type == QB_LOG_FILTER_FILE ||
	    type == QB_LOG_FILTER_FUNCTION) {
		char token[500];
		const char *offset = NULL;
		const char *next = text;

		do {
			offset = next;
			next = strchrnul(offset, ',');
			snprintf(token, 499, "%.*s", (int)(next - offset), offset);

			if (type == QB_LOG_FILTER_FILE) {
				match = (strstr(cs->filename, token) != NULL);
			} else {
				match = (strstr(cs->function, token) != NULL);
			}
			if (!match && next[0] != 0) {
				next++;
			}

		} while (match == QB_FALSE && next != NULL && next[0] != 0);
	} else if (type == QB_LOG_FILTER_FORMAT) {
		if (strstr(cs->format, text)) {
			match = QB_TRUE;
		}
	}
	return match;
}

void
qb_log_real_va_(struct qb_log_callsite *cs, va_list ap)
{
	int32_t found_threaded;
	struct qb_log_target *t;
	struct timespec tv;
	int32_t pos;
	int len;
	int32_t formatted = QB_FALSE;
	char buf[QB_LOG_MAX_LEN];
	char *str = buf;
	va_list ap_copy;

	if (in_logger || cs == NULL) {
		return;
	}
	in_logger = QB_TRUE;

	if (old_internal_log_fn) {
		if (qb_bit_is_set(cs->tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
			if (!formatted) {
				va_copy(ap_copy, ap);
				len = vsnprintf(str, QB_LOG_MAX_LEN, cs->format, ap_copy);
				va_end(ap_copy);
				if (len > QB_LOG_MAX_LEN)
					len = QB_LOG_MAX_LEN;
				if (str[len - 1] == '\n') str[len - 1] = '\0';
				formatted = QB_TRUE;
			}
			old_internal_log_fn(cs->filename, cs->lineno,
					    cs->priority, str);
		}
	}

	qb_util_timespec_from_epoch_get(&tv);

	/*
	 * 1 if we can find a threaded target that needs this log then post it
	 * 2 foreach non-threaded target call it's logger function
	 */
	found_threaded = QB_FALSE;

	for (pos = 0; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if (t->state != QB_LOG_STATE_ENABLED) {
			continue;
		}
		if (t->threaded) {
			if (!found_threaded
			    && qb_bit_is_set(cs->targets, t->pos)) {
				found_threaded = QB_TRUE;
				if (!formatted) {
					va_copy(ap_copy, ap);
					len = vsnprintf(str, QB_LOG_MAX_LEN, cs->format, ap_copy);
					va_end(ap_copy);
					if (len > QB_LOG_MAX_LEN)
						len = QB_LOG_MAX_LEN;
					if (str[len - 1] == '\n') str[len - 1] = '\0';
					formatted = QB_TRUE;
				}
			}
		} else {
			if (qb_bit_is_set(cs->targets, t->pos)) {
				if (t->vlogger) {
					va_copy(ap_copy, ap);
					t->vlogger(t->pos, cs, tv.tv_sec, ap_copy);
					va_end(ap_copy);
				} else if (t->logger) {
					if (!formatted) {
						va_copy(ap_copy, ap);
						len = vsnprintf(str, QB_LOG_MAX_LEN, cs->format, ap_copy);
						va_end(ap_copy);
						if (len > QB_LOG_MAX_LEN)
							len = QB_LOG_MAX_LEN;
						if (str[len - 1] == '\n') str[len - 1] = '\0';
						formatted = QB_TRUE;
					}
					t->logger(t->pos, cs, tv.tv_sec, str);
				}
			}
		}
	}

	if (found_threaded) {
		qb_log_thread_log_post(cs, tv.tv_sec, str);
	}
	in_logger = QB_FALSE;
}

void
qb_log_real_(struct qb_log_callsite *cs, ...)
{
	va_list ap;

	va_start(ap, cs);
	qb_log_real_va_(cs, ap);
	va_end(ap);
}

void
qb_log_thread_log_write(struct qb_log_callsite *cs,
			time_t timestamp, const char *buffer)
{
	struct qb_log_target *t;
	int32_t pos;

	for (pos = 0; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if (t->state != QB_LOG_STATE_ENABLED) {
			continue;
		}
		if (!t->threaded) {
			continue;
		}
		if (qb_bit_is_set(cs->targets, t->pos)) {
			t->logger(t->pos, cs, timestamp, buffer);
		}
	}
}

struct qb_log_callsite*
qb_log_callsite_get(const char *function,
		    const char *filename,
		    const char *format,
		    uint8_t priority,
		    uint32_t lineno,
		    uint32_t tags)
{
	struct qb_log_target *t;
	struct qb_log_filter *flt;
	struct qb_log_callsite *cs;
	int32_t new_dcs = QB_FALSE;
	struct qb_list_head *f_item;
	int32_t pos;

	if (!logger_inited) {
		return NULL;
	}

	cs = qb_log_dcs_get(&new_dcs, function, filename,
			    format, priority, lineno, tags);
	if (cs == NULL) {
		return NULL;
	}

	if (new_dcs) {
		pthread_rwlock_rdlock(&_listlock);
		for (pos = 0; pos <= conf_active_max; pos++) {
			t = &conf[pos];
			if (t->state != QB_LOG_STATE_ENABLED) {
				continue;
			}
			qb_list_for_each(f_item, &t->filter_head) {
				flt = qb_list_entry(f_item, struct qb_log_filter, list);
				_log_filter_apply_to_cs(cs, t->pos, flt->conf, flt->type,
							flt->text, flt->high_priority,
							flt->low_priority);
			}
		}
		if (tags == 0) {
			qb_list_for_each(f_item, &tags_head) {
				flt = qb_list_entry(f_item, struct qb_log_filter, list);
				_log_filter_apply_to_cs(cs, flt->new_value, flt->conf, flt->type,
							flt->text, flt->high_priority,
							flt->low_priority);
			}
		} else {
			cs->tags = tags;
		}
		if (_custom_filter_fn) {
			_custom_filter_fn(cs);
		}
		pthread_rwlock_unlock(&_listlock);
	} else if (cs->tags != tags) {
		cs->tags = tags;
		if (_custom_filter_fn) {
			_custom_filter_fn(cs);
		}
	}
	return cs;
}

void
qb_log_from_external_source_va(const char *function,
			       const char *filename,
			       const char *format,
			       uint8_t priority,
			       uint32_t lineno, uint32_t tags, va_list ap)
{
	struct qb_log_callsite *cs;

	if (!logger_inited) {
		return;
	}

	cs = qb_log_callsite_get(function, filename,
				 format, priority, lineno, tags);
	qb_log_real_va_(cs, ap);
}

void
qb_log_from_external_source(const char *function,
			    const char *filename,
			    const char *format,
			    uint8_t priority,
			    uint32_t lineno, uint32_t tags, ...)
{
	struct qb_log_callsite *cs;
	va_list ap;

	if (!logger_inited) {
		return;
	}

	cs = qb_log_callsite_get(function, filename,
				 format, priority, lineno, tags);
	va_start(ap, tags);
	qb_log_real_va_(cs, ap);
	va_end(ap);
}

int32_t
qb_log_callsites_register(struct qb_log_callsite *_start,
			  struct qb_log_callsite *_stop)
{
	struct callsite_section *sect;
	struct qb_log_callsite *cs;
	struct qb_log_target *t;
	struct qb_log_filter *flt;
	int32_t pos;

	if (_start == NULL || _stop == NULL) {
		return -EINVAL;
	}

	pthread_rwlock_rdlock(&_listlock);
	qb_list_for_each_entry(sect, &callsite_sections, list) {
		if (sect->start == _start || sect->stop == _stop) {
			pthread_rwlock_unlock(&_listlock);
			return -EEXIST;
		}
	}
	pthread_rwlock_unlock(&_listlock);

	sect = calloc(1, sizeof(struct callsite_section));
	if (sect == NULL) {
		return -ENOMEM;
	}
	sect->start = _start;
	sect->stop = _stop;
	qb_list_init(&sect->list);

	pthread_rwlock_wrlock(&_listlock);
	qb_list_add(&sect->list, &callsite_sections);

	/*
	 * Now apply the filters on these new callsites
	 */
	for (pos = 0; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if (t->state != QB_LOG_STATE_ENABLED) {
			continue;
		}
		qb_list_for_each_entry(flt, &t->filter_head, list) {
			_log_filter_apply(sect, t->pos, flt->conf,
					  flt->type, flt->text,
					  flt->high_priority, flt->low_priority);
		}
	}
	qb_list_for_each_entry(flt, &tags_head, list) {
		_log_filter_apply(sect, flt->new_value, flt->conf,
				  flt->type, flt->text,
				  flt->high_priority, flt->low_priority);
	}
	pthread_rwlock_unlock(&_listlock);
	if (_custom_filter_fn) {
		for (cs = sect->start; cs < sect->stop; cs++) {
			if (cs->lineno > 0) {
				_custom_filter_fn(cs);
			}
		}
	}
	/* qb_log_callsites_dump_sect(sect); */

	return 0;
}

static void
qb_log_callsites_dump_sect(struct callsite_section *sect)
{
	struct qb_log_callsite *cs;

	printf(" start %p - stop %p\n", sect->start, sect->stop);
	printf("filename    lineno targets         tags\n");
	for (cs = sect->start; cs < sect->stop; cs++) {
		if (cs->lineno > 0) {
			printf("%12s %6d %16d %16d\n", cs->filename, cs->lineno,
			       cs->targets, cs->tags);
		}
	}
}

void
qb_log_callsites_dump(void)
{
	struct callsite_section *sect;
	int32_t l;

	pthread_rwlock_rdlock(&_listlock);
	l = qb_list_length(&callsite_sections);
	printf("Callsite Database [%d]\n", l);
	printf("---------------------\n");
	qb_list_for_each_entry(sect, &callsite_sections, list) {
		qb_log_callsites_dump_sect(sect);
	}
	pthread_rwlock_unlock(&_listlock);
}

static int32_t
_log_filter_exists(struct qb_list_head *list_head,
		   enum qb_log_filter_type type,
		   const char *text,
		   uint8_t high_priority,
		   uint8_t low_priority,
		   uint32_t new_value)
{
	struct qb_log_filter *flt;

	qb_list_for_each_entry(flt, list_head, list) {
		if (flt->type == type &&
		    flt->high_priority == high_priority &&
		    flt->low_priority == low_priority &&
		    flt->new_value == new_value &&
		    strcmp(flt->text, text) == 0) {
			return QB_TRUE;
		}
	}
	return QB_FALSE;
}

static int32_t
_log_filter_store(uint32_t t, enum qb_log_filter_conf c,
		  enum qb_log_filter_type type,
		  const char *text,
		  uint8_t high_priority,
		  uint8_t low_priority)
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
		if (_log_filter_exists(list_head, type, text,
				       high_priority, low_priority, t)) {
			return -EEXIST;
		}
		flt = calloc(1, sizeof(struct qb_log_filter));
		if (flt == NULL) {
			return -ENOMEM;
		}
		qb_list_init(&flt->list);
		flt->conf = c;
		flt->type = type;
		flt->text = strdup(text);
		if (flt->text == NULL) {
			free(flt);
			return -ENOMEM;
		}
		flt->high_priority = high_priority;
		flt->low_priority = low_priority;
		flt->new_value = t;
		qb_list_add_tail(&flt->list, list_head);
	} else if (c == QB_LOG_FILTER_REMOVE || c == QB_LOG_TAG_CLEAR) {
		qb_list_for_each_safe(iter, next, list_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			if (flt->type == type &&
			    flt->low_priority <= low_priority &&
			    flt->high_priority >= high_priority &&
			    (strcmp(flt->text, text) == 0 ||
			     strcmp("*", text) == 0)) {
				qb_list_del(iter);
				free(flt->text);
				free(flt);
				return 0;
			}
		}

	} else if (c == QB_LOG_FILTER_CLEAR_ALL || c == QB_LOG_TAG_CLEAR_ALL) {
		qb_list_for_each_safe(iter, next, list_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			qb_list_del(iter);
			free(flt->text);
			free(flt);
		}
	}
	return 0;
}

static void
_log_filter_apply(struct callsite_section *sect,
		  uint32_t t, enum qb_log_filter_conf c,
		  enum qb_log_filter_type type,
		  const char *text,
		  uint8_t high_priority, uint8_t low_priority)
{
	struct qb_log_callsite *cs;

	for (cs = sect->start; cs < sect->stop; cs++) {
		if (cs->lineno > 0) {
			_log_filter_apply_to_cs(cs, t, c, type, text,
					    high_priority, low_priority);
		}
	}
}

/* #define _QB_FILTER_DEBUGGING_ 1 */
static void
_log_filter_apply_to_cs(struct qb_log_callsite *cs,
			uint32_t t, enum qb_log_filter_conf c,
			enum qb_log_filter_type type,
			const char *text,
			uint8_t high_priority, uint8_t low_priority)
{

	if (c == QB_LOG_FILTER_CLEAR_ALL) {
		qb_bit_clear(cs->targets, t);
		return;
	} else if (c == QB_LOG_TAG_CLEAR_ALL) {
		cs->tags = 0;
		return;
	}

	if (_cs_matches_filter_(cs, type, text, high_priority, low_priority)) {
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
			       cs->filename, cs->lineno, t, old_tags, cs->tags);
		}
#endif /* _QB_FILTER_DEBUGGING_ */
	}
}

int32_t
qb_log_filter_ctl2(int32_t t, enum qb_log_filter_conf c,
		   enum qb_log_filter_type type, const char * text,
		   uint8_t high_priority, uint8_t low_priority)
{
	struct callsite_section *sect;
	int32_t rc;

	if (!logger_inited) {
		return -EINVAL;
	}

	if (c == QB_LOG_FILTER_ADD ||
	    c == QB_LOG_FILTER_CLEAR_ALL ||
	    c == QB_LOG_FILTER_REMOVE) {
		if (t < 0 || t >= QB_LOG_TARGET_MAX ||
		    conf[t].state == QB_LOG_STATE_UNUSED) {
			return -EBADF;
		}
	}

	if (text == NULL ||
	    low_priority < high_priority ||
	    type > QB_LOG_FILTER_FORMAT ||
	    c > QB_LOG_TAG_CLEAR_ALL) {
		return -EINVAL;
	}
	pthread_rwlock_rdlock(&_listlock);
	rc = _log_filter_store(t, c, type, text, high_priority, low_priority);
	if (rc < 0) {
		pthread_rwlock_unlock(&_listlock);
		return rc;
	}

	qb_list_for_each_entry(sect, &callsite_sections, list) {
		_log_filter_apply(sect, t, c, type, text, high_priority, low_priority);
	}
	pthread_rwlock_unlock(&_listlock);
	return 0;
}

int32_t
qb_log_filter_fn_set(qb_log_filter_fn fn)
{
	struct callsite_section *sect;
	struct qb_log_callsite *cs;

	if (!logger_inited) {
		return -EINVAL;
	}
	_custom_filter_fn = fn;
	if (_custom_filter_fn == NULL) {
		return 0;
	}

	qb_list_for_each_entry(sect, &callsite_sections, list) {
		for (cs = sect->start; cs < sect->stop; cs++) {
			if (cs->lineno > 0) {
				_custom_filter_fn(cs);
			}
		}
	}
	return 0;
}

int32_t
qb_log_filter_ctl(int32_t t, enum qb_log_filter_conf c,
		  enum qb_log_filter_type type,
		  const char *text, uint8_t priority)
{
	return qb_log_filter_ctl2(t, c, type, text, LOG_EMERG, priority);
}

#ifdef QB_HAVE_ATTRIBUTE_SECTION
static int32_t
_log_so_walk_callback(struct dl_phdr_info *info, size_t size, void *data)
{
	if (strlen(info->dlpi_name) > 0) {
		void *handle;
		void *start;
		void *stop;
		const char *error;

		handle = dlopen(info->dlpi_name, RTLD_LAZY);
		error = dlerror();
		if (!handle || error) {
			qb_log(LOG_ERR, "%s", error);
			if (handle) {
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

static void
_log_target_state_set(struct qb_log_target *t, enum qb_log_target_state s)
{
	int32_t i;
	int32_t a_set = QB_FALSE;
	int32_t u_set = QB_FALSE;

	t->state = s;

	for (i = 31; i >= 0; i--) {
		if (!a_set && conf[i].state == QB_LOG_STATE_ENABLED) {
			a_set = QB_TRUE;
			conf_active_max = i;
		}
		if (!u_set && conf[i].state != QB_LOG_STATE_UNUSED) {
			u_set = QB_TRUE;
		}
	}
}

void
qb_log_init(const char *name, int32_t facility, uint8_t priority)
{
	int32_t i;

	i = pthread_rwlock_init(&_listlock, NULL);
	assert(i == 0);
	qb_log_format_init();

	for (i = 0; i < QB_LOG_TARGET_MAX; i++) {
		conf[i].pos = i;
		conf[i].debug = QB_FALSE;
		conf[i].file_sync = QB_FALSE;
		conf[i].state = QB_LOG_STATE_UNUSED;
		(void)strlcpy(conf[i].name, name, PATH_MAX);
		conf[i].facility = facility;
		qb_list_init(&conf[i].filter_head);
	}

	qb_log_dcs_init();
#ifdef QB_HAVE_ATTRIBUTE_SECTION
	qb_log_callsites_register(__start___verbose, __stop___verbose);
	dl_iterate_phdr(_log_so_walk_callback, NULL);
#endif /* QB_HAVE_ATTRIBUTE_SECTION */

	conf[QB_LOG_STDERR].state = QB_LOG_STATE_DISABLED;
	conf[QB_LOG_BLACKBOX].state = QB_LOG_STATE_DISABLED;
	conf[QB_LOG_STDOUT].state = QB_LOG_STATE_DISABLED;

	logger_inited = QB_TRUE;
	(void)qb_log_syslog_open(&conf[QB_LOG_SYSLOG]);
	_log_target_state_set(&conf[QB_LOG_SYSLOG], QB_LOG_STATE_ENABLED);
	(void)qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
				QB_LOG_FILTER_FILE, "*", priority);
}

void
qb_log_fini(void)
{
	struct qb_log_target *t;
	struct qb_log_filter *flt;
	struct callsite_section *s;
	struct qb_list_head *iter;
	struct qb_list_head *iter2;
	struct qb_list_head *next;
	struct qb_list_head *next2;
	int32_t pos;

	if (!logger_inited) {
		return;
	}
	logger_inited = QB_FALSE;
	qb_log_thread_stop();
	pthread_rwlock_destroy(&_listlock);

	for (pos = 0; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		_log_target_disable(t);
		qb_list_for_each_safe(iter2, next2, &t->filter_head) {
			flt = qb_list_entry(iter2, struct qb_log_filter, list);
			qb_list_del(iter2);
			free(flt->text);
			free(flt);
		}
	}
	qb_log_format_fini();
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

struct qb_log_target *
qb_log_target_alloc(void)
{
	int32_t i;
	for (i = 0; i < QB_LOG_TARGET_MAX; i++) {
		if (conf[i].state == QB_LOG_STATE_UNUSED) {
			_log_target_state_set(&conf[i], QB_LOG_STATE_DISABLED);
			return &conf[i];
		}
	}
	return NULL;
}

void
qb_log_target_free(struct qb_log_target *t)
{
	(void)qb_log_filter_ctl(t->pos, QB_LOG_FILTER_CLEAR_ALL,
				QB_LOG_FILTER_FILE, NULL, 0);
	t->debug = QB_FALSE;
	t->filename[0] = '\0';
	qb_log_format_set(t->pos, NULL);
	_log_target_state_set(t, QB_LOG_STATE_UNUSED);
}

struct qb_log_target *
qb_log_target_get(int32_t pos)
{
	return &conf[pos];
}

void *
qb_log_target_user_data_get(int32_t t)
{
	if (t < 0 || t >= QB_LOG_TARGET_MAX ||
	    conf[t].state == QB_LOG_STATE_UNUSED) {
		errno = -EBADF;
		return NULL;
	}

	return conf[t].instance;
}

int32_t
qb_log_target_user_data_set(int32_t t, void *user_data)
{
	if (!logger_inited) {
		return -EINVAL;
	}
	if (t < 0 || t >= QB_LOG_TARGET_MAX ||
	    conf[t].state == QB_LOG_STATE_UNUSED) {
		return -EBADF;
	}

	conf[t].instance = user_data;
	return 0;
}

int32_t
qb_log_custom_open(qb_log_logger_fn log_fn,
		   qb_log_close_fn close_fn,
		   qb_log_reload_fn reload_fn, void *user_data)
{
	struct qb_log_target *t;

	t = qb_log_target_alloc();
	if (t == NULL) {
		return -errno;
	}

	t->instance = user_data;
	snprintf(t->filename, PATH_MAX, "custom-%d", t->pos);

	t->logger = log_fn;
	t->vlogger = NULL;
	t->reload = reload_fn;
	t->close = close_fn;

	return t->pos;
}

void
qb_log_custom_close(int32_t t)
{
	struct qb_log_target *target;

	if (!logger_inited) {
		return;
	}
	if (t < 0 || t >= QB_LOG_TARGET_MAX ||
	    conf[t].state == QB_LOG_STATE_UNUSED) {
		return;
	}

	target = qb_log_target_get(t);

	if (target->close) {
		in_logger = QB_TRUE;
		target->close(t);
		in_logger = QB_FALSE;
	}
	qb_log_target_free(target);
}

static int32_t
_log_target_enable(struct qb_log_target *t)
{
	int32_t rc = 0;

	if (t->state == QB_LOG_STATE_ENABLED) {
		return 0;
	}
	if (t->pos == QB_LOG_STDERR ||
	    t->pos == QB_LOG_STDOUT) {
		rc = qb_log_stderr_open(t);
	} else if (t->pos == QB_LOG_SYSLOG) {
		rc = qb_log_syslog_open(t);
	} else if (t->pos == QB_LOG_BLACKBOX) {
		rc = qb_log_blackbox_open(t);
	}
	if (rc == 0) {
		_log_target_state_set(t, QB_LOG_STATE_ENABLED);
	}
	return rc;
}

static void
_log_target_disable(struct qb_log_target *t)
{
	if (t->state != QB_LOG_STATE_ENABLED) {
		return;
	}
	_log_target_state_set(t, QB_LOG_STATE_DISABLED);
	if (t->close) {
		in_logger = QB_TRUE;
		t->close(t->pos);
		in_logger = QB_FALSE;
	}
}

int32_t
qb_log_ctl(int32_t t, enum qb_log_conf c, int32_t arg)
{
	int32_t rc = 0;
	int32_t need_reload = QB_FALSE;

	if (!logger_inited) {
		return -EINVAL;
	}
	if (t < 0 || t >= QB_LOG_TARGET_MAX ||
	    conf[t].state == QB_LOG_STATE_UNUSED) {
		return -EBADF;
	}
	switch (c) {
	case QB_LOG_CONF_ENABLED:
		if (arg) {
			rc = _log_target_enable(&conf[t]);
		} else {
			_log_target_disable(&conf[t]);
		}
		break;
	case QB_LOG_CONF_STATE_GET:
		rc = conf[t].state;
		break;
	case QB_LOG_CONF_FACILITY:
		conf[t].facility = arg;
		if (t == QB_LOG_SYSLOG) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_FILE_SYNC:
		conf[t].file_sync = arg;
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
		in_logger = QB_TRUE;
		conf[t].reload(t);
		in_logger = QB_FALSE;
	}
	return rc;
}
