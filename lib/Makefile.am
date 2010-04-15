#
# Copyright (c) 2010 Red Hat, Inc.
#
# Author: Angus Salkeld <asaslkeld@redhat.com>
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


MAINTAINERCLEANFILES	= Makefile.in

noinst_HEADERS          = ipc_int.h

#pkgconfigdir		= $(libdir)/pkgconfig
#pkgconfig_DATA		= libqbhash.pc
#libqbhash_la_LIBADD	= 

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

lib_LTLIBRARIES		= libqbhash.la libqbpoll.la libqbtimer.la libqblogsys.la \
			  libqbtsafe.la  libqbwthread.la libqbipcc.la libqbipcs.la

lib_LIBRARIES		= libqbplugin.a

libqbplugin_a_SOURCES	= plugin_loader.c

libqbhash_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libqbhash_la_LDFLAGS	= -version-info 0:0:0
libqbhash_la_SOURCES	= hash.c

libqbpoll_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libqbpoll_la_LDFLAGS	= -version-info 0:0:0
libqbpoll_la_SOURCES	= poll.c

libqbtimer_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libqbtimer_la_LDFLAGS	= -version-info 0:0:0
libqbtimer_la_SOURCES	= timer.c

libqblogsys_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libqblogsys_la_LDFLAGS	= -version-info 0:0:0
libqblogsys_la_SOURCES	= logsys.c

libqbtsafe_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libqbtsafe_la_LDFLAGS	= -version-info 0:0:0
libqbtsafe_la_SOURCES	= tsafe.c

libqbwthread_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libqbwthread_la_LDFLAGS	= -version-info 0:0:0
libqbwthread_la_SOURCES	= wthread.c

libqbipcc_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libqbipcc_la_LDFLAGS	= -version-info 0:0:0
libqbipcc_la_SOURCES	= ipcc.c

libqbipcs_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libqbipcs_la_LDFLAGS	= -version-info 0:0:0
libqbipcs_la_SOURCES	= ipcs.c

