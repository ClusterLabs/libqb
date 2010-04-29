# Copyright (c) 2010 Red Hat, Inc.
#
# Authors: Angus Salkeld <asalkeld@redhat.com>
#
# This software licensed under BSD license, the text of which follows:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

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

