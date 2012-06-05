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
#include "log_int.h"

static void
_file_logger(int32_t t,
	     struct qb_log_callsite *cs, time_t timestamp, const char *msg)
{
	char output_buffer[QB_LOG_MAX_LEN];
	struct qb_log_target *target = qb_log_target_get(t);
	FILE *f = qb_log_target_user_data_get(t);

	if (f == NULL) {
		return;
	}
	output_buffer[0] = '\0';

	qb_log_target_format(t, cs, timestamp, msg, output_buffer);

	fprintf(f, "%s\n", output_buffer);

	fflush(f);
	if (target->file_sync) {
		fsync(fileno(f));
	}
}

static void
_file_close(int32_t t)
{
	FILE *f = qb_log_target_user_data_get(t);

	if (f) {
		fclose(f);
		(void)qb_log_target_user_data_set(t, NULL);
	}
}

static void
_file_reload(int32_t target)
{
	struct qb_log_target *t = qb_log_target_get(target);

	if (t->instance) {
		fclose(t->instance);
	}
	t->instance = fopen(t->filename, "a+");
}

int32_t
qb_log_stderr_open(struct qb_log_target *t)
{
	t->logger = _file_logger;
	t->reload = NULL;
	t->close = NULL;
	if (t->pos == QB_LOG_STDERR) {
		(void)strlcpy(t->filename, "stderr", PATH_MAX);
		t->instance = stderr;
	} else {
		(void)strlcpy(t->filename, "stdout", PATH_MAX);
		t->instance = stdout;
	}
	return 0;
}

int32_t
qb_log_file_open(const char *filename)
{
	struct qb_log_target *t;
	FILE *fp;
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
	(void)strlcpy(t->filename, filename, PATH_MAX);

	t->logger = _file_logger;
	t->reload = _file_reload;
	t->close = _file_close;
	return t->pos;
}

void
qb_log_file_close(int32_t t)
{
	qb_log_custom_close(t);
}
