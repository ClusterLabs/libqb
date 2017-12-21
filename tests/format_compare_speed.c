/*
 * Copyright (c) 2013 Red Hat, Inc.
 *
 * All rights reserved.
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

#include "os_base.h"

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

extern size_t qb_vsnprintf_serialize(char *serialize, size_t max_len, const char *fmt, va_list ap);

static void
store_this_qb(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
store_this_snprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

typedef void (*snprintf_like_func)(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));


static void
store_this_qb(const char *fmt, ...)
{
	char buf[QB_LOG_MAX_LEN];
	va_list ap;

	va_start(ap, fmt);
	qb_vsnprintf_serialize(buf, QB_LOG_MAX_LEN, fmt, ap);
	va_end(ap);
}

static void
store_this_snprintf(const char *fmt, ...)
{
	char buf[QB_LOG_MAX_LEN];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, QB_LOG_MAX_LEN, fmt, ap);
	va_end(ap);
}

#define ITERATIONS 10000000

static void
test_this_one(const char *name, snprintf_like_func func)
{
	unsigned i;
	qb_util_stopwatch_t *sw = qb_util_stopwatch_create();
	float elapsed = 452.245252343;
	float ops_per_sec = 0.345624523;

	qb_util_stopwatch_start(sw);
	for (i = 0; i < ITERATIONS; i++) {
		func("%u %s %llu %9.3f", i, "hello", 3425ULL, elapsed);
		func("[%10s] %.32xd -> %p", "hello", i, func);
		func("Client %s.%.9s wants to fence (%s) '%s' with device '%3.5f'",
		     "bla", "foooooooooooooooooo",
		     name, "target", ops_per_sec);
		func("Node %s now has process list: %.32x (was %.32x)",
		     "18builder", 2U, 0U);
	}
	qb_util_stopwatch_stop(sw);
	elapsed = qb_util_stopwatch_sec_elapsed_get(sw);
	ops_per_sec = ((float)ITERATIONS) / elapsed;
	printf("%s] Duration: %9.3f OPs/sec: %9.3f\n", name, elapsed, ops_per_sec);
	qb_util_stopwatch_free(sw);
}

int
main(void)
{
	test_this_one("qb store", store_this_qb);
	test_this_one("snprintf", store_this_snprintf);

	return 0;
}
