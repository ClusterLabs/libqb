# to build official release tarballs, handle tagging and publish.

gpgsignkey = A70D4537  # signing key

project = libqb

deliverables = $(project)-$(version).sha256 \
               $(project)-$(version).tar.gz \
               $(project)-$(version).tar.xz


.PHONY: all
all: tag tarballs sign  # first/last skipped per release/gpgsignkey respectively


.PHONY: checks
checks:
ifeq (,$(version))
	@echo ERROR: need to define version=
	@exit 1
endif
	@if [ ! -d .git ]; then \
		echo This script needs to be executed from top level cluster git tree; \
		exit 1; \
	fi


.PHONY: setup
setup: checks
	./autogen.sh
	./configure
	make maintainer-clean


.PHONY: tag
tag: setup ./tag-$(version)

tag-$(version):
ifeq (,$(release))
	@echo Building test release $(version), no tagging
	echo '$(version)' > .tarball-version
else
	# following will be captured by git-version-gen automatically
	git tag -a -m "v$(version) release" v$(version) HEAD
	@touch $@
endif


.PHONY: tarballs
tarballs: tag
	./autogen.sh
	./configure
	make distcheck


.PHONY: sha256
sha256: $(project)-$(version).sha256

# NOTE: dependency backtrack may fail trying to sign missing tarballs otherwise
$(deliverables): tarballs

$(project)-$(version).sha256:
	# checksum anything from deliverables except for in-prep checksums file
	sha256sum $(deliverables:$@=) | sort -k2 > $@


.PHONY: sign
ifeq (,$(gpgsignkey))
sign: $(deliverables)
	@echo No GPG signing key defined
else
sign: $(deliverables:=.asc)
endif

# NOTE: cannot sign multiple files at once
$(project)-$(version).%.asc: $(project)-$(version).%
	gpg --default-key "$(strip $(gpgsignkey))" \
		--detach-sign \
		--armor \
		$<


.PHONY: publish
publish:
ifeq (,$(release))
	@echo Building test release $(version), no publishing!
else
	@echo CHANGEME git push --follow-tags origin
	@echo : Since the switch to publishing non-generated, customized and signed
	@echo : upstream tarballs solely to GitHub, we cannot simply use scp anymore.
	@echo : TODO: investigate automation using GH API, directly or not:
	@echo : https://developer.github.com/v3/repos/releases/#upload-a-release-asset
	@echo : http://github3py.readthedocs.io/en/latest/repos.html#github3.repos.release.Release.upload_asset
	@echo : NOTE: precaution needs to be taken so as NOT TO LEAK the API token!
endif


.PHONY: clean
clean:
	rm -rf $(project)-* tag-* .tarball-version
