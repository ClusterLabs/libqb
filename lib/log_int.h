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
	size_t size;
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
};

struct qb_log_record {
	struct qb_log_callsite *cs;
	time_t timestamp;
	char *buffer;
	struct qb_list_head list;
};


#define TIME_STRING_SIZE 64

struct qb_log_target * qb_log_target_alloc(void);
void qb_log_target_free(struct qb_log_target *t);
struct qb_log_target * qb_log_target_get(int32_t pos);

int32_t qb_log_syslog_open(struct qb_log_target *t);
int32_t qb_log_stderr_open(struct qb_log_target *t);
int32_t qb_log_blackbox_open(struct qb_log_target *t);

void qb_log_thread_stop(void);
void qb_log_thread_log_post(struct qb_log_callsite *cs,
			    time_t current_time,
			    const char *buffer);
void qb_log_thread_log_write(struct qb_log_callsite *cs,
			    time_t current_time,
			    const char *buffer);

void qb_log_dcs_init(void);
void qb_log_dcs_fini(void);
struct qb_log_callsite *qb_log_dcs_get(int32_t *newly_created,
				       const char *function,
				       const char *filename,
				       const char *format,
				       uint8_t priority,
				       uint32_t lineno,
				       uint32_t tags);

const char * qb_log_priority2str(uint8_t priority);
size_t qb_vsnprintf_serialize(char *serialize, size_t max_len, const char *fmt, va_list ap);
size_t qb_vsnprintf_deserialize(char *string, size_t str_len, const char *buf);

void qb_log_target_format_static(int32_t target, const char * format, char *output_buffer);

#endif /* _QB_LOG_INT_H_ */

