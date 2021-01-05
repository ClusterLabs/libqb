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
#ifndef _QB_LOG_INT_H_
#define _QB_LOG_INT_H_

#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbrb.h>
#include <regex.h>

struct qb_log_target;

struct qb_log_target {
	uint32_t pos;
	enum qb_log_target_state state;
	char name[PATH_MAX];
	char filename[PATH_MAX];
	struct qb_list_head filter_head;
	int32_t facility;
	int32_t priority_bump;
	int32_t file_sync;
	int32_t debug;
	int32_t extended;
	int32_t use_journal;
	size_t size;
	size_t max_line_length;
	int32_t ellipsis;
	char *format;
	int32_t threaded;
	void *instance;

	qb_log_reload_fn reload;
	qb_log_close_fn close;
	qb_log_logger_fn logger;
	qb_log_vlogger_fn vlogger;
};

struct qb_log_filter {
	enum qb_log_filter_conf conf;
	enum qb_log_filter_type type;
	char *text;
	uint8_t high_priority;
	uint8_t low_priority;
	uint32_t new_value;
	struct qb_list_head list;
	regex_t *regex;
};

struct qb_log_record {
	struct qb_log_callsite *cs;
	struct timespec timestamp;
	char *buffer;
	struct qb_list_head list;
};


#define TIME_STRING_SIZE 64

/**
 * @internal
 * @brief Call a log function, handling any extended information marker
 *
 * If the string to be passed to the log function contains an extended
 * information marker, temporarily modify the string to strip the extended
 * information if appropriate. Special cases: if a marker occurs with nothing
 * after it, it will always be stripped; if only extended information is
 * present, stmt will be called only if extended is true.
 *
 * @param[in]  str       Null-terminated log message
 * @param[in]  extended  QB_TRUE if extended information should be printed
 * @param[in]  stmt      Code block to call log function
 *
 * @note Because this is a macro, none of the arguments other than stmt should
 *       have side effects.
 */
#define qb_do_extended(str, extended, stmt) do { \
	char *qb_xc = strchr((str), QB_XC); \
	if (qb_xc) { \
		if ((qb_xc != (str)) || (extended)) { \
			*qb_xc = ((extended) && *(qb_xc + 1))? '|' : '\0'; \
			stmt; \
			*qb_xc = QB_XC; \
		} \
	} else { \
		stmt; \
	} \
} while (0)

struct qb_log_target * qb_log_target_alloc(void);
void qb_log_target_free(struct qb_log_target *t);
struct qb_log_target * qb_log_target_get(int32_t pos);

int32_t qb_log_syslog_open(struct qb_log_target *t);
int32_t qb_log_stderr_open(struct qb_log_target *t);
int32_t qb_log_blackbox_open(struct qb_log_target *t);

void qb_log_thread_stop(void);
void qb_log_thread_log_post(struct qb_log_callsite *cs,
			    struct timespec *current_time,
			    const char *buffer);
void qb_log_thread_log_write(struct qb_log_callsite *cs,
			    struct timespec *current_time,
			    const char *buffer);
void qb_log_thread_pause(struct qb_log_target *t);
void qb_log_thread_resume(struct qb_log_target *t);

void qb_log_dcs_init(void);
void qb_log_dcs_fini(void);
struct qb_log_callsite *qb_log_dcs_get(int32_t *newly_created,
				       const char *message_id,
				       const char *function,
				       const char *filename,
				       const char *format,
				       uint8_t priority,
				       uint32_t lineno,
				       uint32_t tags);

void qb_log_format_init(void);
void qb_log_format_fini(void);
const char * qb_log_priority2str(uint8_t priority);
size_t qb_vsnprintf_serialize(char *serialize, size_t max_len, const char *fmt, va_list ap);
size_t qb_vsnprintf_deserialize(char *string, size_t str_len, const char *buf);

void qb_log_target_format_static(int32_t target, const char * format, char *output_buffer);

#endif /* _QB_LOG_INT_H_ */

