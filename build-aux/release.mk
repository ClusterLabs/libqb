# to build official release tarballs, handle tagging and publish.

project=libqb

all: sign


# subtargets of all target

checks:
ifeq (,$(version))
	@echo 'ERROR: need to define version='
	@exit 1
endif
	@if [ ! -d .git ]; then \
		echo 'This script needs to be executed from top level cluster git tree'; \
		exit 1; \
	fi

setup: checks
	./autogen.sh
	./configure
	make maintainer-clean

tag-$(version): setup
ifeq (,$(release))
	@echo 'Building test release $(version), no tagging'
else
ifeq (,$(gpgsignkey))
	git tag -a -m "v$(version) release" v$(version) HEAD
else
	git tag -u $(gpgsignkey) -m "v$(version) release" v$(version) HEAD
endif
	@touch $@
endif

tarballs: tag-$(version)
	./autogen.sh
	./configure
	MAKEFLAGS= $(MAKE) distcheck

$(project)-$(version).sha256: tarballs
ifeq (,$(release))
	@echo 'Building test release $(version), no sha256'
else
	sha256sum $(project)-$(version).tar.* | sort -k2 > $@
endif

$(project)-$(version).sha256.asc: $(project)-$(version).sha256
ifeq (,$(gpgsignkey))
	@echo 'No GPG signing key defined'
else
ifeq (,$(release))
	@echo 'Building test release $(version), no sign'
else
	gpg --default-key $(gpgsignkey) \
		--detach-sign \
		--armor \
		$<
endif
endif

sign: $(project)-$(version).sha256.asc


# backward compatibility targets

sha256: $(project)-$(version).sha256

tag: tag-$(version)


# extra targets

publish:
ifeq (,$(release))
	@echo 'Building test release $(version), no publishing!'
else
	@echo 'CHANGEME git push --follow-tags origin'
	@echo 'CHANGEME ...supposing branch has not yet been pushed...'
	@echo 'CHANGEME ...so as to achieve just a single CI build...'
	@echo 'CHANGEME ...otherwise:  git push --tags origin'
	@echo 'CHANGEME + put the tarballs to GitHub (ClusterLabs/$(project))'
endif

clean:
	rm -rf $(project)-* tag-*
