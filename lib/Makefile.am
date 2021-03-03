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


CLEANFILES		= qblog_script.ld
MAINTAINERCLEANFILES	= Makefile.in

noinst_HEADERS          = ipc_int.h util_int.h ringbuffer_int.h loop_int.h \
			  log_int.h map_int.h rpl_sem.h loop_poll_int.h \
			  atomic_int.h

AM_CPPFLAGS             = -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LTLIBRARIES		= libqb.la

libqb_la_LDFLAGS	= -version-info 102:0:2

source_to_lint		= util.c hdb.c ringbuffer.c ringbuffer_helper.c \
			  array.c loop.c loop_poll.c loop_job.c \
			  loop_timerlist.c ipcc.c ipcs.c ipc_shm.c \
			  ipc_setup.c ipc_socket.c \
			  log.c log_thread.c log_blackbox.c log_file.c \
			  log_syslog.c log_dcs.c log_format.c \
			  map.c skiplist.c hashtable.c trie.c

# Following two files related to linkage using classic ld from binutils 2.29+
# with which we cannot afford to lose public access to section boundary symbols
# (as the mentioned version started to scope them privately by default, see
# the comment within the first of the files, ultimately leading to broken
# logging functionality of libqb) deserve a bit of explanation:
# * qblog_script.ld
#   - linker script that instructs the output section that those symbols should
#     be visible, i.e. supports the same behaviour regardless of ld version
#   - serves two purposes:
#     . local: libqb itself and its "private" (cf. examples) users need those
#              symbols visible, which is achieved with a help of the other file
#     . system-wide: whenever the non-private library users link against libqb
#                    (it's development files), this linker script with
#                    prepended INPUT command so as to refer to the actual
#                    libqb library (it's numbered alias that is eventually
#                    resolved to proper shared library) is masked as libqb.so,
#                    this arrangement achieves the libqb's user will have
#                    the discussed symbols visible alike
# * qblog_script.la
#   - as mentioned earlier, this indirectly hooks into libtool machinery, with
#     the only true intention of injecting "-Wl,<path to qblog_script.ld>"
#     into "inherited_linker_flags" libtool archive variable, from where it's
#     subsequently spread into the build process of all the internal library
#     users, assuming they have their dep arranged as "user_LIBADD=libqb.la"
#     (this also alleviates the burden on getting things right if, e.g., any
#     libqb user consumes it directly like this from its own sub-checkout tree)
#   - it indirectly, once libtool prechew the original link command
#     originally referring to this file, it turns such reference into the
#     "real" library reference (here combining libdir and old_library
#     variables within the file), also ensures libqb itself will visibly
#     expose the discussed symbols, because such references point again to
#     the (not enriched) linker script file that will get interpreted just
#     like that during the last build step of the library
EXTRA_libqb_la_DEPENDENCIES = qblog_script.ld qblog_script.la
EXTRA_DIST		= qblog_script.ld.in qblog_script.la.in qblog_script_noop.ld

libqb_la_SOURCES	= $(source_to_lint) unix.c
libqb_la_CFLAGS		= $(PTHREAD_CFLAGS) $(SYSTEMD_CFLAGS)
libqb_la_LIBADD		= $(LTLIBOBJS) $(dlopen_LIBS) $(PTHREAD_LIBS) $(socket_LIBS) $(rt_LIBS) $(SYSTEMD_LIBS)

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

# nested sed expression is also escaping meta character for the outer one
qblog_script.ld: %.ld: %.ld.in
	$(AM_V_GEN)$(CPP) -C -D_GNU_SOURCE -P $(AM_CPPFLAGS) -xc $< \
	  | sed -n "/$$(sed -n '/^[^#]/{s/[*\/]/\\&/g;p;q;}' $<)/,$$ p" \
	  > $@

# sadly, there's a distinction between "exec" and "data" install, and it's hard
# to decouple install-exec-hook below (.pc file is platform-dependent, anyway)
pkgconfigexecdir = $(libdir)/pkgconfig
pkgconfigexec_DATA = libqb.pc

