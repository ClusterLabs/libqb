#
# Copyright (C) 2017-2019 Red Hat, Inc.  All rights reserved.
#
# Authors: Fabio M. Di Nitto <fabbione@kronosnet.org>
#          Federico Simoncelli <fsimon@kronosnet.org>
#
# This software licensed under GPL-2.0+
#

MAINTAINERCLEANFILES	= Makefile.in

EXTRA_DIST             = doxygen2man.1

bin_PROGRAMS	= doxygen2man
noinst_HEADERS  = cstring.h

all: $(PROGRAMS) $(MANS)

doxygen2man_SOURCES = doxygen2man.c cstring.c
doxygen2man_CPPFLAGS = -I$(top_srcdir)/include/
doxygen2man_CFLAGS = $(AM_CFLAGS) $(libxml_CFLAGS)
doxygen2man_LDADD = $(top_builddir)/lib/libqb.la $(libxml_LIBS)

man1_MANS = doxygen2man.1

