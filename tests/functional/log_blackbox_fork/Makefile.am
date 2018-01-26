# Copyright 2018 Red Hat, Inc.
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

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include

noinst_PROGRAMS = log_blackbox_fork
log_blackbox_fork_LDADD = $(top_builddir)/lib/libqb.la
log_blackbox_fork_sources = log_blackbox_fork.c
