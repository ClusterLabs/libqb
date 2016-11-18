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
MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST =
CLEANFILES =
AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include

noinst_PROGRAMS = bmc bmcpt bms rbreader rbwriter \
	bench-log format_compare_speed loop print_ver

noinst_HEADERS = check_common.h

format_compare_speed_SOURCES = format_compare_speed.c $(top_builddir)/include/qb/qbutil.h
format_compare_speed_LDADD = $(top_builddir)/lib/libqb.la

bmc_SOURCES = bmc.c $(top_builddir)/include/qb/qbipcc.h
bmc_LDADD = $(top_builddir)/lib/libqb.la

bmcpt_SOURCES = bmcpt.c $(top_builddir)/include/qb/qbipcc.h
bmcpt_LDADD = $(top_builddir)/lib/libqb.la

bms_SOURCES = bms.c $(top_builddir)/include/qb/qbipcs.h
bms_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include $(GLIB_CFLAGS)
bms_LDADD = $(top_builddir)/lib/libqb.la $(GLIB_LIBS)

rbwriter_SOURCES = rbwriter.c $(top_builddir)/include/qb/qbrb.h
rbwriter_LDADD = $(top_builddir)/lib/libqb.la

rbreader_SOURCES = rbreader.c $(top_builddir)/include/qb/qbrb.h
rbreader_LDADD = $(top_builddir)/lib/libqb.la

loop_SOURCES = loop.c $(top_builddir)/include/qb/qbloop.h
loop_LDADD = $(top_builddir)/lib/libqb.la

inc_dir = $(top_srcdir)/include/qb
public_headers = $(sort $(patsubst %.in,%,$(subst $(inc_dir)/,,$(shell \
	printf 'include $(inc_dir)/Makefile.am\n\n%%.var:\n\t@echo $$($$*)' \
	| MAKEFLAGS= ${MAKE} --no-print-directory -f- inst_HEADERS.var \
	|| echo $(inc_dir)/qb*.h*))))
auto_c_files = $(patsubst %.h,auto_check_header_%.c,$(public_headers))
CLEANFILES += $(auto_c_files)

# this works for both non/generated headers thanks to VPATH being
# automatically set to $(top_srcdir)/tests and $(top_builddir)
# being resolved to ".." by automake
# ($(top_srcdir)/tests/../include/qb/%.h = $(top_srcdir)/include/qb/%.h)
auto_check_header_%.c: $(top_builddir)/include/qb/%.h
	@name=$$(echo "$<" | sed  "s|.*qb/qb||" | sed "s|\.h||") ;\
	NAME=$$(echo $$name | tr [:lower:] [:upper:]) ;\
	echo "#include <qb/qb$$name.h>" > $@_ ;\
	echo "#ifndef QB_$${NAME}_H_DEFINED" >> $@_ ;\
	echo "#error no header protector in file qb$$name.h" >> $@_ ;\
	echo "#endif" >> $@_ ;\
	echo "int main(void) {return 0;}" >> $@_
	$(AM_V_GEN)mv $@_ $@

check: check-headers

# rely on implicit automake rule to include right (local) includes
.PHONY: check-headers
check-headers: $(auto_c_files:.c=.o)

distclean-compile:
	rm -rf auto_*.c

if HAVE_DICT_WORDS
if HAVE_SLOW_TESTS
EXTRA_DIST += make-log-test.sh
CLEANFILES += auto_write_logs.c
MAINTAINERCLEANFILES += auto_write_logs.c

nodist_bench_log_SOURCES = auto_write_logs.c

bench_log: auto_write_logs.c
$(builddir)/auto_write_logs.c: make-log-test.sh
	@$(srcdir)/make-log-test.sh > $(builddir)/write_logs-tmp.c
	$(AM_V_GEN)mv $(builddir)/write_logs-tmp.c $(builddir)/auto_write_logs.c
endif
endif

bench_log_SOURCES = bench-log.c $(top_builddir)/include/qb/qblog.h
bench_log_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bench_log_LDADD = $(top_builddir)/lib/libqb.la

if HAVE_CHECK
EXTRA_DIST += resources.test
EXTRA_DIST += blackbox-segfault.sh

TESTS = array.test map.test rb.test log.test blackbox-segfault.sh loop.test ipc.test resources.test

resources.log: rb.log log.log ipc.log

check_LTLIBRARIES =
check_PROGRAMS = array.test map.test rb.test log.test loop.test ipc.test util.test crash_test_dummy file_change_bytes
dist_check_SCRIPTS = resources.test blackbox-segfault.sh

if HAVE_SLOW_TESTS
TESTS += util.test
check_PROGRAMS += util.test
endif

file_change_bytes_SOURCES = file_change_bytes.c

crash_test_dummy_SOURCES = crash_test_dummy.c $(top_builddir)/include/qb/qblog.h
crash_test_dummy_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
crash_test_dummy_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

array_test_SOURCES = check_array.c $(top_builddir)/include/qb/qbarray.h
array_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
array_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

map_test_SOURCES = check_map.c $(top_builddir)/include/qb/qbmap.h
map_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
map_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

rb_test_SOURCES = check_rb.c $(top_builddir)/include/qb/qbrb.h
rb_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
rb_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

loop_test_SOURCES = check_loop.c $(top_builddir)/include/qb/qbloop.h
loop_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
loop_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

ipc_test_SOURCES = check_ipc.c $(top_builddir)/include/qb/qbipcc.h $(top_builddir)/include/qb/qbipcs.h
ipc_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
ipc_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@
if HAVE_FAILURE_INJECTION
ipc_test_LDADD += _failure_injection.la

check_LTLIBRARIES += _failure_injection.la
_failure_injection_la_SOURCES = _failure_injection.c _failure_injection.h
_failure_injection_la_LDFLAGS = -module
endif

log_test_SOURCES = check_log.c $(top_builddir)/include/qb/qblog.h
log_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
log_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@
if HAVE_SYSLOG_TESTS
log_test_LDADD += _syslog_override.la

check_LTLIBRARIES += _syslog_override.la
_syslog_override_la_SOURCES = _syslog_override.c _syslog_override.h
_syslog_override_la_LDFLAGS = -module
endif

util_test_SOURCES = check_util.c $(top_builddir)/include/qb/qbutil.h
util_test_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
util_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

endif

clean-local:
	rm -f *.log
	rm -f *.fdata
