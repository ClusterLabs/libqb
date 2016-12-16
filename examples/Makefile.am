# Copyright (c) 2011 Red Hat, Inc.
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
MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST =
CLEANFILES =

noinst_PROGRAMS = mapnotify simplelog tcpclient tcpserver ipcclient ipcserver

mapnotify_SOURCES = mapnotify.c $(top_builddir)/include/qb/qbmap.h
mapnotify_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
mapnotify_LDADD = $(top_builddir)/lib/libqb.la

simplelog_SOURCES = simplelog.c $(top_builddir)/include/qb/qblog.h
simplelog_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
simplelog_LDADD = $(top_builddir)/lib/libqb.la

tcpclient_SOURCES = tcpclient.c $(top_builddir)/include/qb/qbloop.h
tcpclient_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
tcpclient_LDADD = $(top_builddir)/lib/libqb.la $(socket_LIBS) $(nsl_LIBS)

tcpserver_SOURCES = tcpserver.c $(top_builddir)/include/qb/qbloop.h
tcpserver_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
tcpserver_LDADD = $(top_builddir)/lib/libqb.la $(socket_LIBS)

ipcclient_SOURCES = ipcclient.c $(top_builddir)/include/qb/qbloop.h \
		    $(top_builddir)/include/qb/qbipcc.h
ipcclient_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
ipcclient_LDADD = $(top_builddir)/lib/libqb.la

ipcserver_SOURCES = ipcserver.c $(top_builddir)/include/qb/qbloop.h \
		    $(top_builddir)/include/qb/qbipcs.h
ipcserver_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include \
		     $(GLIB_CFLAGS)
ipcserver_LDADD = $(top_builddir)/lib/libqb.la $(GLIB_LIBS)
