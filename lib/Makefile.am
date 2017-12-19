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

libqb_la_LDFLAGS	= -version-info 19:0:19

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
libqb_la_CFLAGS		= $(PTHREAD_CFLAGS)
libqb_la_LIBADD		= $(LTLIBOBJS) $(dlopen_LIBS) $(PTHREAD_LIBS) $(socket_LIBS) $(rt_LIBS)
if NEED_GCC_ATTRIBUTE_SECTION_WORKAROUND
libqb_la_LIBADD 	+= qblog_script.la
endif

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

qblog_script.ld: %.ld: %.ld.in
	$(AM_V_GEN)$(CPP) -xc -I$(top_srcdir)/include -D_GNU_SOURCE -C -P $< \
	  | sed -n "/$$(sed -n '/^[^#]/{s/[*\/]/\\&/g;p;q;}' $<)/,$$ p" \
	  > $@

# sadly, there's a distinction between "exec" and "data" install, and it's hard
# to decouple install-exec-hook below (.pc file is platform-dependent, anyway)
pkgconfigexecdir = $(libdir)/pkgconfig
pkgconfigexec_DATA = libqb.pc

# find the libqb.so symlink's target, if so, try to find out, iteratively,
# its gradually shorter forms that likewise symlinks the same target as the
# original libqb.so path, point to that file from the linker script using
# qblog_script.ld as a template, storing result in place of original libqb.so
# (e.g., libqb.so := "INPUT(libqb.so.0) " [...] "SECTIONS { " [...] "}")
# NOTE: readlink nor realpath are POSIX; not chained links ready
# NOTE: conservative check, i.e., not per NEED_GCC_ATTRIBUTE_SECTION_WORKAROUND
if HAVE_GCC_ATTRIBUTE_SECTION
install-exec-hook: qblog_script.ld
	target=$$(ls -l "$(DESTDIR)$(libdir)/libqb.so" || :); \
	  target=$${target#* -> }; t1_bn=$$(basename "$${target}" || :); \
	  while test -n "$${t1_bn}"; do t2_bn=$${t1_bn%.*[0-9]*}; \
	  test "$${t2_bn}" != libqb.so || break; \
	  test -L "$${t2_bn}" || { t1_bn=$${t2_bn}; continue; }; \
	  t2_target=$$(ls -l "$${t2_bn}" || break); t2_target=$${t2_target#* -> }; \
	  test "$${t2_target}" = "$${target}" || break; \
	  t1_bn=$${t2_bn}; done; test -n "$${t1_bn}" || \
	  { echo "only applicable to SO symlink scheme"; exit 1; }; \
	  echo "$${t1_bn}" > "$(DESTDIR)$(libdir)/libqb.so-t"
	so_ver=$$(cat "$(DESTDIR)$(libdir)/libqb.so-t"); \
	  echo "INPUT($${so_ver})" > "$(DESTDIR)$(libdir)/libqb.so-t"; \
	  cat $< >> "$(DESTDIR)$(libdir)/libqb.so-t"; \
	  sed -i -- "s/libqb.so.<digit>/$${so_ver}/" \
	    "$(DESTDIR)$(libdir)/libqb.so-t" "$(DESTDIR)$(pkgconfigexecdir)/libqb.pc"
	mv -f "$(DESTDIR)$(libdir)/libqb.so-t" "$(DESTDIR)$(libdir)/libqb.so"
endif

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
