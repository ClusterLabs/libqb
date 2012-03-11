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

noinst_HEADERS          = ipc_int.h util_int.h ringbuffer_int.h loop_int.h log_int.h map_int.h

#
# Here are a set of rules to help you update your library version information:
# Start with version information of ‘0:0:0’ for each libtool library.
# Update the version information only immediately before a public release of your software.
# More frequent updates are unnecessary, and only guarantee that the current interface number gets larger faster.
# If the library source code has changed at all since the last update, then increment revision (‘c:r:a’ becomes ‘c:r+1:a’).
# If any interfaces have been added, removed, or changed since the last update, increment current, and set revision to 0.
# If any interfaces have been added since the last public release, then increment age.
# If any interfaces have been removed since the last public release, then set age to 0.
#
AM_CPPFLAGS             = -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LTLIBRARIES		= libqb.la

libqb_la_LDFLAGS	= -version-info 11:1:11
source_to_lint		= util.c hdb.c ringbuffer.c ringbuffer_helper.c array.c \
			  loop.c loop_poll.c loop_job.c loop_timerlist.c \
			  ipcc.c ipcs.c ipc_shm.c ipc_us.c \
			  log.c log_thread.c log_blackbox.c log_file.c \
			  log_syslog.c log_dcs.c log_format.c \
			  map.c skiplist.c hashtable.c trie.c

libqb_la_SOURCES	= $(source_to_lint) unix.c
libqb_la_LIBADD	        = @LTLIBOBJS@

if HAVE_SYSV_MQ
  libqb_la_SOURCES+=ipc_sysv_mq.c
endif

if HAVE_POSIX_MQ
  libqb_la_SOURCES+=ipc_posix_mq.c
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libqb.pc

if HAVE_SPLINT
check_SCRIPTS = run_splint.sh
TESTS = $(check_SCRIPTS)

ALL_LINT_FLAGS = $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
		 $(libqb_la_CPPFLAGS) $(CPPFLAGS) $(AM_CPPFLAGS) \
		 $(LINT_FLAGS)

run_splint.sh: $(top_srcdir)/configure.ac
	echo "$(SPLINT) $(ALL_LINT_FLAGS) $(addprefix $(top_srcdir)/lib/, $(source_to_lint))" > $@
	$(AM_V_GEN)chmod +x $@

dist-clean-local:
	$(AM_V_GEN)rm -f run_splint.sh

clean-generic:
	$(AM_V_GEN)rm -f run_splint.sh
endif


