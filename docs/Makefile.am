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
EXTRA_DIST 		= common.dox html.dox man.dox
noinst_HEADERS          = mainpage.h

dist_man_MANS = man8/qb-blackbox.8
if HAVE_DOXYGEN
inc_dir = $(top_srcdir)/include/qb
public_headers = $(sort $(patsubst %.in,%,$(subst $(inc_dir)/,,$(shell \
	printf 'include $(inc_dir)/Makefile.am\n\n%%.var:\n\t@echo $$($$*)' \
	| MAKEFLAGS= ${MAKE} --no-print-directory -f- inst_HEADERS.var \
	|| echo $(inc_dir)/qb*.h*))))
public_headers_omit = qbconfig.h
public_headers_pick = $(filter-out $(public_headers_omit),$(public_headers) )

dist_man3_MANS = $(patsubst %,man3/%.3,$(public_headers_pick))
doxygen2man_flags = -q -i qb/ -o man3/ -m -P -H "libqb Programmer's Manual"

$(dist_man3_MANS): man.dox $(public_headers_pick:%=$(inc_dir)/%)
	mkdir -p man3
	doxygen man.dox
if BUILD_MAN
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbarray_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbatomic_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbconfig_8h.xml
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
endif

doxygen: html.dox
	mkdir -p html
	doxygen html.dox

else
doxygen:
	@echo WARNING: no doxygen to build man pages!
endif

clean-local:
	rm -rf html man3
