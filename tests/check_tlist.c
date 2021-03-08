/*
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse <jfriesse@redhat.com>
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

#include "tlist.h"

#include <poll.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

#define SHORT_TIMEOUT		(100 * QB_TIME_NS_IN_MSEC)
#define LONG_TIMEOUT		(60 * QB_TIME_NS_IN_SEC)

#define SPEED_TEST_NO_ITEMS	10000

static int timer_list_fn1_called = 0;

static void
timer_list_fn1(void *data)
{

	ck_assert(data == &timer_list_fn1_called);

	timer_list_fn1_called++;
}

static void
sleep_ns(long long int ns)
{

	(void)poll(NULL, 0, (ns / QB_TIME_NS_IN_MSEC));
}

START_TEST(test_check_basic)
{
	struct timerlist tlist;
	timer_handle thandle;
	int res;
	uint64_t u64;

	timerlist_init(&tlist);

	/*
	 * Check adding short duration and calling callback
	 */
	res = timerlist_add_duration(&tlist, timer_list_fn1, &timer_list_fn1_called, SHORT_TIMEOUT / 2, &thandle);
	ck_assert_int_eq(res, 0);

	sleep_ns(SHORT_TIMEOUT);
	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 == 0);

	timer_list_fn1_called = 0;
	timerlist_expire(&tlist);
	ck_assert_int_eq(timer_list_fn1_called, 1);

	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 == -1);

	/*
	 * Check callback is not called (long timeout)
	 */
	res = timerlist_add_duration(&tlist, timer_list_fn1, &timer_list_fn1_called, LONG_TIMEOUT / 2, &thandle);
	ck_assert_int_eq(res, 0);

	sleep_ns(SHORT_TIMEOUT);
	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 > 0);

	timer_list_fn1_called = 0;
	timerlist_expire(&tlist);
	ck_assert_int_eq(timer_list_fn1_called, 0);

	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 > 0);

	/*
	 * Delete timer
	 */
	timerlist_del(&tlist, thandle);
	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 == -1);
}
END_TEST

START_TEST(test_check_speed)
{
	struct timerlist tlist;
	timer_handle thandle[SPEED_TEST_NO_ITEMS];
	int res;
	uint64_t u64;
	int i;

	timerlist_init(&tlist);

	/*
	 * Check adding a lot of short duration and deleting
	 */
	for (i = 0; i < SPEED_TEST_NO_ITEMS; i++) {
		res = timerlist_add_duration(&tlist, timer_list_fn1, &timer_list_fn1_called,
		    SHORT_TIMEOUT / 2, &thandle[i]);
		ck_assert_int_eq(res, 0);
	}

	for (i = 0; i < SPEED_TEST_NO_ITEMS; i++) {
		timerlist_del(&tlist, thandle[i]);
	}

	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 == -1);

	/*
	 * Check adding a lot of short duration and calling callback
	 */
	for (i = 0; i < SPEED_TEST_NO_ITEMS; i++) {
		res = timerlist_add_duration(&tlist, timer_list_fn1, &timer_list_fn1_called,
		    SHORT_TIMEOUT / 2, &thandle[i]);
		ck_assert_int_eq(res, 0);
	}

	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 != -1);

	sleep_ns(SHORT_TIMEOUT);

	timer_list_fn1_called = 0;
	timerlist_expire(&tlist);
	ck_assert_int_eq(timer_list_fn1_called, SPEED_TEST_NO_ITEMS);

	u64 = timerlist_msec_duration_to_expire(&tlist);
	ck_assert(u64 == -1);
}
END_TEST

static Suite *tlist_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("tlist");

	add_tcase(s, tc, test_check_basic);
	add_tcase(s, tc, test_check_speed, 30);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = tlist_suite();
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
