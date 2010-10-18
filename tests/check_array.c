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

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <check.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbarray.h>

struct test_my_st {
	int32_t a;
	int32_t b;
	int32_t c;
	int32_t d;
};


START_TEST(test_array1)
{
	qb_array_t *a;
	int32_t i;
	int32_t res;
	struct test_my_st *st_old;
	struct test_my_st *st;

	a = qb_array_create(112, sizeof(struct test_my_st));

	/* valid */
	for (i = 0; i < 112; i++) {
		res = qb_array_index(a, i, (void**)&st);
		ck_assert_int_eq(res, 0);
		st->a = i;
		st->b = i+1;
		st->c = i+2;
		st->d = i+3;
	}
	res = qb_array_index(a, 99, (void**)&st_old);
	ck_assert_int_eq(res, 0);

	/* out-of-bounds */
	res = qb_array_index(a, 112, (void**)&st);
	ck_assert_int_eq(res, -EINVAL);


	res = qb_array_grow(a, 1453);
	ck_assert_int_eq(res, 0);

	res = qb_array_index(a, 345, (void**)&st);
	st->a = 411;

	/* read back */
	for (i = 0; i < 112; i++) {
		res = qb_array_index(a, i, (void**)&st);
		ck_assert_int_eq(res, 0);
		ck_assert_int_eq(st->a, i);
		ck_assert_int_eq(st->b, i+1);
		ck_assert_int_eq(st->c, i+2);
		ck_assert_int_eq(st->d, i+3);
	}
	/* confirm the pointer is the same after a grow */
	res = qb_array_index(a, 99, (void**)&st);
	ck_assert_int_eq(res, 0);
	fail_if(st != st_old);

	qb_array_free(a);
}
END_TEST

static Suite *rb_suite(void)
{
	TCase *tc_load;
	Suite *s = suite_create("array");

	tc_load = tcase_create("test01");
	tcase_add_test(tc_load, test_array1);
	suite_add_tcase(s, tc_load);

	return s;
}

static void libqb_log_fn(const char *file_name,
			 int32_t file_line, int32_t severity, const char *msg)
{
//	if (severity < LOG_INFO)
		printf("libqb: %s:%d %s\n", file_name, file_line, msg);
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = rb_suite();
	SRunner *sr = srunner_create(s);

	qb_util_set_log_function(libqb_log_fn);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
