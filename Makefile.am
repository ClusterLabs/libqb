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

EXTRA_DIST		= autogen.sh $(SPEC).in \
			  build-aux/git-version-gen \
			  build-aux/gitlog-to-changelog \
			  build-aux/release.mk \
			  .version

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf libtool libtoolize \
			  ltmain.sh compile


ACLOCAL_AMFLAGS		= -I m4

dist_doc_DATA		= COPYING INSTALL README.markdown

SUBDIRS			= include lib docs tests examples

doxygen:
	$(MAKE) -C docs doxygen

dist-clean-local:
	$(AM_V_GEN)rm -f autoconf automake autoheader

maintainer-clean-local:
	$(AM_V_GEN)rm -rf m4
	$(AM_V_GEN)rm -f .version .tarball-version

clean-generic:
	$(AM_V_GEN)rm -rf $(SPEC) $(TARFILE)

## make rpm/srpm section.

$(SPEC): $(SPEC).in
	$(AM_V_GEN)rm -f $@-t $@
	LC_ALL=C date="$(shell date "+%a %b %d %Y")" && \
	if [ -f .tarball-version ]; then \
		gitver="$(shell cat .tarball-version)" && \
		rpmver=$$gitver && \
		alphatag="" && \
		dirty="" && \
		numcomm="0"; \
	else \
		gitver="$(shell git describe --abbrev=4 --match='v*' HEAD 2>/dev/null)" && \
		rpmver=`echo $$gitver | sed -e "s/^v//" -e "s/-.*//g"` && \
		alphatag=`echo $$gitver | sed -e "s/.*-//" -e "s/^g//"` && \
		vtag=`echo $$gitver | sed -e "s/-.*//g"` && \
		numcomm=`git rev-list $$vtag..HEAD | wc -l` && \
		git update-index --refresh > /dev/null 2>&1 || true && \
		dirty=`git diff-index --name-only HEAD 2>/dev/null`; \
	fi && \
	if [ -n "$$dirty" ]; then dirty="dirty"; else dirty=""; fi && \
	if [ "$$numcomm" = "0" ]; then \
		sed \
			-e "s#@version@#$$rpmver#g" \
			-e "s#%glo.*alpha.*##g" \
			-e "s#%glo.*numcomm.*##g" \
			-e "s#@dirty@#$$dirty#g" \
			-e "s#@date@#$$date#g" \
		$< > $@-t; \
	else \
		sed \
			-e "s#@version@#$$rpmver#g" \
			-e "s#@alphatag@#$$alphatag#g" \
			-e "s#@numcomm@#$$numcomm#g" \
			-e "s#@dirty@#$$dirty#g" \
			-e "s#@date@#$$date#g" \
		$< > $@-t; \
	fi; \
	if [ -z "$$dirty" ]; then sed -i -e "s#%glo.*dirty.*##g" $@-t; fi
	$(AM_V_GEN)chmod a-w $@-t
	$(AM_V_GEN)mv $@-t $@

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

# release/versioning
BUILT_SOURCES	= .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook: gen-ChangeLog
	echo $(VERSION) > $(distdir)/.tarball-version

gen_start_date = 2000-01-01
.PHONY: gen-ChangeLog
gen-ChangeLog:
	if test -d .git; then						\
		$(top_srcdir)/build-aux/gitlog-to-changelog		\
			--since=$(gen_start_date) > $(distdir)/cl-t;	\
		rm -f $(distdir)/ChangeLog;				\
		mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi
