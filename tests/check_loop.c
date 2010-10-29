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
static int32_t job_4_run_count = 0;

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
}
END_TEST


static Suite *rb_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("qb_loop_job");

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

static void libqb_log_fn(const char *file_name,
			 int32_t file_line, int32_t severity, const char *msg)
{
	printf("libqb: %s:%d %s\n", file_name, file_line, msg);
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = rb_suite();
	SRunner *sr = srunner_create(s);

	qb_util_set_log_function(libqb_log_fn);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
