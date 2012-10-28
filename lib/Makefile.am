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
			  log_int.h map_int.h rpl_sem.h loop_poll_int.h

AM_CPPFLAGS             = -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LTLIBRARIES		= libqb.la

# From: http://sourceware.org/autobook/autobook/autobook_91.html
# (for my own sanity).
#
# 1) If you have changed any of the sources for this library, the revision
#    number must be incremented. This is a new revision of the current
#    interface.
#
# 2) If the interface has changed, then current must be incremented, and
#    revision reset to `0'. This is the first revision of a new interface.
#
# 3) If the new interface is a superset of the previous interface
#    (that is, if the previous interface has not been broken by the changes
#    in this new release), then age must be incremented. This release is
#    backwards compatible with the previous release.
#
# 4) If the new interface has removed elements with respect to the previous
#    interface, then you have broken backward compatibility and age must be
#    reset to `0'. This release has a new, but backwards incompatible
#    interface. For example, if the next release of the library included
#    some new commands for an existing socket protocol, you would use
#    -version-info 1:0:1. This is the first revision of a new interface.
#    This release is backwards compatible with the previous release.
#    Later, you implement a faster way of handling part of the algorithm
#    at the core of the library, and release it with -version-info 1:1:1.
#    This is a new revision of the current interface.
#    Unfortunately the speed of your new implementation can only be fully
#    exploited by changing the API to access the structures at a lower level,
#    which breaks compatibility with the previous interface, so you release
#    it as -version-info 2:0:0. This release has a new, but backwards
#    incompatible interface.
#
libqb_la_LDFLAGS	= -version-info 14:3:14

source_to_lint		= util.c hdb.c ringbuffer.c ringbuffer_helper.c \
			  array.c loop.c loop_poll.c loop_job.c \
			  loop_timerlist.c ipcc.c ipcs.c ipc_shm.c ipc_us.c \
			  log.c log_thread.c log_blackbox.c log_file.c \
			  log_syslog.c log_dcs.c log_format.c \
			  map.c skiplist.c hashtable.c trie.c

libqb_la_SOURCES	= $(source_to_lint) unix.c
libqb_la_LIBADD	        = @LTLIBOBJS@

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

run_splint.sh: $(top_srcdir)/configure.ac
	echo "$(SPLINT) $(ALL_LINT_FLAGS) $(addprefix $(top_srcdir)/lib/, $(source_to_lint))" > $@
	$(AM_V_GEN)chmod +x $@

dist-clean-local:
	$(AM_V_GEN)rm -f run_splint.sh

clean-generic:
	$(AM_V_GEN)rm -f run_splint.sh
endif


