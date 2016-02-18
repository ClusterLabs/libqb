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

dependent_headers = $(subst $(inc_dir),,$(shell \
	printf 'include $(inc_dir)/Makefile.am\n\n%%.var:\n\t@echo $$($$*)' \
	| ${MAKE} --no-print-directory -f - inst_HEADERS.var \
	|| echo $(inc_dir)/qb*.h))
dependent_headers_omit = qbconfig.h qbdefs.h qbrb.h

dist_man_MANS += $(patsubst %,man3/%.3,$(filter-out \
		     $(dependent_headers_omit),$(dependent_headers) \
		 ))

$(dist_man_MANS): man.dox $(dependent_headers:%=$(inc_dir)/%)
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
