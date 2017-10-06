# Copyright 2017 Red Hat, Inc.
#
# Author: Jan Pokorny <jpokorny@redhat.com>
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

include ../log.am

# while linking with system-wide version of libqb, we are still pursuing
# local in-tree header file, hence we need to link with dynamic linking
# library (which is a prerequisite for using QB_LOG_INIT_DATA defined
# in qblog.h) explicitly
log_client_LDFLAGS = -lqb @client_dlopen_LIBS@
liblog_inter_la_LIBADD = -lqb @client_dlopen_LIBS@

# extras for log_external: comparable, generated callsite-heavy logging clients

if HAVE_PYTHON
noinst_PROGRAMS += log_callsite_bench_sectionfull log_callsite_bench_sectionless
endif

log_callsite_bench_sectionfull_SOURCES = ../log_callsite_bench.c
log_callsite_bench_sectionfull_LDFLAGS = -lqb

log_callsite_bench_sectionless_SOURCES = ../log_callsite_bench.c
log_callsite_bench_sectionless_LDFLAGS = -lqb
log_callsite_bench_sectionless_CPPFLAGS = -DQB_KILL_ATTRIBUTE_SECTION
