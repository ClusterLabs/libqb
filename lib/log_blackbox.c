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
#include <qb/qblog.h>

qb_ringbuffer_t *bb_rb = NULL;

void qb_log_blackbox_start(size_t size)
{
	bb_rb = qb_rb_open("blackbox", size,
			   QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
}

/* <u32> file lineno
 * <u32> function name length
 * <string> function name
 * <u32> buffer lenght
 * <string> buffer
 */
void qb_log_blackbox_append(struct qb_log_callsite *cs,
			    const char *buffer)
{
	size_t size = sizeof(uint32_t);
	size_t fn_size;
	size_t buf_size;
	char *chunk;

	fn_size = strlen(cs->function) + 1;
	buf_size = strlen(buffer) + 1;

	size += 2 * sizeof(uint32_t) + fn_size + buf_size;

	chunk = qb_rb_chunk_alloc(bb_rb, size);

	/* line number */
	memcpy(chunk, &cs->lineno, sizeof(uint32_t));
	chunk += sizeof(uint32_t);

	/* function name */
	memcpy(chunk, &fn_size, sizeof(uint32_t));
	chunk += sizeof(uint32_t);
	memcpy(chunk, cs->function, fn_size);
	chunk += fn_size;

	/* log message */
	memcpy(chunk, &buf_size, sizeof(uint32_t));
	chunk += sizeof(uint32_t);
	memcpy(chunk, buffer, buf_size);

	qb_rb_chunk_commit(bb_rb, size);
}

ssize_t qb_log_blackbox_write_to_file(const char *filename)
{
	ssize_t written_size = 0;
	int fd = open (filename, O_CREAT|O_RDWR, 0700);

	if (fd < 0) {
		return -errno;
	}
	written_size = qb_rb_write_to_file(bb_rb, fd);
	close (fd);

	return written_size;
}

void qb_log_blackbox_print_from_file(const char* bb_filename)
{
	qb_ringbuffer_t * rb;
	ssize_t bytes_read;
	char chunk[512];
	int fd;

	fd = open(bb_filename, O_CREAT|O_RDWR, 0700);
	if (fd < 0) {
		perror("qb_log_blackbox_print_from_file");
		return;
	}
	rb = qb_rb_create_from_file(fd, 0);

	do {
		char     *ptr;
		uint32_t *lineno;
		uint32_t *fn_size;
		char     *function;
		uint32_t *log_size;
		char     *logmsg;

		bytes_read = qb_rb_chunk_read(rb, chunk, 512, 0);
		ptr = chunk;
		if (bytes_read > 0) {
			lineno = (uint32_t*)ptr;
			ptr  += sizeof(uint32_t);

			fn_size = (uint32_t*)ptr;
			ptr += sizeof(uint32_t);

			function = ptr;
			ptr += *fn_size;

			log_size = (uint32_t*)ptr;
			ptr += sizeof(uint32_t);
			logmsg = ptr;
			printf("%s():%d %s\n", function, *lineno, logmsg);
		}
	} while (bytes_read > 0);
}

