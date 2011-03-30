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

enum qb_log_state {
	QB_LOG_STATE_UNUSED,
	QB_LOG_STATE_DISABLED,
	QB_LOG_STATE_ENABLED,
};

struct qb_log_target;

typedef void (*qb_log_logger_fn)(struct qb_log_target *t,
				 struct qb_log_callsite *cs,
				 const char* timestamp_str,
				 const char *msg);

typedef void (*qb_log_close_fn)(struct qb_log_target *t);
typedef void (*qb_log_reload_fn)(struct qb_log_target *t);

struct qb_log_target {
	uint32_t pos;
	enum qb_log_state state;
	char name[PATH_MAX];
	struct qb_list_head filter_head;
	int32_t facility;
	int32_t debug;
	size_t size;
	int32_t threaded;
	void *instance;

	qb_log_reload_fn reload;
	qb_log_close_fn close;
	qb_log_logger_fn logger;
};

struct qb_log_filter {
	enum qb_log_filter_type type;
	char *text;
	int32_t priority;
	struct qb_list_head list;
};


struct qb_log_record {
	struct qb_log_callsite *cs;
	char *timestamp;
	char *buffer;
	struct qb_list_head list;
};

#define COMBINE_BUFFER_SIZE 256
struct qb_log_target * qb_log_target_alloc(void);
void qb_log_target_free(struct qb_log_target *t);
struct qb_log_target * qb_log_target_get(int32_t pos);

int32_t qb_log_syslog_open(struct qb_log_target *t);
int32_t qb_log_stderr_open(struct qb_log_target *t);
int32_t qb_log_blackbox_open(struct qb_log_target *t);

void qb_log_thread_log_post(struct qb_log_callsite *cs,
			    const char* timestamp_str,
			    const char *buffer);

void qb_log_thread_log_write(struct qb_log_callsite *cs,
			    const char* timestamp_str,
			    const char *buffer);

#endif /* _QB_LOG_INT_H_ */

