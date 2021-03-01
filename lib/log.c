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
#include <stdarg.h>
#include <string.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include <qb/qbarray.h>
#include <qb/qbatomic.h>
#include "log_int.h"
#include "util_int.h"
#include <regex.h>

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
			      regex_t *regex,
			      uint8_t high_priority, uint8_t low_priority);
static void _log_filter_apply_to_cs(struct qb_log_callsite *cs,
				    uint32_t t, enum qb_log_filter_conf c,
				    enum qb_log_filter_type type,
				    const char *text,
				    regex_t *regex,
				    uint8_t high_priority, uint8_t low_priority);

/* deprecated method of getting internal log messages */
static qb_util_log_fn_t old_internal_log_fn = NULL;
void
qb_util_set_log_function(qb_util_log_fn_t fn)
{
	old_internal_log_fn = fn;
}

static void
_log_free_filter(struct qb_log_filter *flt)
{
	if (flt->regex) {
		regfree(flt->regex);
	}
	free(flt->regex);
	free(flt->text);
	free(flt);
}

static int32_t
_cs_matches_filter_(struct qb_log_callsite *cs,
		    enum qb_log_filter_type type,
		    const char *text,
		    regex_t *regex,
		    uint8_t high_priority,
		    uint8_t low_priority)
{
	int32_t match = QB_FALSE;
	const char *offset = NULL;
	const char *next = NULL;

	if (cs->priority > low_priority ||
	    cs->priority < high_priority) {
		return QB_FALSE;
	}
	if (strcmp(text, "*") == 0) {
		return QB_TRUE;
	}

	switch (type) {
	case QB_LOG_FILTER_FILE:
	case QB_LOG_FILTER_FUNCTION:
		next = text;
		do {
			char token[500];
			offset = next;
			next = strchrnul(offset, ',');
			snprintf(token, 499, "%.*s", (int)(next - offset), offset);

			if (type == QB_LOG_FILTER_FILE) {
				match = (strcmp(cs->filename, token) == 0) ? 1 : 0;
			} else {
				match = (strcmp(cs->function, token) == 0) ? 1 : 0;
			}
			if (!match && next[0] != 0) {
				next++;
			}

		} while (match == QB_FALSE && next != NULL && next[0] != 0);
		break;
	case QB_LOG_FILTER_FILE_REGEX:
		next = next ? next : cs->filename;
		/* fallthrough */
	case QB_LOG_FILTER_FUNCTION_REGEX:
		next = next ? next : cs->function;
		/* fallthrough */
	case QB_LOG_FILTER_FORMAT_REGEX:
		next = next ? next : cs->format;

		if (regex == NULL) {
			return QB_FALSE;
		}
		match = regexec(regex, next, 0, NULL, 0);
		if (match == 0) {
			match = QB_TRUE;
		} else {
			match = QB_FALSE;
		}
		break;
	case QB_LOG_FILTER_FORMAT:
		if (strstr(cs->format, text)) {
			match = QB_TRUE;
		}
		break;
	}

	return match;
}

/**
 * @internal
 * @brief Format a log message into a string buffer
 *
 * @param[out] str  Destination buffer to contain formatted string
 * @param[in]  cs   Callsite containing format to use
 * @param[in]  ap   Variable arguments for format
 */
/* suppress suggestion that we currently can do nothing better about
   as the format specification is hidden in cs argument */
#ifdef HAVE_GCC_FORMAT_COMPLAINTS
#pragma GCC diagnostic push
#ifdef HAVE_GCC_MISSING_FORMAT_ATTRIBUTE
#pragma GCC diagnostic ignored "-Wmissing-format-attribute"
#endif
#ifdef HAVE_GCC_SUGGEST_ATTRIBUTE_FORMAT
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#endif
#endif
static inline void
cs_format(char *str, size_t maxlen, struct qb_log_callsite *cs, va_list ap)
{
	va_list ap_copy;
	int len;

	va_copy(ap_copy, ap);
	len = vsnprintf(str, maxlen, cs->format, ap_copy);
	va_end(ap_copy);

	if (len > maxlen) {
		len = maxlen;
	}
	if (str[len - 1] == '\n') {
		str[len - 1] = '\0';
	}
}
#ifdef HAVE_GCC_FORMAT_COMPLAINTS
#pragma GCC diagnostic pop
#endif

