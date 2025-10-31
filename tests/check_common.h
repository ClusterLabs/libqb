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

#define _STRINGIFY(arg)                #arg
#define STRINGIFY(arg)         _STRINGIFY(arg)

#define add_tcase(suite, tcase, func, timeout) \
	do { \
		const char * fname = STRINGIFY(func); \
		(tcase) = tcase_create(fname + sizeof("test") ); \
		tcase_add_test((tcase), func); \
		if (timeout != 0) { \
			tcase_set_timeout((tcase), (timeout));	\
		} \
		suite_add_tcase((suite), (tcase)); \
	} while (0)

#endif
