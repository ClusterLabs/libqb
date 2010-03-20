# Copyright (c) 2004 MontaVista Software, Inc.
# Copyright (c) 2010 Red Hat, Inc.
#
# Authors: Angus Salkeld <asalkeld@redhat.com>
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
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

MAINTAINERCLEANFILES	= Makefile.in
EXTRA_DIST 		= Doxyfile

dist_man_MANS = man3/qbtsafe.h.3 man3/qbhdb.h.3 man3/qblist.h.3 \
	       	man3/qbpoll.h.3 man3/qbtimer.h.3 man3/qbplugin.h.3 \
		man3/qbplugin_comp.h.3 man3/qbipcc.h.3 man3/qbipcs.h.3 \
		man3/qbqueue.h.3 man3/qbwthread.h man3/qblogsys.h.3

inc_dir = $(top_srcdir)/include/qb

dependant_headers = $(inc_dir)/qbtsafe.h $(inc_dir)/qbhdb.h $(inc_dir)/qblist.h \
		    $(inc_dir)/qbpoll.h $(inc_dir)/qbtimer.h $(inc_dir)/qbplugin.h \
		    $(inc_dir)/qbplugin_comp.h $(inc_dir)/qbipcc.h \
		    $(inc_dir)/qbipcs.h $(inc_dir)/qbqueue.h $(inc_dir)/qbwthread.h \
		    $(inc_dir)/qblogsys.h

$(dist_man_MANS): Doxyfile $(dependant_headers)
	doxygen Doxyfile

