/*
 * Copyright 2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Pokorny <jpokorny@redhat.com>
 *
 * This file is part of libqb.
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
#include <qb/qblog.h>

#ifndef NSELFCHECK
QB_LOG_INIT_DATA(linker_contra_log);
#endif

static const char *
my_tags_stringify(uint32_t tags)
{
	if (qb_bit_is_set(tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
		return "libqb";
	} else {
		return "MAIN";
	}
}

int32_t
main(int32_t argc, char *argv[])
{
	int tmpfile_fd;
	struct stat tmpfile_stat;
	char *tmpfile_buf = strdup("linker-log-XXXXXX");

	qb_log_init("linker-contra-log", LOG_USER, LOG_INFO);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	qb_log_tags_stringify_fn_set(my_tags_stringify);
	qb_log_format_set(QB_LOG_STDERR, "[%5g|%p] %f:%l:%b");

#if 0
	printf("--\n");
	qb_log_callsites_dump();
	printf("--\n");
#endif

#ifndef NLOG
	/* Casual test of "user-space" logging. */
	qb_log(LOG_DEBUG, "hello");
#endif

	/* And now of "library-space" logging, i.e., let libqb generated
	   an error message on its own behalf, first to see if it will be
	   logged at all, second if it will be distinguished properly.
	   The trigger here is as simple as trying to print non-existing
	   blackbox file. */
	tmpfile_fd = mkstemp(tmpfile_buf);
	if (tmpfile_fd == -1) {
		qb_perror(LOG_ERR, "creating temporary file");
		exit(EXIT_FAILURE);
	}
	unlink(tmpfile_buf);
	close(tmpfile_fd);
#if 0
	if (stat(tmpfile_buf, &tmpfile_stat) == -1) {
		qb_perror(LOG_ERR, "stat'ing nonexistent temporary file");
		exit(EXIT_FAILURE);
	}
#endif
	qb_log_blackbox_print_from_file(tmpfile_buf);
	free(tmpfile_buf);
	qb_log_fini();
}
