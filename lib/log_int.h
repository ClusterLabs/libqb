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

struct qb_log_filter_file {
	char *filename;
	int32_t start;
	int32_t end;
	struct qb_list_head list;
};

struct qb_log_filter {
	uint8_t priority;
	struct qb_list_head files_head;
};

struct qb_log_destination {
	qb_log_logger_fn logger;
	int32_t threaded;
};

struct qb_log_record {
	struct qb_log_callsite *cs;
	char *buffer;
	struct qb_list_head list;
};

#define COMBINE_BUFFER_SIZE 256

extern struct qb_log_destination *destination;

void qb_log_thread_log_post(struct qb_log_callsite *cs,
		      const char *buffer);


#endif /* _QB_LOG_INT_H_ */

