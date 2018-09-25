/*
 * Copyright (c) 2018 Red Hat, Inc.
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

#include "os_base.h"

#include "check_common.h"

#include <qb/qblist.h>
#include <qb/qblog.h>

typedef struct {
	struct qb_list_head list;
	size_t i;
} enlistable_num_t;

#define DIMOF(_a) sizeof(_a)/sizeof(*(_a))

START_TEST(test_list_iter)
{
	QB_LIST_DECLARE(mylist);
	enlistable_num_t reference_head[] = { {.i=0}, {.i=1}, {.i=2}, {.i=3} };
	enlistable_num_t reference_tail[] = { {.i=4}, {.i=5}, {.i=6}, {.i=7} };
	enlistable_num_t *iter, replacement = {.i=8};
	size_t iter_i;

	for (iter_i = DIMOF(reference_head); iter_i > 0; iter_i--) {
		/* prepends in reverse order */
		qb_list_add(&reference_head[iter_i-1].list, &mylist);
	}
	for (iter_i = 0; iter_i < DIMOF(reference_tail); iter_i++) {
		/* appends in natural order */
		qb_list_add_tail(&reference_tail[iter_i].list, &mylist);
	}

	/* assert the constructed list corresponds to ordered sequence... */

	/* ... increasing when iterating forward */
	iter_i = 0;
	qb_list_for_each_entry(iter, &mylist, list) {
		ck_assert_int_eq(iter->i, iter_i);
		iter_i++;
	}

	/* ... and decreasing when iterating backward */
	qb_list_for_each_entry_reverse(iter, &mylist, list) {
		ck_assert_int_gt(iter_i, 0);
		ck_assert_int_eq(iter->i, iter_i-1);
		iter_i--;
	}
	ck_assert_int_eq(iter_i, 0);

	/* also check qb_list_replace and qb_list_first_entry */
	qb_list_replace(mylist.next, &replacement.list);
	ck_assert_int_eq(qb_list_first_entry(&mylist, enlistable_num_t, list)->i,
	                  replacement.i);
}
END_TEST

static Suite *array_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("qb_list");

	add_tcase(s, tc, test_list_iter);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = array_suite();
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
