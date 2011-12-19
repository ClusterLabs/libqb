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
MAINTAINERCLEANFILES = Makefiles.in
EXTRA_DIST =
CLEANFILES =

noinst_PROGRAMS = bmc bmcpt bms rbwriter rbreader loop bench-log

bmc_SOURCES = bmc.c $(top_builddir)/include/qb/qbipcc.h
bmc_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bmc_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la

bmcpt_SOURCES = bmcpt.c $(top_builddir)/include/qb/qbipcc.h
bmcpt_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bmcpt_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la

bms_SOURCES = bms.c $(top_builddir)/include/qb/qbipcs.h
bms_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include $(GLIB_CFLAGS)
bms_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la $(GLIB_LIBS)

rbwriter_SOURCES = rbwriter.c $(top_builddir)/include/qb/qbrb.h
rbwriter_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
rbwriter_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la

rbreader_SOURCES = rbreader.c $(top_builddir)/include/qb/qbrb.h
rbreader_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
rbreader_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la

loop_SOURCES = loop.c $(top_builddir)/include/qb/qbloop.h
loop_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
loop_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la

if HAVE_DICT_WORDS
if HAVE_SLOW_TESTS
EXTRA_DIST += make-log-test
CLEANFILES += write_logs.c
MAINTAINERCLEANFILES += write_logs.c

nodist_bench_log_SOURCES = write_logs.c

bench_log: write_logs.c
$(builddir)/write_logs.c: make-log-test
	$(srcdir)/make-log-test > $(builddir)/write_logs-tmp.c
	$(AM_V_GEN)mv $(builddir)/write_logs-tmp.c $(builddir)/write_logs.c

distclean-compile:
	rm -rf write_logs.c

endif
endif

bench_log_SOURCES = bench-log.c $(top_builddir)/include/qb/qblog.h
bench_log_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bench_log_LDADD = $(LIB_RT) $(top_builddir)/lib/libqb.la

if HAVE_CHECK
EXTRA_DIST += resources.test

TESTS = array.test map.test rb.test log.test loop.test ipc.test resources.test map.test

resources.log: rb.log log.log ipc.log

check_PROGRAMS = array.test map.test rb.test log.test loop.test ipc.test
check_SCRIPTS = resources.test

array_test_SOURCES = check_array.c $(top_builddir)/include/qb/qbarray.h
array_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
array_test_LDADD = $(top_builddir)/lib/libqb.la $(LIB_RT) @CHECK_LIBS@

map_test_SOURCES = check_map.c $(top_builddir)/include/qb/qbmap.h
map_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
map_test_LDADD = $(top_builddir)/lib/libqb.la $(LIB_RT) @CHECK_LIBS@

rb_test_SOURCES = check_rb.c $(top_builddir)/include/qb/qbrb.h
rb_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
rb_test_LDADD = $(top_builddir)/lib/libqb.la $(LIB_RT) @CHECK_LIBS@

loop_test_SOURCES = check_loop.c $(top_builddir)/include/qb/qbloop.h
loop_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
loop_test_LDADD = $(top_builddir)/lib/libqb.la $(LIB_RT) @CHECK_LIBS@

ipc_test_SOURCES = check_ipc.c $(top_builddir)/include/qb/qbipcc.h $(top_builddir)/include/qb/qbipcs.h
ipc_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
ipc_test_LDADD = $(top_builddir)/lib/libqb.la $(LIB_RT) @CHECK_LIBS@

log_test_SOURCES = check_log.c $(top_builddir)/include/qb/qblog.h
log_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
log_test_LDADD = $(top_builddir)/lib/libqb.la $(LIB_RT) @CHECK_LIBS@

endif

clean-generic:
	$(AM_V_GEN)rm -f *.log
