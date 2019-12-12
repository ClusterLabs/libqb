/*
 * Copyright (C) 2011-2018 Red Hat, Inc.
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
	     struct qb_log_callsite *cs, struct timespec *timestamp, const char *msg)
{
	char buffer[QB_LOG_MAX_LEN];
	char *output_buffer = buffer;
	struct qb_log_target *target = qb_log_target_get(t);
	FILE *f = qb_log_target_user_data_get(t);

	if (f == NULL) {
		return;
	}
	if (target->max_line_length > QB_LOG_MAX_LEN) {
		output_buffer = malloc(target->max_line_length);
		if (!output_buffer) {
			return;
		}
	}
	output_buffer[0] = '\0';

	qb_log_target_format(t, cs, timestamp, msg, output_buffer);

	fprintf(f, "%s\n", output_buffer);

	fflush(f);
	if (target->file_sync) {
		QB_FILE_SYNC(fileno(f));
	}
	if (target->max_line_length > QB_LOG_MAX_LEN) {
		free(output_buffer);
	}
}

static void
_file_close(int32_t t)
{
	FILE *f = qb_log_target_user_data_get(t);

	if (f) {
		(void)qb_log_target_user_data_set(t, NULL);
		fclose(f);
	}
}

static int
_do_file_reload(const char *filename, int32_t target)
{
	struct qb_log_target *t = qb_log_target_get(target);
	FILE *oldfile = qb_log_target_user_data_get(target);
	FILE *newfile;
	int saved_errno;
	int rc;

	if (filename == NULL) {
		filename = t->filename;
	}
	newfile = fopen(filename, "a+");
	saved_errno = errno;

	qb_log_thread_pause(t);

	if (newfile) {
		/* Only close oldfile if newfile open succeeds */
		if (oldfile) {
			fclose(oldfile);
		}

		if (filename != t->filename) {
			(void)strlcpy(t->filename, filename, PATH_MAX);
		}
		(void)qb_log_target_user_data_set(target, newfile);
		rc = 0;
	}
	else {
		rc = -saved_errno;
	}
	qb_log_thread_resume(t);

	return rc;
}

/* The version called from the logger object */
static void
_file_reload(int32_t target)
{
	(void)_do_file_reload(NULL, target);
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

int32_t
qb_log_file_reopen(int32_t t, const char *filename)
{
	return _do_file_reload(filename, t);
}
