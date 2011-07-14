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

#include <qb/qblog.h>

/**
 * This is used internally by libqb.
 *
 * It sets the 32nd bit of the tags so that internal logs can be
 * destinguished from external ones.
 */
#ifndef S_SPLINT_S
#define qb_util_log(priority, fmt, args...) qb_logt(priority, QB_LOG_TAG_LIBQB_MSG, fmt, ##args)
#else
#define qb_util_log
#endif

#ifndef S_SPLINT_S
#define qb_util_perror(priority, fmt, args...) do {		\
	char _perr_buf_[QB_LOG_STRERROR_MAX_LEN];			\
	const char *_perr_str_ = qb_strerror_r(errno, _perr_buf_, sizeof(_perr_buf_));	\
	qb_logt(priority, QB_LOG_TAG_LIBQB_MSG, fmt ": %s (%d)", ##args, _perr_str_, errno); \
    } while(0)
#else
#define qb_util_perror
#endif

/**
 * Create a file to be used to back shared memory.
 *
 * @param path (out) the final absolute path of the file.
 * @param file (in) the name of the file to be used.
 * @param bytes the size to truncate the file to.
 * @param file_flags same as passed into open()
 * @return 0 (success) or -errno
 */
int32_t qb_util_mmap_file_open(char *path, const char *file, size_t bytes,
			       uint32_t file_flags);

/**
 * Create a shared mamory circular buffer.
 *
 * @param fd an open file to use to back the shared memory.
 * @param buf (out) the pointer to the start of the memory.
 * @param bytes the size of the shared memory.
 * @return 0 (success) or -errno
 */
int32_t qb_util_circular_mmap(int32_t fd, void **buf, size_t bytes);


/**
 * Set O_NONBLOCK and FD_CLOEXEC on a file descriptor.
 * @param fd the file descriptor.
 * @return 0 (success) or -errno
 */
int32_t qb_util_fd_nonblock_cloexec_set(int32_t fd);

#endif /* QB_UTIL_INT_H_DEFINED */
