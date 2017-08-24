/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake <sdake@redhat.com>
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

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

#define assert_int_between(_c, _lower, _upper) \
_ck_assert_int(_c, >=, _lower); \
_ck_assert_int(_c, <=, _upper);


START_TEST(test_check_overwrite)
{
	uint64_t res;
	uint32_t last;
	qb_util_stopwatch_t *sw = qb_util_stopwatch_create();

	qb_util_stopwatch_split_ctl(sw, 5, QB_UTIL_SW_OVERWRITE);

	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 0, 100);

	usleep(10000);
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 9000, 11000);

	usleep(20000);
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 19000, 21000);

	usleep(30000);
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 29000, 31000);

	usleep(40000);
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 39000, 41000);

	/*
	 * window should be 100000 (40000 + 30000 + 20000 + 10000) usec
	 */
	last = qb_util_stopwatch_split_last(sw);
	res = qb_util_stopwatch_time_split_get(sw, last, last - 4);
	assert_int_between(res, 95000, 105000);

	usleep(50000);
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 49000, 52000);
	/*
	 * window should be 140000 (50000 + 40000 + 30000 + 20000) usec
	 */
	last = qb_util_stopwatch_split_last(sw);
	res = qb_util_stopwatch_time_split_get(sw, last, last - 4);
	assert_int_between(res, 135000, 145000);

	usleep(25000);
	qb_util_stopwatch_split(sw);

	/* ask for a split that has been overwritten.
	 */
	res = qb_util_stopwatch_time_split_get(sw, last, 1);
	ck_assert_int_eq(res, 0);

	/* iterating
	 */
	last = qb_util_stopwatch_split_last(sw);
	do {
		res = qb_util_stopwatch_time_split_get(sw, last, last);
		qb_log(LOG_INFO, "overwrite split %d is %"PRIu64"", last, res);
		last--;
	} while (res > 0);

	qb_util_stopwatch_free(sw);
}
END_TEST

START_TEST(test_check_normal)
{
	uint64_t res;
	uint32_t last;
	qb_util_stopwatch_t *sw = qb_util_stopwatch_create();

	qb_util_stopwatch_split_ctl(sw, 3, 0);

	qb_util_stopwatch_start(sw);
	usleep(33000);
	/* 1 */
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 30000, 36000);
	last = qb_util_stopwatch_split_last(sw);
	ck_assert_int_eq(last, 0);

	usleep(10000);
	/* 2 */
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 9000, 11000);

	usleep(20000);
	/* 3 */
	res = qb_util_stopwatch_split(sw);
	assert_int_between(res, 19000, 21000);

	/* no more space */
	res = qb_util_stopwatch_split(sw);
	ck_assert_int_eq(res, 0);

	/*
	 * split should be 30000 (10000 + 20000) usec
	 */
	last = qb_util_stopwatch_split_last(sw);
	ck_assert_int_eq(last, 2);
	res = qb_util_stopwatch_time_split_get(sw, last, 0);
	assert_int_between(res, 25000, 35000);

	/* ask for a split that has beyond the max.
	 */
	res = qb_util_stopwatch_time_split_get(sw, 3, 2);
	ck_assert_int_eq(res, 0);

	/* iterating
	 */
	last = qb_util_stopwatch_split_last(sw);
	do {
		res = qb_util_stopwatch_time_split_get(sw, last, last);
		qb_log(LOG_INFO, "normal split %d is %"PRIu64"", last, res);
		last--;
	} while (res > 0);

	qb_util_stopwatch_free(sw);
}
END_TEST

static Suite *util_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("qb_util");

	add_tcase(s, tc, test_check_overwrite);
	add_tcase(s, tc, test_check_normal);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = util_suite();
	SRunner *sr = srunner_create(s);

	qb_log_init("check", LOG_USER, LOG_EMERG);
	atexit(qb_log_fini);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
