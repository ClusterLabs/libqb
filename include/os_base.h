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

#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif /* HAVE_SYS_UIO_H */

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif /* HAVE_STDDEF_H */

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

#ifdef HAVE_EPOLL_CREATE1
#define HAVE_EPOLL 1
#endif /* HAVE_EPOLL_CREATE */

#if defined(__UCLIBC__)
 #define DISABLE_POSIX_THREAD_PROCESS_SHARED 1
#endif

/* The pshared semaphore madness:
 * To have a usable pshared semaphore we need the timed_wait api
 * and pshared functionality.
 *
 * The order of choice is:
 * 1) real posix sem -> HAVE_POSIX_PSHARED_SEMAPHORE
 * 2) sysv sems (if we have semtimedop) -> HAVE_SYSV_PSHARED_SEMAPHORE
 * 3) faked sems using pthread_cond_timedwait -> HAVE_RPL_PSHARED_SEMAPHORE
 * 4) ENOTSUP
 */
#undef HAVE_POSIX_PSHARED_SEMAPHORE
#undef HAVE_SYSV_PSHARED_SEMAPHORE
#undef HAVE_RPL_PSHARED_SEMAPHORE

#if defined(DISABLE_POSIX_THREAD_PROCESS_SHARED)
 #undef HAVE_PTHREAD_SHARED_SPIN_LOCK
#endif /* DISABLE_POSIX_THREAD_PROCESS_SHARED */

#if  !defined(DISABLE_POSIX_THREAD_PROCESS_SHARED) && \
     _POSIX_THREAD_PROCESS_SHARED > 0

  #if defined(HAVE_PTHREAD_SPIN_LOCK)
  #define HAVE_PTHREAD_SHARED_SPIN_LOCK 1
  #endif /* HAVE_PTHREAD_SPIN_LOCK */

  #if  defined(HAVE_SEM_TIMEDWAIT)
  #define HAVE_POSIX_PSHARED_SEMAPHORE 1
  #else
    #if defined(HAVE_PTHREAD_CONDATTR_SETPSHARED) && \
        defined(HAVE_PTHREAD_MUTEXATTR_SETPSHARED)
    #define HAVE_RPL_PSHARED_SEMAPHORE 1
    #endif
  #endif /* HAVE_SEM_TIMEDWAIT */
#endif /* posix pshared */

#ifdef HAVE_SEMTIMEDOP
#define HAVE_SYSV_PSHARED_SEMAPHORE 1
#endif /* HAVE_SEMTIMEDOP */

#if !defined(HAVE_SYSV_PSHARED_SEMAPHORE) && \
    !defined(HAVE_POSIX_PSHARED_SEMAPHORE) && \
    !defined(HAVE_RPL_PSHARED_SEMAPHORE)
#define DISABLE_IPC_SHM 1
#endif /* HAVE PSHARED SEMAPHORE */

#ifndef HAVE_STRCHRNUL
char *strchrnul (const char *s, int c_in);
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dest, const char *src, size_t maxlen);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dest, const char *src, size_t maxlen);
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#endif /* QB_OS_BASE_H_DEFINED */
