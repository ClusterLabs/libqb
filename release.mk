# NOTE: this make file snippet is only used by the release managers
# to build official release tarballs, handle tagging and publish.
#
# this script is NOT "make -j" safe
#
# do _NOT_ use for anything else!!!!!!!!!
#
# make -f release.mk version=<x.y.z> oldversion=<a.b.c> [release]
#

# setup tons of vars

# signing key
gpgsignkey=956EEFB5

# project layout
project=libqb
projectver=$(project)-$(version)
projecttar=$(projectver).tar
projectgz=$(projecttar).gz
projectbz=$(projecttar).bz2


# temp dirs

ifdef release
reldir=release
gitver=$(projectver)
forceclean=clean
else
reldir=release-candidate
gitver=HEAD
forceclean=
endif

releasearea=$(shell pwd)/../$(projectver)-$(reldir)

all: $(forceclean) setup tag tarballs changelog sha256 sign

checks:
ifeq (,$(version))
	@echo ERROR: need to define version=
	@exit 1
endif
ifeq (,$(oldversion))
	@echo ERROR: need to define oldversion=
	@exit 1
endif
	@if [ ! -d .git ]; then \
		echo This script needs to be executed from top level cluster git tree; \
		exit 1; \
	fi

setup: checks $(releasearea)

$(releasearea):
	mkdir $@

tag: setup $(releasearea)/tag-$(version)

$(releasearea)/tag-$(version):
ifeq (,$(release))
	@echo Building test release $(version), no tagging
else
	git tag -a -m "$(projectver) release" $(projectver) HEAD
endif
	@touch $@

tarballs: tag
tarballs: $(releasearea)/$(projecttar)
tarballs: $(releasearea)/$(projectgz)
tarballs: $(releasearea)/$(projectbz)

$(releasearea)/$(projecttar):
	@echo Creating $(project) tarball
	rm -rf $(releasearea)/$(projectver)
	git archive \
		--format=tar \
		--prefix=$(projectver)/ \
		$(gitver) | \
		(cd $(releasearea)/ && tar xf -)
	cd $(releasearea) && \
	tar cpf $(projecttar) $(projectver) && \
	rm -rf $(projectver)

	#sed -i -e \
	#	's#<CVS>#$(version)#g' \
	#	$(projectver)/gfs-kernel/src/gfs/gfs.h && \
	#echo "VERSION \"$(version)\"" \
	#	>> $(projectver)/make/official_release_version && \

$(releasearea)/%.gz: $(releasearea)/%
	@echo Creating $@
	cat $< | gzip -9 > $@

$(releasearea)/%.bz2: $(releasearea)/%
	@echo Creating $@
	cat $< | bzip2 -c > $@

changelog: checks setup $(releasearea)/Changelog-$(version)

$(releasearea)/Changelog-$(version): $(releasearea)/$(projecttar)
	git log $(project)-$(oldversion)..$(gitver) | \
	git shortlog > $@
	git diff --stat $(project)-$(oldversion)..$(gitver) >> $@

sha256: changelog tarballs $(releasearea)/$(projectver).sha256

$(releasearea)/$(projectver).sha256: $(releasearea)/Changelog-$(version)
	cd $(releasearea) && \
	sha256sum Changelog-$(version) *.gz *.bz2 | sort -k2 > $@

sign: sha256 $(releasearea)/$(projectver).sha256.asc

$(releasearea)/$(projectver).sha256.asc: $(releasearea)/$(projectver).sha256
	cd $(releasearea) && \
	gpg --default-key $(gpgsignkey) \
		--detach-sign \
		--armor \
		$<

publish: sign
ifeq (,$(release))
	@echo Nothing to publish
else
	git push --tags origin
	cd $(releasearea) && \
	scp *.gz *.bz2 Changelog-* *sha256* \
		fedorahosted.org:quarterback
	@echo Hey you!.. yeah you looking somewhere else!
	@echo remember to update the wiki and send the email to quarterback-devel
endif

clean: checks
	rm -rf $(releasearea)

