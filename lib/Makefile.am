#
# Copyright (C) 2010 Red Hat, Inc.
#
# Author: Angus Salkeld <asaslkeld@redhat.com>
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


MAINTAINERCLEANFILES	= Makefile.in

noinst_HEADERS          = ipc_int.h util_int.h ringbuffer_int.h loop_int.h \
			  log_int.h map_int.h rpl_sem.h loop_poll_int.h \
			  atomic_int.h

AM_CPPFLAGS             = -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LTLIBRARIES		= libqb.la

libqb_la_LDFLAGS	= -version-info 18:0:18

source_to_lint		= util.c hdb.c ringbuffer.c ringbuffer_helper.c \
			  array.c loop.c loop_poll.c loop_job.c \
			  loop_timerlist.c ipcc.c ipcs.c ipc_shm.c \
			  ipc_setup.c ipc_socket.c \
			  log.c log_thread.c log_blackbox.c log_file.c \
			  log_syslog.c log_dcs.c log_format.c \
			  map.c skiplist.c hashtable.c trie.c

libqb_la_SOURCES	= $(source_to_lint) unix.c
libqb_la_LIBADD	        = @LTLIBOBJS@

AM_LDFLAGS 	= $(LDFLAGS_COPY:-Bsymbolic-functions=)

if HAVE_SEM_TIMEDWAIT
else
  libqb_la_SOURCES+=rpl_sem.c
endif

if HAVE_EPOLL
  libqb_la_SOURCES+=loop_poll_epoll.c
else
if HAVE_KQUEUE
  libqb_la_SOURCES+=loop_poll_kqueue.c
else
  libqb_la_SOURCES+=loop_poll_poll.c
endif
endif


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libqb.pc

if HAVE_SPLINT
check_SCRIPTS = run_splint.sh
TESTS = $(check_SCRIPTS)
# this is a hack because debian/ubuntu don't set the arch path
# in splint.
DEB_INCLUDES = -I/usr/include/x86_64-linux-gnu -I/usr/include/i386-linux-gnu

ALL_LINT_FLAGS = $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(DEB_INCLUDES) \
		 $(libqb_la_CPPFLAGS) $(CPPFLAGS) $(AM_CPPFLAGS) \
		 $(LINT_FLAGS)
# expected for the time being (but SHOULD be fixed eventually):
# ipc_setup.c: (in function qb_ipcc_us_setup_connect)
# ipc_setup.c:479:2: Return value (type int32_t) ignored: qb_ipc_us_ready(...
# ipc_setup.c:494:2: Return value (type int32_t) ignored: qb_ipc_auth_cred...
SPLINT_SUMMARY_EXP = " 2 code warnings"

run_splint.sh: $(top_srcdir)/configure.ac
	@echo '$(SPLINT) $(ALL_LINT_FLAGS) \' > $@-t
	@echo '  $(addprefix $(top_srcdir)/lib/, $(source_to_lint)) \' >> $@-t
	@echo '  2>&1 | tee $@-o' >> $@-t
	@echo 'tail -n1 $@-o | grep -qF $(SPLINT_SUMMARY_EXP)' >> $@-t
	@echo 'ret=$$?' >> $@-t
	@echo 'rm -f $@-o' >> $@-t
	@echo 'test $$ret = 0 || echo "EXPECTED:$(SPLINT_SUMMARY_EXP)"' >> $@-t
	@echo 'exit $$ret' >> $@-t
	@chmod +x $@-t
	$(AM_V_GEN)mv $@-t $@

dist-clean-local:
	rm -f run_splint.sh

clean-local:
	rm -f run_splint.sh
endif
