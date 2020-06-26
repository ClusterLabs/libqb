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
EXTRA_DIST 		= man.dox.in
noinst_HEADERS          = mainpage.h

dist_man_MANS = man8/qb-blackbox.8
if HAVE_DOXYGEN
if BUILD_MAN

doxygen2man_flags = -q -i qb/ -o man3/ -m -g -P -H "libqb Programmer's Manual"

dist_man3_MANS = man3/*.3

$(dist_man3_MANS): txt-man

txt-man: man.dox
	doxygen man.dox

xml-man: txt-man
	mkdir -p man3
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbarray_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbatomic_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbdefs_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbhdb_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbipcc_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbipc__common_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbipcs_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qblist_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qblog_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbloop_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbmap_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbrb_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbutil_8h.xml

all: $(dist_man3_MANS) xml-man

endif
endif

clean-local:
	rm -rf html man3 xml
