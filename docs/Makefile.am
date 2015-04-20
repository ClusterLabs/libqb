# Copyright (C) 2010 Red Hat, Inc.
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

MAINTAINERCLEANFILES	= Makefile.in
EXTRA_DIST 		= man.dox html.dox
noinst_HEADERS          = mainpage.h

dist_man_MANS = man8/qb-blackbox.8
if HAVE_DOXYGEN
inc_dir = $(top_srcdir)/include/qb
dependant_headers = $(wildcard $(inc_dir)/qb*.h)
dist_man_MANS += man3/qbipcc.h.3 man3/qbipcs.h.3 man3/qbatomic.h.3 \
		 man3/qbhdb.h.3 man3/qbipc_common.h.3 man3/qblist.h.3 \
		 man3/qbloop.h.3 man3/qbutil.h.3 man3/qbarray.h.3 \
		 man3/qblog.h.3 man3/qbmap.h.3


$(dist_man_MANS): man.dox $(dependant_headers)
	mkdir -p man3
	doxygen man.dox

doxygen: html.dox
	mkdir -p html
	doxygen html.dox

else
doxygen:
	@echo WARNING: no doxygen to build man pages!
endif

clean-generic:
	rm -rf html man3
