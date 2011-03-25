# Copyright (c) 2010 Red Hat, Inc.
#
# Authors: Angus Salkeld <asalkeld@redhat.com>
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
#

noinst_PROGRAMS = bmc bmcpt bms rbwriter rbreader loop simple-log

bmc_SOURCES = bmc.c $(top_builddir)/include/qb/qbipcc.h
bmc_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bmc_LDADD = -lrt $(top_builddir)/lib/libqb.la

bmcpt_SOURCES = bmcpt.c $(top_builddir)/include/qb/qbipcc.h
bmcpt_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bmcpt_LDADD = -lrt $(top_builddir)/lib/libqb.la

bms_SOURCES = bms.c $(top_builddir)/include/qb/qbipcs.h
bms_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include $(GLIB_CFLAGS)
bms_LDADD = -lrt $(top_builddir)/lib/libqb.la $(GLIB_LIBS)

rbwriter_SOURCES = rbwriter.c $(top_builddir)/include/qb/qbrb.h
rbwriter_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
rbwriter_LDADD = -lrt $(top_builddir)/lib/libqb.la

rbreader_SOURCES = rbreader.c $(top_builddir)/include/qb/qbrb.h
rbreader_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
rbreader_LDADD = -lrt $(top_builddir)/lib/libqb.la

loop_SOURCES = loop.c $(top_builddir)/include/qb/qbloop.h
loop_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
loop_LDADD = -lrt $(top_builddir)/lib/libqb.la

simple_log_SOURCES = simple-log.c $(top_builddir)/include/qb/qblog.h
simple_log_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
simple_log_LDADD = -lrt $(top_builddir)/lib/libqb.la


if HAVE_CHECK
EXTRA_DIST = check_resources.sh

TESTS = check_array check_rb check_loop check_ipc check_resources.sh

check_PROGRAMS = check_array check_rb check_loop check_ipc
check_SCRIPTS = check_resources.sh

check_array_SOURCES = check_array.c $(top_builddir)/include/qb/qbarray.h
check_array_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_array_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

check_rb_SOURCES = check_rb.c $(top_builddir)/include/qb/qbrb.h
check_rb_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_rb_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

check_loop_SOURCES = check_loop.c $(top_builddir)/include/qb/qbloop.h
check_loop_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_loop_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

check_ipc_SOURCES = check_ipc.c $(top_builddir)/include/qb/qbipcc.h $(top_builddir)/include/qb/qbipcs.h
check_ipc_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_ipc_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

endif

