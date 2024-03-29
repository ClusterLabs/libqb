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
MAINTAINERCLEANFILES = Makefile.in auto_write_logs.c
EXTRA_DIST = make-log-test.sh sock_ipc_wrapper.test blackbox-segfault.sh resources.test start.test
CLEANFILES = $(auto_c_files) ${auto_c_files:.c=.opp} auto_write_logs.c

export SOCKETDIR

AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include

noinst_PROGRAMS = bmc bmcpt bms rbreader rbwriter \
	bench-log format_compare_speed loop print_ver \
	$(check_PROGRAMS)

noinst_HEADERS = check_common.h

format_compare_speed_SOURCES = format_compare_speed.c $(top_builddir)/include/qb/qbutil.h
format_compare_speed_LDADD = $(top_builddir)/lib/libqb.la

bmc_SOURCES = bmc.c
bmc_LDADD = $(top_builddir)/lib/libqb.la

bmcpt_SOURCES = bmcpt.c
bmcpt_CFLAGS = $(PTHREAD_CFLAGS)
bmcpt_LDADD = $(PTHREAD_LIBS) $(top_builddir)/lib/libqb.la

bms_SOURCES = bms.c
bms_CFLAGS = $(GLIB_CFLAGS)
bms_LDADD = $(top_builddir)/lib/libqb.la $(GLIB_LIBS)

rbwriter_SOURCES = rbwriter.c
rbwriter_LDADD = $(top_builddir)/lib/libqb.la

rbreader_SOURCES = rbreader.c
rbreader_LDADD = $(top_builddir)/lib/libqb.la

loop_SOURCES = loop.c
loop_LDADD = $(top_builddir)/lib/libqb.la

inc_dir = $(top_srcdir)/include/qb
public_headers = $(sort $(patsubst %.in,%,$(subst $(inc_dir)/,,$(shell \
	printf 'include $(inc_dir)/Makefile.am\n\n%%.var:\n\t@echo $$($$*)' \
	| MAKEFLAGS= ${MAKE} --no-print-directory -f- inst_HEADERS.var \
	|| echo $(inc_dir)/qb*.h*))))
auto_c_files = $(patsubst %.h,auto_check_header_%.c,$(public_headers))

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
check-headers: $(auto_c_files:.c=.o) $(auto_c_files:.c=.opp)

# this is to check basic sanity of using libqb from C++ code, if possible
%.opp: %.c
if HAVE_GXX
	$(AM_V_GEN)$(CXX) $(AM_CPPFLAGS) -c $(CPPFLAGS) $(CXXFLAGS) -o $@ $<
else
	@echo "C++ compatibility tests not run"
endif

if HAVE_DICT_WORDS
if HAVE_SLOW_TESTS
nodist_bench_log_SOURCES = auto_write_logs.c

bench_log: auto_write_logs.c
$(builddir)/auto_write_logs.c: make-log-test.sh
	@$(srcdir)/make-log-test.sh > $(builddir)/write_logs-tmp.c
	$(AM_V_GEN)mv $(builddir)/write_logs-tmp.c $(builddir)/auto_write_logs.c
endif
endif

bench_log_SOURCES = bench-log.c
bench_log_LDADD = $(top_builddir)/lib/libqb.la
libdir= $(TESTDIR)

if HAVE_CHECK
TESTS = start.test array.test map.test rb.test list.test log.test blackbox-segfault.sh loop.test ipc.test sock_ipc_wrapper.test resources.test
TESTS_ENVIRONMENT = export PATH=.:../tools:$$PATH;

check_PROGRAMS = array.test ipc.test ipc_sock.test list.test log.test loop.test \
		 map.test rb.test util.test tlist.test \
		 crash_test_dummy file_change_bytes
dist_check_SCRIPTS = sock_ipc_wrapper.test blackbox-segfault.sh
check_LTLIBRARIES =

if HAVE_SLOW_TESTS
TESTS += util.test
check_PROGRAMS += util.test
endif

