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

noinst_PROGRAMS = bmc bmcpt bms

bmc_SOURCES = bmc.c
bmc_CFLAGS = -I$(top_srcdir)/include
bmc_LDADD = -lrt $(top_builddir)/lib/libqbipcc.la $(top_builddir)/lib/libqbpoll.la

bmcpt_SOURCES = bmcpt.c
bmcpt_CFLAGS = -I$(top_srcdir)/include
bmcpt_LDADD = -lrt $(top_builddir)/lib/libqbipcc.la $(top_builddir)/lib/libqbpoll.la

bms_SOURCES = bms.c
bms_CFLAGS = -I$(top_srcdir)/include
bms_LDADD = -lrt $(top_builddir)/lib/libqbipcs.la $(top_builddir)/lib/libqbpoll.la

if HAVE_CHECK

TESTS = check_hash check_plugin

check_PROGRAMS = check_hash check_plugin

check_hash_SOURCES = check_hash.c $(top_builddir)/include/qb/qbhash.h
check_hash_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_hash_LDADD = $(top_builddir)/lib/libqbhash.la @CHECK_LIBS@

check_plugin_SOURCES = check_plugin.c
check_plugin_CFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/include
check_plugin_LDADD = $(top_builddir)/lib/libqbplugin.a @CHECK_LIBS@
check_plugin_LDFLAGS = $(OS_DYFLAGS)

noinst_libdir = $(shell pwd)/$(top_builddir)/tests

noinst_lib_LTLIBRARIES = libtest_a.la libtest_b.la

libtest_a_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libtest_a_la_LDFLAGS	= -version-info 0:0:0
libtest_a_la_SOURCES	= libtest_a.c

libtest_b_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libtest_b_la_LDFLAGS	= -version-info 0:0:0
libtest_b_la_SOURCES	= libtest_b.c

endif

