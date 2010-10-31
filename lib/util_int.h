/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef QB_UTIL_INT_H_DEFINED
#define QB_UTIL_INT_H_DEFINED

#include <syslog.h>

#define qb_util_log(severity, args...)				\
do {							\
	_qb_util_log (__FILE__,  __LINE__, severity, ##args);	\
} while(0)

void _qb_util_log(const char *file_name,
		  int32_t file_line,
		  int32_t severity,
		  const char *format,
		  ...) __attribute__ ((format(printf, 4, 5)));

/**
 * Create a file to be used to back shared memory.
 *
 * @param path (out) the final absolute path of the file.
 * @param file (in) the name of the file to be used.
 * @param bytes the size to truncate the file to.
 * @param file_flags same as passed into open()
 * @return 0 (success) or -1 (error)
 */
int32_t qb_util_mmap_file_open(char *path, const char *file, size_t bytes,
			       uint32_t file_flags);

/**
 * Create a shared mamory circular buffer.
 *
 * @param fd an open file to use to back the shared memory.
 * @param buf (out) the pointer to the start of the memory.
 * @param bytes the size of the shared memory.
 * @return 0 (success) or -1 (error)
 */
int32_t qb_util_circular_mmap(int32_t fd, void **buf, size_t bytes);

#endif /* QB_UTIL_INT_H_DEFINED */
