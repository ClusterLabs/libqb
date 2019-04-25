#!/bin/sh
# Copyright 2017 Red Hat, Inc.
#
# Author: Jan Pokorny <jpokorny@redhat.com>
#
# This file is part of libqb.
#
# libqb is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2.1 of the License, or
# (at your option) any later version.
#
# libqb is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with libqb.  If not, see <http://www.gnu.org/licenses/>.

# error msg can differ per locale, errno code per system (Hurd begs to differ)
./log_interlib_client 2>&1 >/dev/null \
  | sed 's/\(qb_log_blackbox_print_from_file:\).*/\1/' \
  >log_test_interlib_client.err.real

_pipeline='cat ../log_test_interlib_client.err'
case "${CPPFLAGS}" in
	*-DNLOG*)
		_pipeline="${_pipeline} | \
		           grep -Ev '^\[MAIN \|info] \.\./log_interlib_client\.c'";;
	*-DNLIBLOG*)
		_pipeline="${_pipeline} | \
		           grep -Ev '^\[MAIN \|info\] \.\./log_interlib\.c'";;
esac

eval "${_pipeline}" | diff -u - log_test_interlib_client.err.real
