all check:
	# do not trigger automatically, it's for on-demand use
	@echo "Use 'make $@' within particular subdirectories"
install: force
	# definitely not desired to install anything from this subtree
distclean:
	# following is a nasty hack to keep "make distclean" succeeding
	# (problem mostly arises from shared object files and hence shared
	# compiler-generated makefile includes which are swiped when
	# processing one subdir and missing as hard error for the other)
	@$(MAKE) -C log_external $@
	@mkdir .deps
	@touch .deps/log_client.Po .deps/log_interlib.Plo .deps/log_interlib_client.Po
	@$(MAKE) -C log_internal $@
	@$(MAKE) -f Makefile $@ SUBDIRS=
%: force
	@$(MAKE) -f Makefile $@
force: ;

.PHONY: check distclean force install
