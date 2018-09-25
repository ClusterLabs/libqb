/*
 * Copyright (c) 2016-2018 Red Hat, Inc.
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

#include "os_base.h"  /* instead of assert.h & stdio.h + defines VERSION */

#include <qb/qbconfig.h>  /* QB_VER_{MAJOR,MINOR,MICRO,REST} + QB_VER_STR */
#include <qb/qbdefs.h>  /* QB_PP_STRINGIFY */

int
main(void)
{
	printf("%s consists of: %d, %d, %d, %s\n", QB_VER_STR,
	       QB_VER_MAJOR, QB_VER_MINOR, QB_VER_MICRO, QB_VER_REST);
	return 0;
}

/* make version components emitted during "make check" for easy inspection;
   semicolon intentionally omitted so as to avoid unnecessary message
   source diagnostics (bug or feature?) with some GCC versions (5.3.1) */
#define MSG QB_PP_STRINGIFY( \
	message (QB_PP_STRINGIFY(VERSION parsed as: QB_VER_STR)))
_Pragma(MSG);
