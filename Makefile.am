# Copyright (C) 2010 Red Hat, Inc.
#
# Authors: Andrew Beekhof <abeekhof@redhat.com>
#	   Steven Dake <sdake@redhat.com>
#	   Angus Salkeld <asalkeld@redhat.com>
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

SPEC			= $(PACKAGE_NAME).spec

TARFILE			= $(PACKAGE_NAME)-$(VERSION).tar.gz

EXTRA_DIST		= autogen.sh $(SPEC).in

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf libtool libtoolize \
			  ltmain.sh compile


ACLOCAL_AMFLAGS		= -I m4

dist_doc_DATA		= COPYING INSTALL README TODO 

SUBDIRS			= include lib docs tests
#pkgconfig

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/${SOCKETDIR}

uninstall-local:
	rmdir $(DESTDIR)/${SOCKETDIR} || :;

lint:
	$(MAKE) -C lib lint

doxygen:
	$(MAKE) -C docs doxygen

dist-clean-local:
	rm -f autoconf automake autoheader

maintainer-clean-local:
	rm -rf m4

clean-generic:
	rm -rf docs/html docs/man3 $(SPEC) $(TARFILE) 

## make rpm/srpm section.

$(SPEC): $(SPEC).in
	rm -f $@-t $@
	LC_ALL=C date="$(shell date "+%a %b %d %Y")" && \
	alphatag="$(shell svnversion | sed -e "s#.*:##g" -e "s#[MS]##g")" && \
	sed \
		-e "s#@alphatag@#r$$alphatag#g" \
		-e "s#@version@#$(VERSION)#g" \
		-e "s#@date@#$$date#g" \
	$< > $@-t
	chmod a-w $@-t
	mv $@-t $@

$(TARFILE):
	$(MAKE) dist

RPMBUILDOPTS	= --define "_sourcedir $(abs_builddir)" \
		  --define "_specdir $(abs_builddir)" \
		  --define "_builddir $(abs_builddir)" \
		  --define "_srcrpmdir $(abs_builddir)" \
		  --define "_rpmdir $(abs_builddir)"

srpm: clean
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(RPMBUILDOPTS) --nodeps -bs $(SPEC)

rpm: clean
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(RPMBUILDOPTS) -ba $(SPEC)

