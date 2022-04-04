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

#ifndef QB_CHECK_COMMON_H_DEFINED
#define QB_CHECK_COMMON_H_DEFINED

#include <check.h>

/*
    Auxiliary macros
 */

#define JOIN(a, b)		a##b
#define _STRINGIFY(arg)		#arg
#define STRINGIFY(arg)		_STRINGIFY(arg)

/* wide-spread technique, see, e.g.,
   http://cplusplus.co.il/2010/07/17/variadic-macro-to-count-number-of-arguments
 */
#define VA_ARGS_CNT(...)	VA_ARGS_CNT_IMPL(__VA_ARGS__,8,7,6,5,4,3,2,1,_)
#define VA_ARGS_CNT_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,N,...)		N

/* add_tcase "overloading" per argument count;
   "func" argument is assumed to always starts with "test_", which is skipped
   for the purpose of naming the respective test case that's being created */

#define add_tcase_select(cnt)	JOIN(add_tcase_, cnt)
#define add_tcase_3(suite, tcase, func) \
	do { \
		(tcase) = tcase_create(STRINGIFY(func) + sizeof("test")); \
		tcase_add_test((tcase), func); \
		suite_add_tcase((suite), (tcase)); \
	} while (0)
#define add_tcase_4(suite, tcase, func, timeout) \
	do { \
		(tcase) = tcase_create(STRINGIFY(func) + sizeof("test")); \
		tcase_add_test((tcase), func); \
		tcase_set_timeout((tcase), (timeout)); \
		suite_add_tcase((suite), (tcase)); \
	} while (0)

/*
    Use-me macros
 */

/* add_tcase(<dest-suite>, <testcase-tmp-storage>, <function>[, <timeout>]) */
#define add_tcase(...)	add_tcase_select(VA_ARGS_CNT(__VA_ARGS__))(__VA_ARGS__)

#endif
