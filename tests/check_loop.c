/*
 * Copyright (c) 2010 Red Hat, Inc.
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
#include <check.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>

static int32_t job_1_run_count = 0;
static int32_t job_2_run_count = 0;
static int32_t job_3_run_count = 0;

static void job_1(void *data)
{
	job_1_run_count++;
}

static void job_stop(void *data)
{
	qb_loop_t *l = (qb_loop_t *)data;
	job_3_run_count++;
	qb_loop_stop(l);
}
static void job_2(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_2_run_count++;
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_stop);
	ck_assert_int_eq(res, 0);
}
static void job_1_r(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_1_run_count++;
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_2);
	ck_assert_int_eq(res, 0);
}
static void job_1_add_nuts(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_1_run_count++;
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_1);
	res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_1);
	if (job_1_run_count < 500) {
		res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_1_add_nuts);
	} else {
		res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_stop);
	}
	ck_assert_int_eq(res, 0);
}

START_TEST(test_loop_job_input)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	fail_if(l == NULL);

	res = qb_loop_job_add(NULL, QB_LOOP_LOW,  NULL, job_2);
	ck_assert_int_eq(res, -EINVAL);
	res = qb_loop_job_add(l, 89,  NULL, job_2);
	ck_assert_int_eq(res, -EINVAL);
	res = qb_loop_job_add(l, QB_LOOP_LOW,  NULL, NULL);
	ck_assert_int_eq(res, -EINVAL);
	qb_loop_destroy(l);
}
END_TEST

START_TEST(test_loop_job_1)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	fail_if(l == NULL);

	res = qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_LOW,  l, job_stop);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	ck_assert_int_eq(job_1_run_count, 1);
	qb_loop_destroy(l);
}
END_TEST

START_TEST(test_loop_job_4)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	fail_if(l == NULL);

	res = qb_loop_job_add(l, QB_LOOP_LOW,  l, job_1_r);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	ck_assert_int_eq(job_1_run_count, 1);
	ck_assert_int_eq(job_2_run_count, 1);
	ck_assert_int_eq(job_3_run_count, 1);
	qb_loop_destroy(l);
}
END_TEST


START_TEST(test_loop_job_nuts)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	fail_if(l == NULL);

	res = qb_loop_job_add(l, QB_LOOP_LOW,  l, job_1_add_nuts);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	fail_if(job_1_run_count < 500);
	qb_loop_destroy(l);
}
END_TEST

static Suite *loop_job_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("loop_job");

	tc = tcase_create("limits");
	tcase_add_test(tc, test_loop_job_input);
	suite_add_tcase(s, tc);

	tc = tcase_create("run_one");
	tcase_add_test(tc, test_loop_job_1);
	suite_add_tcase(s, tc);

	tc = tcase_create("run_recursive");
	tcase_add_test(tc, test_loop_job_4);
	suite_add_tcase(s, tc);

	tc = tcase_create("run_500");
	tcase_add_test(tc, test_loop_job_nuts);
	suite_add_tcase(s, tc);

	return s;
}

/*
 * -----------------------------------------------------------------------
 *  Timers
 */

START_TEST(test_loop_timer_input)
{
	int32_t res;
	qb_loop_timer_handle th;
	qb_loop_t *l = qb_loop_create();
	fail_if(l == NULL);

	res = qb_loop_timer_add(NULL, QB_LOOP_LOW, 5, NULL, job_2, &th);
	ck_assert_int_eq(res, -EINVAL);
	res = qb_loop_timer_add(l, QB_LOOP_LOW, 5, l, NULL, &th);
	ck_assert_int_eq(res, -EINVAL);
	res = qb_loop_timer_add(l, QB_LOOP_LOW, 5, l, job_1, NULL);
	ck_assert_int_eq(res, -ENOENT);
	qb_loop_destroy(l);
}
END_TEST

struct qb_stop_watch {
	uint64_t start;
	uint64_t end;
	qb_loop_t *l;
	int32_t ms_timer;
	int64_t total;
	int32_t count;
};

static void stop_watch_tmo(void*data)
{
	qb_loop_timer_handle th;
	struct qb_stop_watch *sw = (struct qb_stop_watch *)data;
	int64_t per;

	sw->end = qb_util_nano_current_get();
	sw->total += sw->end - sw->start;
	sw->total -= sw->ms_timer * QB_TIME_NS_IN_MSEC;
	sw->start = sw->end;
	sw->count++;
	if (sw->count < 50) {
		qb_loop_timer_add(sw->l, QB_LOOP_LOW, sw->ms_timer, data, stop_watch_tmo, &th);
	} else {
		per = (sw->total / sw->count) * 100 / (sw->ms_timer * QB_TIME_NS_IN_MSEC);
		printf("average error for %d ms timer is %"PRIi64" (ns) (%"PRIi64"%%)\n",
		       sw->ms_timer,
		       sw->total/sw->count, per);
		if (sw->ms_timer == 100) {
			qb_loop_stop(sw->l);
		}
	}
}

static void start_timer(qb_loop_t *l, struct qb_stop_watch *sw, int32_t timeout)
{
	qb_loop_timer_handle th;
	int32_t res;

	sw->l = l;
	sw->count = 0;
	sw->total = 0;
	sw->ms_timer = timeout;
	sw->start = qb_util_nano_current_get();
	res = qb_loop_timer_add(sw->l, QB_LOOP_LOW, sw->ms_timer, sw, stop_watch_tmo, &th);
	ck_assert_int_eq(res, 0);
}


START_TEST(test_loop_timer_basic)
{
	int32_t i;
	int32_t tmo;
	struct qb_stop_watch sw[11];
	qb_loop_t *l = qb_loop_create();

	fail_if(l == NULL);

	for (i = 0; i < 10; i++) {
		tmo = 5 + i * 9;
		start_timer(l, &sw[i], tmo);
	}
	start_timer(l, &sw[i], 100);

	qb_loop_run(l);
	qb_loop_destroy(l);
}
END_TEST

static Suite *loop_timer_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("loop_timers");

	tc = tcase_create("limits");
	tcase_add_test(tc, test_loop_timer_input);
	suite_add_tcase(s, tc);

	tc = tcase_create("basic");
	tcase_add_test(tc, test_loop_timer_basic);
	tcase_set_timeout(tc, 30);
	suite_add_tcase(s, tc);

	return s;
}

static void libqb_log_fn(const char *file_name,
			 int32_t file_line, int32_t severity, const char *msg)
{
	printf("libqb: %s:%d %s\n", file_name, file_line, msg);
}

int32_t main(void)
{
	int32_t number_failed;
	SRunner *sr = srunner_create(loop_job_suite());
	srunner_add_suite (sr, loop_timer_suite());

	qb_util_set_log_function(libqb_log_fn);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
