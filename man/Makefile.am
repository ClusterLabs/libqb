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
EXTRA_DIST 		= Doxyfile

dist_man_MANS = man3/qbtsafe.h.3 man3/qbhdb.h.3 man3/qblist.h.3 \
	       	man3/qbpoll.h.3 man3/qbtimer.h.3 man3/qbplugin.h.3 \
		man3/qbplugin_comp.h.3 man3/qbipcc.h.3 man3/qbipcs.h.3 \
		man3/qbqueue.h.3 man3/qbwthread.h.3 man3/qblogsys.h.3 \
		man3/qbhash.h.3 man3/qbipc_common.h.3

inc_dir = $(top_srcdir)/include/qb

dependant_headers = $(inc_dir)/qbtsafe.h $(inc_dir)/qbhdb.h $(inc_dir)/qblist.h \
		    $(inc_dir)/qbpoll.h $(inc_dir)/qbtimer.h $(inc_dir)/qbplugin.h \
		    $(inc_dir)/qbplugin_comp.h $(inc_dir)/qbipcc.h $(inc_dir)/qbipc_common.h \
		    $(inc_dir)/qbipcs.h $(inc_dir)/qbqueue.h $(inc_dir)/qbwthread.h \
		    $(inc_dir)/qblogsys.h $(inc_dir)/qbhash.h

$(dist_man_MANS): Doxyfile $(dependant_headers)
	doxygen Doxyfile

