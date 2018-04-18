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

#include "os_base.h"
#include <qb/qblog.h>

#ifndef QB_UTIL_INT_INLINECOMPAT
# define QB_UTIL_INT_INLINECOMPAT static inline
#endif

#if !defined (va_copy)
#if defined (__va_copy)
#define va_copy(_a, _b) __va_copy(_a, _b)
#else
#define va_copy(_a, _b)  memcpy(&_a, &_b, sizeof(va_list))
#endif /* !__va_copy */
#endif /* !va_copy */

/**
 * This is used internally by libqb.
 *
 * It sets the 32nd bit of the tags so that internal logs can be
 * distinguished from external ones.
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
int32_t qb_sys_mmap_file_open(char *path, const char *file, size_t bytes,
			       uint32_t file_flags);

/**
 * Create a shared mamory circular buffer.
 *
 * @param fd an open file to use to back the shared memory.
 * @param buf (out) the pointer to the start of the memory.
 * @param bytes the size of the shared memory.
 * @return 0 (success) or -errno
 */
int32_t qb_sys_circular_mmap(int32_t fd, void **buf, size_t bytes);


/**
 * Set O_NONBLOCK and FD_CLOEXEC on a file descriptor.
 * @param fd the file descriptor.
 * @return 0 (success) or -errno
 */
int32_t qb_sys_fd_nonblock_cloexec_set(int32_t fd);

/**
 * Try to unlink file, and possibly truncate it as a fallback.
 * @param path the file to be unlinked or truncated.
 * @param truncate_fallback whether to truncate the file when unlink fails.
 * @return 0 (success) or -errno
 */
int32_t qb_sys_unlink_or_truncate(const char *path, int32_t truncate_fallback);

#if defined(HAVE_OPENAT) && defined(HAVE_UNLINKAT)
/**
 * Try to unlinkat file, and possibly truncate it as a fallback ("at" variant).
 * @param path the file to be unlinked or truncated.
 * @param truncate_fallback whether to truncate the file when unlink fails.
 * @return 0 (success) or -errno
 */
int32_t qb_sys_unlink_or_truncate_at(int32_t dirfd, const char *path,
				     int32_t truncate_fallback);
#endif

enum qb_sigpipe_ctl {
       QB_SIGPIPE_IGNORE,
       QB_SIGPIPE_DEFAULT,
};

/* Following two are "weak" helpers, extracted common parts of what the
   other internal functions use -- in a quite rapid cadence, hence we
   mark them inline to prevent construction of sometimes even completely
   empty frames, while retaining the idea of an interface through function
   declaration (there's just a single implemention thereof: unix*.[ch]) */

/**
 * Control sigpipe (ignore/default) during send/recv
 * Needed on some bsd's
 */
QB_UTIL_INT_INLINECOMPAT void qb_sigpipe_ctl(enum qb_sigpipe_ctl ctl);

/**
 * Control sigpipe on the socket.
 */
QB_UTIL_INT_INLINECOMPAT void qb_socket_nosigpipe(int32_t s);

#define SERVER_BACKLOG 128

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX    108
#endif /* UNIX_PATH_MAX */

/*
 * SUN_LEN() does a strlen() on sun_path, but if you are trying to use the
 * "Linux abstract namespace" (you have to set sun_path[0] == '\0') then
 * the strlen() doesn't work.
 */
#if defined(SUN_LEN)
#define QB_SUN_LEN(a) ((a)->sun_path[0] == '\0') ? sizeof(*(a)) : SUN_LEN(a)
#else
#define QB_SUN_LEN(a) sizeof(*(a))
#endif

#endif /* QB_UTIL_INT_H_DEFINED */
