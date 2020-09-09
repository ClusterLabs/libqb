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

EXTRA_DIST		= autogen.sh $(SPEC).in \
			  build-aux/git-version-gen \
			  build-aux/gitlog-to-changelog \
			  build-aux/release.mk \
			  .version

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf libtool libtoolize \
			  ltmain.sh compile build-aux/test-driver


ACLOCAL_AMFLAGS		= -I m4

dist_doc_DATA		= COPYING INSTALL README.markdown

SUBDIRS			= include lib doxygen2man docs tools tests examples

dist-clean-local:
	rm -f .snapshot-version autoconf automake autoheader

# this will also get rid of "libtoolized" m4 files
maintainer-clean-local:
	rm -f .version .tarball-version
	rm -f $(patsubst $(top_srcdir)/m4/ax_%,,$(wildcard $(top_srcdir)/m4/*.m4))

clean-local:
	rm -rf $(SPEC) $(DIST_ARCHIVES)

## make rpm/srpm section.

$(SPEC): $(SPEC).in
	@rm -f $@-t $@
	@date="$(shell LC_ALL=C date "+%a %b %d %Y")" && \
	if [ -s .tarball-version ] || [ -n "$$(echo '${VERSION}' | grep -v UNKNOWN)" ]; then \
		ver="$$({ cat .tarball-version 2>/dev/null; echo '${VERSION}'; } | head -n1)" && \
		gitver=$$(echo $$ver | sed \
		          -e 's|^\(v[0-9][0-9]*\.[0-9][0-9]*\)\([^.].*\)\?$$|\1.0\2|') && \
		rpmver=$$(echo $$gitver | sed 's/-.*//') && \
		alphatag="" && \
		dirty=$$(echo $$gitver | sed -n 's/[^-][^-]*-//p') && \
		numcomm="0"; \
	else \
		ver="$(shell git describe --abbrev=4 --match='v*' --tags HEAD 2>/dev/null)" && \
		gitver=$$(echo $$ver | sed \
		          -e 's|^\(v[0-9][0-9]*\.[0-9][0-9]*\)\([^.].*\)\?$$|\1.0\2|') && \
		rpmver=$$(echo $$gitver | sed -e "s/^v//" -e "s/-.*//g") && \
		alphatag=$$(echo $$gitver | sed -e "s/.*-//" -e "s/^g//") && \
		vtag=$$(echo $$ver | sed -e "s/-.*//g") && \
		numcomm=$$(git rev-list $$vtag..HEAD | wc -l) && \
		git update-index --refresh > /dev/null 2>&1 || true && \
		dirty=$$(git diff-index --name-only HEAD 2>/dev/null | sed 's/..*/dirty/'); \
	fi && \
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
	@chmod a-w $@-t
	$(AM_V_GEN)mv $@-t $@

RPMBUILDOPTS	= --define "_sourcedir $(abs_builddir)" \
		  --define "_specdir $(abs_builddir)" \
		  --define "_builddir $(abs_builddir)" \
		  --define "_srcrpmdir $(abs_builddir)" \
		  --define "_rpmdir $(abs_builddir)"

# generates both .tar.[gx]z (backward compatibility)
tarball: dist

srpm: clean
	autoreconf -if
	$(MAKE) $(SPEC) dist-xz
	rpmbuild $(RPMBUILDOPTS) --nodeps -bs $(SPEC)

rpm: clean
	autoreconf -if
	$(MAKE) $(SPEC) dist-xz
	rpmbuild $(RPMBUILDOPTS) -ba $(SPEC)

# release/versioning
BUILT_SOURCES	= .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

# untaint configure.ac when modified upon obtaining the "yanked" snapshot form
dist-hook: gen-ChangeLog
	echo $(SOURCE_EPOCH) > $(distdir)/source_epoch
	echo $(VERSION) | tee $(distdir)/.tarball-version | grep -Eqv '\-yank' \
	  || sed "s/\(.*git-version-gen[^']*[']\)[^']*/\1\$$Format:%h??%D\$$/" \
	     $(distdir)/configure.ac > $(builddir)/configure.ac-t
	if [ -f $(builddir)/configure.ac-t ]; then \
		touch -r $(distdir)/configure.ac $(builddir)/configure.ac-t; \
		chmod u+w $(builddir)/configure.ac-t; \
		mv $(builddir)/configure.ac-t $(distdir)/configure.ac; \
	fi

gen_start_date = 2000-01-01
.PHONY: gen-ChangeLog
gen-ChangeLog:
	if test -d .git; then						\
		$(top_srcdir)/build-aux/gitlog-to-changelog		\
			--since=$(gen_start_date) > $(distdir)/cl-t;	\
		rm -f $(distdir)/ChangeLog;				\
		mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi
