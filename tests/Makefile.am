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

noinst_PROGRAMS = bmc bmcpt bms rbwriter rbreader

bmc_SOURCES = bmc.c $(top_builddir)/include/qb/qbipcc.h
bmc_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bmc_LDADD = -lrt $(top_builddir)/lib/libqb.la

bmcpt_SOURCES = bmcpt.c $(top_builddir)/include/qb/qbipcc.h
bmcpt_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bmcpt_LDADD = -lrt $(top_builddir)/lib/libqb.la

bms_SOURCES = bms.c $(top_builddir)/include/qb/qbipcs.h
bms_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
bms_LDADD = -lrt $(top_builddir)/lib/libqb.la

rbwriter_SOURCES = rbwriter.c $(top_builddir)/include/qb/qbrb.h
rbwriter_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
rbwriter_LDADD = -lrt $(top_builddir)/lib/libqb.la

rbreader_SOURCES = rbreader.c $(top_builddir)/include/qb/qbrb.h
rbreader_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
rbreader_LDADD = -lrt $(top_builddir)/lib/libqb.la

#noinst_libdir		= $(abs_top_builddir)/tests
#noinst_lib_LTLIBRARIES	= libtest_a.la libtest_b.la
#
#libtest_a_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
#libtest_a_la_LDFLAGS	= -version-info 0:0:0
#libtest_a_la_SOURCES	= libtest_a.c
#
#libtest_b_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
#libtest_b_la_LDFLAGS	= -version-info 0:0:0
#libtest_b_la_SOURCES	= libtest_b.c

if HAVE_CHECK

TESTS = check_rb check_ipc check_hash
#check_plugin

check_PROGRAMS = check_rb check_ipc check_hash
#check_plugin

check_rb_SOURCES = check_rb.c $(top_builddir)/include/qb/qbrb.h
check_rb_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_rb_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

check_ipc_SOURCES = check_ipc.c $(top_builddir)/include/qb/qbipcc.h $(top_builddir)/include/qb/qbipcs.h
check_ipc_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_ipc_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

check_hash_SOURCES = check_hash.c $(top_builddir)/include/qb/qbhash.h
check_hash_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_hash_LDADD = $(top_builddir)/lib/libqb.la -lrt @CHECK_LIBS@

#check_plugin_SOURCES = check_plugin.c
#check_plugin_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
#check_plugin_LDADD = $(top_builddir)/lib/libqbplugin.a @CHECK_LIBS@
#check_plugin_LDFLAGS = $(OS_DYFLAGS)

endif