void
qb_log_real_va_(struct qb_log_callsite *cs, va_list ap)
{
	int32_t found_threaded = QB_FALSE;
	struct qb_log_target *t;
	struct timespec tv;
	enum qb_log_target_slot pos;
	size_t max_line_length = 0;
	int32_t formatted = QB_FALSE;
	char buf[QB_LOG_MAX_LEN];
	char *str = buf;
	va_list ap_copy;

	if (qb_atomic_int_compare_and_exchange(&in_logger, QB_FALSE, QB_TRUE) == QB_FALSE || cs == NULL) {
		return;
	}

	/* 0 Work out the longest line length available */
	for (pos = QB_LOG_TARGET_START; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if ((t->state == QB_LOG_STATE_ENABLED)
		       && qb_bit_is_set(cs->targets, pos)) {
			if (t->max_line_length > max_line_length) {
				max_line_length = t->max_line_length;
			}
		}
	}

	if (max_line_length > QB_LOG_MAX_LEN) {
		str = malloc(max_line_length);
		if (!str) {
			return;
		}
	}

	if (old_internal_log_fn &&
	    qb_bit_is_set(cs->tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
		if (formatted == QB_FALSE) {
			cs_format(str, max_line_length, cs, ap);
			formatted = QB_TRUE;
		}
		qb_do_extended(str, QB_TRUE,
			old_internal_log_fn(cs->filename, cs->lineno,
					    cs->priority, str));
	}

	qb_util_timespec_from_epoch_get(&tv);

	/*
	 * 1 if we can find a threaded target that needs this log then post it
	 * 2 foreach non-threaded target call it's logger function
	 */
	for (pos = QB_LOG_TARGET_START; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if ((t->state == QB_LOG_STATE_ENABLED)
		    && qb_bit_is_set(cs->targets, pos)) {
			if (t->threaded) {
				if (!found_threaded) {
					found_threaded = QB_TRUE;
					if (formatted == QB_FALSE) {
						cs_format(str, max_line_length, cs, ap);
						formatted = QB_TRUE;
					}
				}

			} else if (t->vlogger) {
				va_copy(ap_copy, ap);
				t->vlogger(t->pos, cs, &tv, ap_copy);
				va_end(ap_copy);

			} else if (t->logger) {
				if (formatted == QB_FALSE) {
					cs_format(str, max_line_length, cs, ap);
					formatted = QB_TRUE;
				}
				qb_do_extended(str, t->extended,
					       t->logger(t->pos, cs, &tv, str));
			}
		}
	}

	if (found_threaded) {
		qb_log_thread_log_post(cs, &tv, str);
	}
	qb_atomic_int_set(&in_logger, QB_FALSE);

	if (max_line_length > QB_LOG_MAX_LEN) {
		free(str);
	}
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
			struct timespec *timestamp, const char *buffer)
{
	struct qb_log_target *t;
	enum qb_log_target_slot pos;

	for (pos = QB_LOG_TARGET_START; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if ((t->state == QB_LOG_STATE_ENABLED) && t->threaded
		    && qb_bit_is_set(cs->targets, t->pos)) {
			qb_do_extended(buffer, t->extended,
				t->logger(t->pos, cs, timestamp, buffer));
		}
	}
}

struct qb_log_callsite*
qb_log_callsite_get2(const char *message_id,
		    const char *function,
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
	enum qb_log_target_slot pos;

	if (!logger_inited) {
		return NULL;
	}

	cs = qb_log_dcs_get(&new_dcs, message_id, function, filename,
 			    format, priority, lineno, tags);

	if (cs == NULL) {
		return NULL;
	}

	if (new_dcs) {
		pthread_rwlock_rdlock(&_listlock);
		for (pos = QB_LOG_TARGET_START; pos <= conf_active_max; pos++) {
			t = &conf[pos];
			if (t->state != QB_LOG_STATE_ENABLED) {
				continue;
			}
			qb_list_for_each(f_item, &t->filter_head) {
				flt = qb_list_entry(f_item, struct qb_log_filter, list);
				_log_filter_apply_to_cs(cs, t->pos, flt->conf, flt->type,
							flt->text, flt->regex, flt->high_priority,
							flt->low_priority);
			}
		}
		if (tags == 0) {
			qb_list_for_each(f_item, &tags_head) {
				flt = qb_list_entry(f_item, struct qb_log_filter, list);
				_log_filter_apply_to_cs(cs, flt->new_value, flt->conf, flt->type,
							flt->text, flt->regex, flt->high_priority,
							flt->low_priority);
			}
		} else {
			cs->tags = tags;
		}
		if (_custom_filter_fn) {
			_custom_filter_fn(cs);
		}
		pthread_rwlock_unlock(&_listlock);
	} else {
	        if (tags && cs->tags != tags) {
		        cs->tags = tags;
		}
		if (_custom_filter_fn) {
			_custom_filter_fn(cs);
		}
	}
	return cs;
}

struct qb_log_callsite*
qb_log_callsite_get(const char *function,
		    const char *filename,
		    const char *format,
		    uint8_t priority,
		    uint32_t lineno,
		    uint32_t tags)
{
	return qb_log_callsite_get2(NULL, function, filename, format,
				    priority, lineno, tags);
}

void
qb_log_from_external_source_va2(const char *message_id,
			       const char *function,
			       const char *filename,
			       const char *format,
			       uint8_t priority,
			       uint32_t lineno, uint32_t tags, va_list ap)
{
	struct qb_log_callsite *cs;

	if (!logger_inited) {
		return;
	}

	cs = qb_log_callsite_get2(message_id, function, filename,
				 format, priority, lineno, tags);
	qb_log_real_va_(cs, ap);
}

void
qb_log_from_external_source_va(const char *function,
			       const char *filename,
			       const char *format,
			       uint8_t priority,
			       uint32_t lineno, uint32_t tags, va_list ap)
{
	qb_log_from_external_source_va2(NULL, function, filename,
				   format, priority, lineno, tags, ap);
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

static void
qb_log_callsites_dump_sect(struct callsite_section *sect)
{
	struct qb_log_callsite *cs;
	printf(" start %p - stop %p\n", sect->start, sect->stop);
	printf("filename    lineno targets         tags\n");
	for (cs = sect->start; cs < sect->stop; cs++) {
		if (cs->lineno > 0) {
#ifndef S_SPLINT_S
			printf("%12s %6" PRIu32 " %16" PRIu32 " %16u\n",
			       cs->filename, cs->lineno, cs->targets,
			       cs->tags);
#endif /* S_SPLINT_S */
		}
	}
}


int32_t
qb_log_callsites_register(struct qb_log_callsite *_start,
			  struct qb_log_callsite *_stop)
{
	struct callsite_section *sect;
	struct qb_log_callsite *cs;
	struct qb_log_target *t;
	struct qb_log_filter *flt;
	enum qb_log_target_slot pos;

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
	for (pos = QB_LOG_TARGET_START; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		if (t->state != QB_LOG_STATE_ENABLED) {
			continue;
		}
		qb_list_for_each_entry(flt, &t->filter_head, list) {
			_log_filter_apply(sect, t->pos, flt->conf,
					  flt->type, flt->text, flt->regex,
					  flt->high_priority, flt->low_priority);
		}
	}
	qb_list_for_each_entry(flt, &tags_head, list) {
		_log_filter_apply(sect, flt->new_value, flt->conf,
				  flt->type, flt->text, flt->regex,
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
		  uint8_t low_priority,
		  struct qb_log_filter **new)
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
			_log_free_filter(flt);
			return -ENOMEM;
		}

		if (type == QB_LOG_FILTER_FUNCTION_REGEX ||
			type == QB_LOG_FILTER_FILE_REGEX ||
			type == QB_LOG_FILTER_FORMAT_REGEX) {
			int res;

			flt->regex = calloc(1, sizeof(regex_t));
			if (flt->regex == NULL) {
				_log_free_filter(flt);
				return -ENOMEM;
			}
			res = regcomp(flt->regex, flt->text, 0);
			if (res) {
				_log_free_filter(flt);
				return -EINVAL;
			}
		}
		flt->high_priority = high_priority;
		flt->low_priority = low_priority;
		flt->new_value = t;
		qb_list_add_tail(&flt->list, list_head);
		if (new) {
			*new = flt;
		}
	} else if (c == QB_LOG_FILTER_REMOVE || c == QB_LOG_TAG_CLEAR) {
		qb_list_for_each_safe(iter, next, list_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			if (flt->type == type &&
			    flt->low_priority <= low_priority &&
			    flt->high_priority >= high_priority &&
			    (strcmp(flt->text, text) == 0 ||
			     strcmp("*", text) == 0)) {
				qb_list_del(iter);
				_log_free_filter(flt);
				return 0;
			}
		}

	} else if (c == QB_LOG_FILTER_CLEAR_ALL || c == QB_LOG_TAG_CLEAR_ALL) {
		qb_list_for_each_safe(iter, next, list_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			qb_list_del(iter);
			_log_free_filter(flt);
		}
	}
	return 0;
}

static void
_log_filter_apply(struct callsite_section *sect,
		  uint32_t t, enum qb_log_filter_conf c,
		  enum qb_log_filter_type type,
		  const char *text,
		  regex_t *regex,
		  uint8_t high_priority, uint8_t low_priority)
{
	struct qb_log_callsite *cs;

	for (cs = sect->start; cs < sect->stop; cs++) {
		if (cs->lineno > 0) {
			_log_filter_apply_to_cs(cs, t, c, type, text, regex,
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
			regex_t *regex,
			uint8_t high_priority, uint8_t low_priority)
{

	if (c == QB_LOG_FILTER_CLEAR_ALL) {
		qb_bit_clear(cs->targets, t);
		return;
	} else if (c == QB_LOG_TAG_CLEAR_ALL) {
		cs->tags = 0;
		return;
	}

	if (_cs_matches_filter_(cs, type, text, regex, high_priority, low_priority)) {
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
	struct qb_log_filter *new_flt = NULL;
	regex_t *regex = NULL;
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
	    type > QB_LOG_FILTER_FORMAT_REGEX ||
	    c > QB_LOG_TAG_CLEAR_ALL) {
		return -EINVAL;
	}
	pthread_rwlock_rdlock(&_listlock);
	rc = _log_filter_store(t, c, type, text, high_priority, low_priority, &new_flt);
	if (rc < 0) {
		pthread_rwlock_unlock(&_listlock);
		return rc;
	}

	if (new_flt && new_flt->regex) {
		regex = new_flt->regex;
	}
	qb_list_for_each_entry(sect, &callsite_sections, list) {
		_log_filter_apply(sect, t, c, type, text, regex, high_priority, low_priority);
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

static void
_log_target_state_set(struct qb_log_target *t, enum qb_log_target_state s)
{
	enum qb_log_target_slot i;
	int32_t a_set = QB_FALSE;
	int32_t u_set = QB_FALSE;

	t->state = s;

	for (i = QB_LOG_TARGET_MAX; i > QB_LOG_TARGET_START; i--) {
		if (!a_set && conf[i-1].state == QB_LOG_STATE_ENABLED) {
			a_set = QB_TRUE;
			conf_active_max = i-1;
		}
		if (!u_set && conf[i-1].state != QB_LOG_STATE_UNUSED) {
			u_set = QB_TRUE;
		}
	}
}

void
qb_log_init(const char *name, int32_t facility, uint8_t priority)
{
	int32_t l;
	enum qb_log_target_slot i;

	l = pthread_rwlock_init(&_listlock, NULL);
	assert(l == 0);
	qb_log_format_init();

	for (i = QB_LOG_TARGET_START; i < QB_LOG_TARGET_MAX; i++) {
		conf[i].pos = i;
		conf[i].debug = QB_FALSE;
		conf[i].file_sync = QB_FALSE;
		conf[i].extended = QB_TRUE;
		conf[i].state = QB_LOG_STATE_UNUSED;
		(void)strlcpy(conf[i].name, name, PATH_MAX);
		conf[i].facility = facility;
		conf[i].max_line_length = QB_LOG_MAX_LEN;
		qb_list_init(&conf[i].filter_head);
	}

	qb_log_dcs_init();

	for (i = QB_LOG_TARGET_STATIC_START; i < QB_LOG_TARGET_STATIC_MAX; i++)
		conf[i].state = QB_LOG_STATE_DISABLED;

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
	enum qb_log_target_slot pos;

	if (!logger_inited) {
		return;
	}
	logger_inited = QB_FALSE;
	qb_log_thread_stop();
	pthread_rwlock_destroy(&_listlock);

	for (pos = QB_LOG_TARGET_START; pos <= conf_active_max; pos++) {
		t = &conf[pos];
		_log_target_disable(t);
		qb_list_for_each_safe(iter2, next2, &t->filter_head) {
			flt = qb_list_entry(iter2, struct qb_log_filter, list);
			qb_list_del(iter2);
			_log_free_filter(flt);
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
		_log_free_filter(flt);
	}
}

struct qb_log_target *
qb_log_target_alloc(void)
{
	enum qb_log_target_slot i;
	for (i = QB_LOG_TARGET_START; i < QB_LOG_TARGET_MAX; i++) {
		if (conf[i].state == QB_LOG_STATE_UNUSED) {
			_log_target_state_set(&conf[i], QB_LOG_STATE_DISABLED);
			return &conf[i];
		}
	}
	errno = EMFILE;
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
		errno = EBADF;
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
#ifndef S_SPLINT_S
	snprintf(t->filename, PATH_MAX, "custom-%" PRIu32, t->pos);
#endif /* S_SPLINT_S */

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
		qb_atomic_int_set(&in_logger, QB_TRUE);
		target->close(t);
		qb_atomic_int_set(&in_logger, QB_FALSE);
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
		qb_atomic_int_set(&in_logger, QB_TRUE);
		t->close(t->pos);
		qb_atomic_int_set(&in_logger, QB_FALSE);
	}
}

int32_t
qb_log_ctl2(int32_t t, enum qb_log_conf c, qb_log_ctl2_arg_t arg_not4directuse)
{
	int32_t rc = 0;
	int32_t need_reload = QB_FALSE;

	/* extract the constants and do not touch the origin anymore */
	const int32_t arg_i32 = arg_not4directuse.i32;
	const char * const arg_s = arg_not4directuse.s;

	if (!logger_inited) {
		return -EINVAL;
	}
	if (t < 0 || t >= QB_LOG_TARGET_MAX ||
	    conf[t].state == QB_LOG_STATE_UNUSED) {
		return -EBADF;
	}

	/* Starting/stopping the thread has its own locking that can interfere with this */
	if (c != QB_LOG_CONF_THREADED) {
		qb_log_thread_pause(&conf[t]);
	}

	switch (c) {
	case QB_LOG_CONF_ENABLED:
		if (arg_i32) {
			rc = _log_target_enable(&conf[t]);
		} else {
			_log_target_disable(&conf[t]);
		}
		break;
	case QB_LOG_CONF_STATE_GET:
		rc = conf[t].state;
		break;
	case QB_LOG_CONF_FACILITY:
		conf[t].facility = arg_i32;
		if (t == QB_LOG_SYSLOG) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_IDENT:
		(void)strlcpy(conf[t].name, arg_s, PATH_MAX);
		if (t == QB_LOG_SYSLOG) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_FILE_SYNC:
		conf[t].file_sync = arg_i32;
		break;
	case QB_LOG_CONF_PRIORITY_BUMP:
		conf[t].priority_bump = arg_i32;
		break;
	case QB_LOG_CONF_SIZE:
		if (t == QB_LOG_BLACKBOX) {
			if (arg_i32 <= 0) {
				rc = -EINVAL;
				goto unlock_fini;
			}
			conf[t].size = arg_i32;
			need_reload = QB_TRUE;
		} else {
			rc = -ENOSYS;
		}
		break;
	case QB_LOG_CONF_THREADED:
		conf[t].threaded = arg_i32;
		break;
	case QB_LOG_CONF_EXTENDED:
		conf[t].extended = arg_i32;
		break;
	case QB_LOG_CONF_MAX_LINE_LEN:
		/* arbitrary limit, but you'd be insane to go further */
		if (arg_i32 > QB_LOG_ABSOLUTE_MAX_LEN) {
			rc = -EINVAL;
		} else {
			conf[t].max_line_length = arg_i32;
		}
		break;
	case QB_LOG_CONF_ELLIPSIS:
		conf[t].ellipsis = arg_i32;
		break;
	case QB_LOG_CONF_USE_JOURNAL:
#ifdef USE_JOURNAL
		if (t == QB_LOG_SYSLOG) {
			conf[t].use_journal = arg_i32;
			need_reload = QB_TRUE;
		} else {
			rc = -EINVAL;
		}
#else
		rc = -EOPNOTSUPP;
#endif
		break;

	default:
		rc = -EINVAL;
	}
	if (rc == 0 && need_reload && conf[t].reload) {
		qb_atomic_int_set(&in_logger, QB_TRUE);
		conf[t].reload(t);
		qb_atomic_int_set(&in_logger, QB_FALSE);
	}

unlock_fini:
	if (c != QB_LOG_CONF_THREADED) {
		qb_log_thread_resume(&conf[t]);
	}
	return rc;
}

int32_t
qb_log_ctl(int32_t t, enum qb_log_conf c, int32_t arg)
{
	return qb_log_ctl2(t, c, QB_LOG_CTL2_I32(arg));
}
