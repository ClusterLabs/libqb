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

#include <qb/qbrb.h>
#include "log_int.h"

static void _file_logger(struct qb_log_target *t,
				 struct qb_log_callsite *cs,
				 const char* timestamp_str,
				 const char *msg)
{
	fprintf(t->instance, "%s %s:%d [%s] %s\n", timestamp_str, cs->filename,
		cs->lineno, qb_log_priority_name_get(cs->priority), msg);
}

static void _file_close(struct qb_log_target *t)
{
	if (t->instance) {
		fclose(t->instance);
	}
}

static void _file_reload(struct qb_log_target *t)
{
	if (t->instance) {
		fclose(t->instance);
	}
	t->instance = fopen(t->name, "a+");
}

int32_t qb_log_stderr_open(struct qb_log_target *t)
{
	t->logger = _file_logger;
	t->reload = NULL;
	t->close = NULL;
	strncpy(t->name, "stderr", PATH_MAX);
	t->instance = stderr;
	return 0;
}

int32_t qb_log_file_open(const char *filename)
{
	struct qb_log_target *t;
	FILE* fp;
	int32_t rc;

	t = qb_log_target_alloc();
	if (t == NULL) {
		return -errno;
	}

	fp = fopen(filename, "a+");
	if (fp == NULL) {
		rc = -errno;
		qb_log_target_free(t);
		return rc;
	}
	t->instance = fp;
	strncpy(t->name, filename, PATH_MAX);

	t->logger = _file_logger;
	t->reload = _file_reload;
	t->close = _file_close;
	t->state = QB_LOG_STATE_ENABLED;
	return t->pos;
}

void qb_log_file_close(int32_t t)
{
	struct qb_log_target * target = qb_log_target_get(t);
	target->close(target);
	qb_log_target_free(target);
}

