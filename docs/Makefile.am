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

inc_dir = $(top_srcdir)/include/qb
dependant_headers = $(wildcard $(inc_dir)/qb*.h)
dist_man_MANS = $(addsuffix .3,$(subst $(inc_dir),man3,$(dependant_headers)))

$(dist_man_MANS): man.dox $(dependant_headers)
	mkdir -p man3
	doxygen man.dox

doxygen: html.dox
	mkdir -p html
	doxygen html.dox

clean-generic:
	rm -rf html man3
	
