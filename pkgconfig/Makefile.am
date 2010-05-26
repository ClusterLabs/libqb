# Copyright (c) 2009 Red Hat, Inc.
#
# All rights reserved.
#
# This software licensed under BSD license, the text of which follows:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR ENGINES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

MAINTAINERCLEANFILES	= Makefile.in

EXTRA_DIST		= libtemplate.pc.in

LIBS			= qbipcc qbipcs qbhash qblogsys qbpoll qbtimer qbtsafe qbwthread qbutil

qbipcc_LIBS = -lqbutil
qbipcs_LIBS = -lqbutil
qbhash_LIBS = -lqbutil
qblogsys_LIBS = -lqbutil
qbtimer_LIBS = -lqbutil
qbpoll_LIBS = "-lqbutil -lrt"
qbwthread_LIBS = -lqbutil
qbtsafe_LIBS =
qbutil_LIBS =

target_LIBS = $(LIBS:%=lib%.pc)

lib%.pc: libtemplate.pc.in Makefile
	rm -f $@-t $@
	sed \
		-e 's#@''PREFIX@#$(exec_prefix)#g' \
		-e 's#@''LIBDIR@#$(libdir)#g' \
		-e 's#@''LIBVERSION@#$(VERSION)#g' \
		-e 's#@''LIBNAME@#'$(*:lib%=%)'#g' \
		-e 's#@''LIBS@#'-l$(*:lib%=%)\ $($(*:lib%=%)_LIBS)'#g' \
	    $< > $@-t
	chmod a-w $@-t
	mv $@-t $@

all-local: $(target_LIBS)

install-exec-local: $(target_LIBS) $(target_PACKAGE)
	$(INSTALL) -d $(DESTDIR)/$(libdir)/pkgconfig
	$(INSTALL) -m 644 $(target_LIBS) $(target_PACKAGE) $(DESTDIR)/$(libdir)/pkgconfig

uninstall-local:
	cd $(DESTDIR)/$(libdir)/pkgconfig && rm -f $(target_LIBS) $(target_PACKAGE)
	rmdir $(DESTDIR)/$(libdir)/pkgconfig 2> /dev/null || :

clean-local:
	rm -f *.pc
