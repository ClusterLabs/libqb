/*
 * Copyright (c) 2016 Red Hat, Inc.
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

#include "_failure_injection.h"
#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>  /* dlsym */
#include <errno.h>
#include <fcntl.h>  /* O_CREAT, ... */
#include <stdarg.h>
#include <stdio.h>  /* perror */
#include <string.h>  /* memset */
#include <sys/mman.h>  /* mlock */
#include <sys/types.h>  /* mode_t, off_t */


/*** unlink (failure injection) ***/

int _fi_unlink_inject_failure = 0;
typedef int (func_unlink)(const char *);
func_unlink unlink;

static int
fake_unlink(const char *pathname)
{
	errno = EACCES;
	return -1;
}

int
unlink(const char *pathname)
{
	static func_unlink *real_unlink;

	if (_fi_unlink_inject_failure) {
		return fake_unlink(pathname);
	} else if (!real_unlink) {
		real_unlink = (func_unlink *)
			      dlsym(RTLD_NEXT, "unlink");
		if (!real_unlink) {
			perror("dlsym(RTLD_NEXT, \"unlink\"");
			real_unlink = fake_unlink;
		}
	}
	return real_unlink(pathname);
}


/*** unlinkat (failure injection) ***/

#if defined(HAVE_UNLINKAT)
typedef int (func_unlinkat)(int, const char *, int);
func_unlinkat unlinkat;

static int
fake_unlinkat(int dirfd, const char *pathname, int flags)
{
	errno = EACCES;
	return -1;
}

int
unlinkat(int dirfd, const char *pathname, int flags)
{
	static func_unlinkat *real_unlinkat;

	if (_fi_unlink_inject_failure) {
		return fake_unlinkat(dirfd, pathname, flags);
	} else if (!real_unlinkat) {
		real_unlinkat = (func_unlinkat *)
				 dlsym(RTLD_NEXT, "unlinkat");
		if (!real_unlinkat) {
			perror("dlsym(RTLD_NEXT, \"unlinkat\"");
			real_unlinkat = fake_unlinkat;
		}
	}
	return real_unlinkat(dirfd, pathname, flags);
}
#endif


/*** mlock (failure injection and call recording) ***/

int _fi_mlock_called = 0;
int _fi_mlock_fail_at = 0;
int _fi_mlock_fake_success = 0;
int _fi_mlock_error = ENOMEM;
struct qb_fi_mlock_call
	_fi_mlock_calls[QB_FAILURE_INJECTION_MAX_CALLS];
typedef int (func_mlock)(const void *, size_t);
func_mlock mlock;

static int
fake_mlock(const void *address, size_t length)
{
	(void)address;
	(void)length;
	errno = _fi_mlock_error;
	return -1;
}

void
_fi_mlock_reset(void)
{
	_fi_mlock_called = 0;
	_fi_mlock_fail_at = 0;
	_fi_mlock_fake_success = 0;
	_fi_mlock_error = ENOMEM;
	memset(_fi_mlock_calls, 0, sizeof(_fi_mlock_calls));
}

int
mlock(const void *address, size_t length)
{
	static func_mlock *real_mlock;
	int call = _fi_mlock_called++;

	if (call < QB_FAILURE_INJECTION_MAX_CALLS) {
		_fi_mlock_calls[call].address = address;
		_fi_mlock_calls[call].length = length;
	}
	if (_fi_mlock_called == _fi_mlock_fail_at) {
		return fake_mlock(address, length);
	}
	if (_fi_mlock_fake_success) {
		return 0;
	}
	if (!real_mlock) {
		real_mlock = (func_mlock *)dlsym(RTLD_NEXT, "mlock");
		if (!real_mlock) {
			perror("dlsym(RTLD_NEXT, \"mlock\"");
			real_mlock = fake_mlock;
		}
	}
	return real_mlock(address, length);
}


/*** truncate (trigger detection) ***/

int _fi_truncate_called = 0;
typedef int (func_truncate)(const char *, off_t);
func_truncate truncate;

static int
fake_truncate(const char *path, off_t length)
{
	errno = EIO;
	return -1;
}

int
truncate(const char *path, off_t length)
{
	static func_truncate *real_truncate;

	if (!real_truncate) {
		real_truncate = (func_truncate *)
				 dlsym(RTLD_NEXT, "truncate");
		if (!real_truncate) {
			perror("dlsym(RTLD_NEXT, \"truncate\"");
			real_truncate = fake_truncate;
		}
	}
	_fi_truncate_called++;
	return real_truncate(path, length);
}


/*** openat (trigger detection) ***/

int _fi_openat_called = 0;
#if defined(HAVE_UNLINKAT)
typedef int (func_openat)(int, const char *, int, ...);
func_openat openat;

static int
fake_openat(int fd, const char *path, int oflag, ...)
{
	errno = EBADF;
	return -1;
}

#ifndef O_TMPFILE
#  define O_TMPFILE 0
#endif

int
openat(int fd, const char *path, int oflag, ...)
{
	static func_openat *real_openat;
	int use_mode = 0;
	mode_t mode;
	va_list ap;

	if (!real_openat) {
		real_openat = (func_openat *)
				 dlsym(RTLD_NEXT, "openat");
		if (!real_openat) {
			perror("dlsym(RTLD_NEXT, \"openat\"");
			real_openat = fake_openat;
		}
	}
	_fi_openat_called++;

	va_start(ap, oflag);
	if (oflag & (O_CREAT | O_TMPFILE)) {
		mode = va_arg(ap, mode_t);
		use_mode = 1;
	}
	va_end(ap);

	if (use_mode) {
		return real_openat(fd, path, oflag, mode);
	} else {
		return real_openat(fd, path, oflag);
	}
}
#endif