if INSTALL_TESTS
testsuitedir	= $(TESTDIR)
testsuite_PROGRAMS = $(check_PROGRAMS)
testsuite_SCRIPTS = $(dist_check_SCRIPTS) test.conf
endif

file_change_bytes_SOURCES = file_change_bytes.c

crash_test_dummy_SOURCES = crash_test_dummy.c
crash_test_dummy_CFLAGS = @CHECK_CFLAGS@
crash_test_dummy_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

array_test_SOURCES = check_array.c
array_test_CFLAGS = @CHECK_CFLAGS@
array_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

map_test_SOURCES = check_map.c
map_test_CFLAGS = @CHECK_CFLAGS@
map_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

rb_test_SOURCES = check_rb.c
rb_test_CFLAGS = @CHECK_CFLAGS@
rb_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

loop_test_SOURCES = check_loop.c
loop_test_CFLAGS = @CHECK_CFLAGS@
loop_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

tlist_test_SOURCES = check_tlist.c
tlist_test_CFLAGS = @CHECK_CFLAGS@
tlist_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

if HAVE_FAILURE_INJECTION
check_LTLIBRARIES += _failure_injection.la
_failure_injection_la_SOURCES = _failure_injection.c _failure_injection.h
_failure_injection_la_LDFLAGS = -module
_failure_injection_la_LIBADD = $(dlopen_LIBS)
endif

check_LTLIBRARIES += _libstat_wrapper.la
_libstat_wrapper_la_SOURCES = _libstat_wrapper.c
_libstat_wrapper_la_LDFLAGS = -module
_libstat_wrapper_la_LIBADD = $(dlopen_LIBS)

ipc_test_SOURCES = check_ipc.c
ipc_test_CFLAGS = @CHECK_CFLAGS@  -DIPC_TEST_NAME_FILE="\"ipc-test-name\""
ipc_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@
if HAVE_GLIB
ipc_test_CFLAGS += $(GLIB_CFLAGS)
ipc_test_LDADD += $(GLIB_LIBS)
endif
if HAVE_FAILURE_INJECTION
ipc_test_LDADD += _failure_injection.la
endif

ipc_sock_test_SOURCES = check_ipc.c
ipc_sock_test_CFLAGS = @CHECK_CFLAGS@ -DIPC_TEST_NAME_FILE="\"ipc-test-name-sock\""
ipc_sock_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@
if HAVE_GLIB
ipc_sock_test_CFLAGS += $(GLIB_CFLAGS)
ipc_sock_test_LDADD += $(GLIB_LIBS)
endif
ipc_sock_test_LDADD += _libstat_wrapper.la
if HAVE_FAILURE_INJECTION
ipc_sock_test_LDADD += _failure_injection.la
endif

check_LTLIBRARIES += _syslog_override.la
_syslog_override_la_SOURCES = _syslog_override.c _syslog_override.h
_syslog_override_la_LDFLAGS = -module

log_test_SOURCES = check_log.c
log_test_CFLAGS = @CHECK_CFLAGS@
log_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@
log_test_LDADD += _syslog_override.la

util_test_SOURCES = check_util.c
util_test_CFLAGS = @CHECK_CFLAGS@
util_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@

list_test_SOURCES = check_list.c
list_test_CFLAGS = @CHECK_CFLAGS@
list_test_LDADD = $(top_builddir)/lib/libqb.la @CHECK_LIBS@


#
# This lot allows the tests to run in parallel
# but with defined dependancies
#
ipc.log: start.log

sock_ipc_wrapper.log: start.log

array.log: start.log

list.log: start.log

log.log: start.log

loop.log: start.log

map.log: start.log

rb.log: start.log

tlist.log: start.log

util.log: start.log

resources.log: ipc.log sock_ipc_wrapper.log rb.log log.log

endif

clean-local:
	rm -f *.log
	rm -f *.fdata
	rm -f ipc-test-name
	rm -f ipc-test-name-sock
	rm -f crash_test_dummy.core


distclean-local:
	rm -rf auto_*.c
	rm -rf .deps
