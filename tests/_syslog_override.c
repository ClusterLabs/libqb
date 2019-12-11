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

#include "_syslog_override.h"

#include <limits.h>
#include <string.h>

int _syslog_opened = 0;
int _syslog_option = 0;
int _syslog_facility = 0;
char _syslog_ident[PATH_MAX] = "";

void openlog(const char *ident, int option, int facility);

void
openlog(const char *ident, int option, int facility)
{
	_syslog_opened = 1;
	_syslog_option = option;
	_syslog_facility = facility;
	strncpy(_syslog_ident, ident, sizeof(_syslog_ident)-1);
}

void syslog(int priority, const char *format, ...);

void
syslog(int priority, const char *format, ...)
{
	_syslog_opened = 1;
}

void closelog(void);

void
closelog(void)
{
	_syslog_opened = 0;
	_syslog_option = -1;
	_syslog_facility = -1;
	_syslog_ident[0] = '\0';
}
