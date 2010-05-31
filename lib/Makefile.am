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

noinst_HEADERS          = ipc_int.h util_int.h

lib_LIBRARIES		= libqbplugin.a

libqbplugin_a_SOURCES	= plugin_loader.c

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

lib_LTLIBRARIES		= libqb.la

libqb_la_CPPFLAGS	= -I$(top_builddir)/include -I$(top_srcdir)/include
libqb_la_LDFLAGS	= -version-info 0:0:0
libqb_la_SOURCES	= util.c tsafe.c hash.c poll.c timer.c wthread.c \
			  ipcc.c ipcs.c logsys.c

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libqb.pc

