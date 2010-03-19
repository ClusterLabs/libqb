#
# Copyright (c) 2010 Red Hat, Inc.
#
# Authors: Andrew Beekhof
#	   Steven Dake (sdake@redhat.com)
#	   Angus Salkeld <asaslkeld@redhat.com>
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
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS		= -fPIC

AM_LDFLAGS		= -lpthread

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= libqbtimer.a
SHARED_LIBS		= $(lib_LIBRARIES:%.a=%.so.$(SONAME))
SHARED_LIBS_SO		= $(lib_LIBRARIES:%.a=%.so)
SHARED_LIBS_SO_TWO	= $(lib_LIBRARIES:%.a=%.so.$(SOMAJOR))

libqbtimer_a_SOURCES	= timer.c

noinst_HEADERS		= libqbtimer.versions 

if BUILD_DARWIN

lib%.so.$(SONAME): %.o
	$(CC) $(DARWIN_OPTS) $^ -o $@
	ln -sf lib$*.so.$(SONAME) lib$*.so
	ln -sf lib$*.so.$(SONAME) lib$*.so.$(SOMAJOR)

else

if BUILD_SOLARIS

lib%.so.$(SONAME): %.o
	$(LD) $(SOLARIS_OPTS) -G $^ -o $@
	ln -sf lib$*.so.$(SONAME) lib$*.so
	ln -sf lib$*.so.$(SONAME) lib$*.so.$(SOMAJOR)

else

libqb%.so.$(SONAME): %.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libqb$*.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libqb$*.versions \
		$^ $(LDFLAGS) $(AM_LDFLAGS)
	ln -sf libqb$*.so.$(SONAME) libqb$*.so
	ln -sf libqb$*.so.$(SONAME) libqb$*.so.$(SOMAJOR)

endif

endif

all-local: $(SHARED_LIBS)
	@echo Built shared libs

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	$(CP) -a $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir)/ && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg

