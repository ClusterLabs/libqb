#
# Copyright (C) 2017-2019 Red Hat, Inc.  All rights reserved.
#
# Authors: Fabio M. Di Nitto <fabbione@kronosnet.org>
#          Federico Simoncelli <fsimon@kronosnet.org>
#
# This software licensed under GPL-2.0+
#

MAINTAINERCLEANFILES	= Makefile.in

bin_PROGRAMS	= doxygen2man

all: $(PROGRAMS)

doxygen2man_SOURCES = doxygen2man.c
doxygen2xml_CPPFLAG = -I$(top_srcdir)/include/
doxygen2man_CFLAGS = $(AM_CFLAGS) $(libqb_BUILD_CFLAGS) $(libxml_BUILD_CFLAGS)
doxygen2man_LDADD = $(top_builddir)/lib/libqb.la $(libxml_BUILD_LIBS)

