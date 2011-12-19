/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
#ifndef QB_OS_BASE_H_DEFINED
#define QB_OS_BASE_H_DEFINED

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif /* _GNU_SOURCE */

#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#include <sys/uio.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <assert.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif /* HAVE_STRINGS_H */

#ifndef S_SPLINT_S
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#endif /* S_SPLINT_S */

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif /*HAVE_ERRNO_H*/

#ifdef HAVE_TIME_H
#include <time.h>
#endif /* HAVE_TIME_H */

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif /* HAVE_SYS_STAT_H */

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif /* HAVE_FCNTL_H */

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */

#ifndef S_SPLINT_S
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif /* HAVE_SYSLOG_H */
#endif /* S_SPLINT_S */

#if defined HAVE_CLOCK_GETTIME && defined _POSIX_MONOTONIC_CLOCK && _POSIX_MONOTONIC_CLOCK >= 0
#define HAVE_MONOTONIC_CLOCK 1
#endif /* have monotonic clock */

#ifdef HAVE_TIMERFD_CREATE
#define HAVE_TIMERFD 1
#endif /* HAVE_TIMERFD_CREATE */

#ifdef HAVE_EPOLL_CREATE1
#define HAVE_EPOLL 1
#endif /* HAVE_EPOLL_CREATE */

/*
 * Darwin claims to support process shared synchronization
 * but it really does not.  The unistd.h header file is wrong.
 */
#if defined(DISABLE_POSIX_THREAD_PROCESS_SHARED) || defined(__UCLIBC__)
#undef HAVE_POSIX_SHARED_SEMAPHORE
#undef HAVE_PTHREAD_SHARED_SPIN_LOCK
#else
#if _POSIX_THREAD_PROCESS_SHARED > 0
#define HAVE_POSIX_SHARED_SEMAPHORE 1

#if defined(HAVE_PTHREAD_SPIN_LOCK)
#define HAVE_PTHREAD_SHARED_SPIN_LOCK 1
#endif /* HAVE_PTHREAD_SPIN_LOCK */

#endif /* _POSIX_THREAD_PROCESS_SHARED */
#endif /* DISABLE_POSIX_THREAD_PROCESS_SHARED */

#ifdef QB_DARWIN
char *strchrnul (const char *s, int c_in);
#endif

#endif /* QB_OS_BASE_H_DEFINED */
