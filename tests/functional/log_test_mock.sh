#!/bin/sh
# Copyright 2018 Red Hat, Inc.
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

# Given the source RPM for libqb, this will run through the basic test matrix
# so as to figure out the outcomes for particular linker (pre-2.29 and 2.29+
# differing in visibility of orphan section delimiting boundary symbols and
# hence possibly causing harm to the logging facility of libqb) being used
# for particular part of the composite logging system (libqb itself,
# it's direct client / (client library + it's own client that uses logging)
# as well).  While this is tailored to Fedora, it should be possible to
# run this testsuite wherever following is present:
#
#   - rpm (for parsing archive name embedded in libqb.src.rpm [note that
#     rpm2cpio is part of rpm package as well] because the extracted dir
#     follows the same naming, which we need to know)
#   - mock (https://github.com/rpm-software-management/mock/)
#     + dependencies + fedora-27-${arch} configuration for mock
#     (or whatever other configuration if some variables below are
#     changed appropriately)
#   - koji (https://pagure.io/koji/) + dependencies (but binutils packages
#     can be precached when downloaded from https://koji.fedoraproject.org/
#     manually)
#   - internet connection (but see the above statement for koji, and
#     possibly the full package set within the build'n'test underlying
#     container can be precached without further details on "how")
#   - commons (coreutils, findutils, sed, ...)
#
# The testsuite will not mangle with your host system as mock spawns
# it's somewhat private container for be worked with under the hood.
#
# Note that in order not to get mad when entering the root password anytime
# mock is invoked, you can add the user initiating the test run to the
# 'mock' group.  Be aware of the associated security risks, though:
# https://github.com/rpm-software-management/mock/wiki#setup

set -eu

# change following as suitable

arch=x86_64
mock_args="-r fedora-27-${arch}"
pkg_binutils_228=binutils-2.28-14.fc27
#pkg_binutils_228=binutils-2.27-23.fc27  # alternatively test with 2.27 ...
pkg_binutils_229=binutils-2.29-6.fc27
#pkg_binutils_229=binutils-2.29.1-2.fc28  # alternatively test with 2.29.1

#
# prettified reporters
#

do_progress () { printf "\x1b[7m%s\x1b[0m\n" "$*"; }
do_info () { printf "\x1b[36mINFO: %s\x1b[0m\n" "$*"; }
do_warn () { printf "\x1b[31mWARNING: %s\x1b[0m\n" "$*"; }
do_die () { printf "\x1b[31mFATAL: %s\x1b[0m\n" "$*"; exit 1; }

#
# actual building blocks
#

# $1, ... $N: packages (and possibly related subpackages) to be downloaded
do_download () {
	while test $# -gt 0; do
		if test -d "_pkgs/$1" 2>/dev/null; then
			do_info "$1 already downloaded"
			shift; continue
		fi
		mkdir -p "_pkgs/$1"
		( cd "_pkgs/$1" && koji download-build --arch="${arch}" "$1" )
		shift
	done
}

# $1, ... $N: descriptors of packages to be installed
do_install () {
	while test $# -gt 0; do
		if test -d "_pkgs/$1" 2>/dev/null; then
			do_install_inner "_pkgs/$1"/*.rpm
		else
			do_warn "$1 is not downloaded, hence skipped"
		fi
		shift
	done
}

# $1, ... $N: concrete packages to be installed
do_install_inner () {
	_remove_cmd="mock ${mock_args} -- shell \"rpm --nodeps --erase"
	_install_cmd="mock ${mock_args}"
	while test $# -gt 0; do
		case "$1" in
		*.src.rpm|*-debuginfo*|*-debugsource*) ;;
		*)
			_pkg_name="$(basename "$1" | sed 's|\(-[0-9].*\)||')"
			_remove_cmd="${_remove_cmd} \'${_pkg_name}\'"
			_install_cmd="${_install_cmd} --install \"$1\"";;
		esac
		shift
	done
	eval "${_remove_cmd}\"" || :  # extra quotation mark intentional
	eval "${_install_cmd}"
}

# $1: full path of srpm to be rebuilt
# $2: %{dist} macro for rpmbuild (distinguishing the builds)
do_buildsrpm () {
	_pkg_descriptor="$(basename "$1" | sed 's|\.src\.rpm$||')"
	# need to prune due to possible duplicates caused by differing %{dist}
	rm -f -- "_pkgs/${_pkg_descriptor}"/*
	mock ${mock_args} -Nn --define "dist $2" --define '_without_check 1' \
	  --resultdir "_pkgs/${_pkg_descriptor}" --rebuild "$1"
}

# $1: full path srpm to be rebuilt
# $2: extra (presumably) variable assignments for the make goal invocation
do_compile_interlib () {
	mock ${mock_args} --shell \
		"find \"builddir/build/BUILD/$1/tests/functional\" \
		\( -name log_internal -o -name '*.c' \) -prune \
		-o -name '*liblog_inter*' \
		-exec rm -- {} \;"
	mock ${mock_args} --shell "( cd \"builddir/build/BUILD/$1\"; ./configure )"
	mock ${mock_args} --shell \
		"make -C \"builddir/build/BUILD/$1/tests/functional/log_external\" \
		liblog_inter.la $2"
}

# $1: full path srpm to be rebuilt
# $2: which type of client to work with (client/interclient)
# $3: base (on-host) directory for test results
# $4: output file to capture particular test result
# $5: extra (presumably) variable assignments for the make goal invocation
do_compile_and_test_client () {
	_result=$4
	case "$2" in
	interclient)
		_logfile=log_test_interlib_client
		mock ${mock_args} --shell \
			"find \"builddir/build/BUILD/$1/tests/functional\" \
			\( -name log_internal -o -name '*.err' -o -name '*.c' \) -prune \
			-o \( -name '*log_interlib_client*' -o -name \"${_logfile}.log\" \) \
			-exec rm -- {} \;"
		;;
	client|*)
		_logfile=log_test_client
		mock ${mock_args} --shell \
			"find \"builddir/build/BUILD/$1/tests/functional\" \
			\( -name log_internal -o -name '*.err' -o -name '*.c' \) -prune \
			-o \( -name '*log_client*' -o -name \"${_logfile}.log\" \) \
			-exec rm -- {} \;"
		;;
	esac
	mock ${mock_args} --copyin "syslog-stdout.py" "builddir"
	mock ${mock_args} --shell "( cd \"builddir/build/BUILD/$1\"; ./configure )"
	mock ${mock_args} --shell \
		"python3 builddir/syslog-stdout.py \
		  >\"builddir/build/BUILD/$1/tests/functional/log_external/.syslog\" & \
		{ sleep 2; make -C \"builddir/build/BUILD/$1/tests/functional/log_external\" \
		  check-TESTS \"TESTS=../${_logfile}.sh\" $5; } \
		&& ! test -s \"builddir/build/BUILD/$1/tests/functional/log_external/.syslog\"; \
		ret_ec=\$?; \
		( cd \"builddir/build/BUILD/$1/tests/functional/log_external\"; \
		  cat .syslog >> test-suite.log; \
		  echo SYSLOG-begin; cat .syslog; echo SYSLOG-end ); \
		ret () { return \$1; }; ret \${ret_ec}" \
	  && _result="${_result}_good" \
	  || _result="${_result}_bad"
	mock ${mock_args} --copyout \
		"builddir/build/BUILD/$1/tests/functional/log_external/test-suite.log" \
		"$3/${_result}"
}

do_shell () {
	mock ${mock_args} --shell
}

# $1, ... $N: "argv"
do_proceed () {

	_makevars=
	_resultsdir_tag=
	_clientselfcheck=1
	_interlibselfcheck=1
	_clientlogging=1
	_interliblogging=1
	while :; do
		case "$1" in
		shell) shift; do_shell "$@"; return;;
		-v)    _makevars="${_makevars} V=1"; shift;;
		-nsc)  case "${_resultsdir_tag}" in
		       *sc*) do_die "do not combine \"sc\" flags";; esac
		       _resultsdir_tag="${_resultsdir_tag}$1"; shift;
		       _clientselfcheck=0; _interlibselfcheck=0;;
		-ncsc) case "${_resultsdir_tag}" in
		       *sc*) do_die "do not combine \"sc\" flags";; esac
		       _resultsdir_tag="${_resultsdir_tag}$1"; shift; _clientselfcheck=0;;
		-nisc) case "${_resultsdir_tag}" in
		       *sc*) do_die "do not combine \"sc\" flags";; esac
		       _resultsdir_tag="${_resultsdir_tag}$1"; shift; _interlibselfcheck=0;;
		-ncl)  _resultsdir_tag="${_resultsdir_tag}$1"; shift; _clientlogging=0;;
		-nil)  _resultsdir_tag="${_resultsdir_tag}$1"; shift; _interliblogging=0;;
		-*)    do_die "Uknown option: $1";;
		*)     break;;
		esac
	done

	if test -n "${_resultsdir_tag}"; then
		_makevars="${_makevars} CPPFLAGS=\" \
		           $(test "${_clientselfcheck}" -eq 1 || printf %s ' -DNSELFCHECK') \
		           $(test "${_interlibselfcheck}" -eq 1 || printf %s ' -DNLIBSELFCHECK') \
		           $(test "${_clientlogging}" -eq 1 || printf %s ' -DNLOG') \
		           $(test "${_interliblogging}" -eq 1 || printf %s ' -DNLIBLOG') \
		           \""
		_makevars=$(echo ${_makevars})
	fi

	test -s "$1" || do_die "Not an input file: $1"
	_libqb_descriptor_path="$1"
	_libqb_descriptor="$(basename "${_libqb_descriptor_path}" \
	                     | sed 's|\.src\.rpm$||')"
	_libqb_descriptor_archive="$(rpm -q --qf '[%{FILENAMES}\n]' \
	                             -p "${_libqb_descriptor_path}" \
	                             | sed -n '/\.tar/{s|\.tar\.[^.]*$||;p;q}')"

	_resultsdir="_results/$(date '+%y%m%d_%H%M%S')_${_libqb_descriptor}${_resultsdir_tag}"
	mkdir -p "${_resultsdir}"
	rm -f -- "${_resultsdir}/*"

	_dist=
	_outfile=
	_outfile_client=
	_outfile_qb=

	do_download "${pkg_binutils_228}" "${pkg_binutils_229}"

	for _pkg_binutils_libqb in "${pkg_binutils_228}" "${pkg_binutils_229}"; do

		case "${_pkg_binutils_libqb}" in
		${pkg_binutils_228}) _outfile_qb="qb+"; _dist=.binutils228;;
		${pkg_binutils_229}) _outfile_qb="qb-"; _dist=.binutils229;;
		*) _outfile_qb="?";;
		esac

		case "${_picktest}" in
		""|${_outfile_qb}*) ;;
		*) do_progress "skipping '${_outfile_qb}' branch (no match with '${_picktest}')"
		   continue;;
		esac

		do_progress "installing ${_pkg_binutils_libqb} so as to build" \
		            "libqb [${_outfile_qb}]"
		do_install "${_pkg_binutils_libqb}"

		do_progress "building ${_libqb_descriptor_path} with" \
		            "${_pkg_binutils_libqb} [${_outfile_qb}]"
		do_buildsrpm "${_libqb_descriptor_path}" "${_dist}"

		do_progress "installing ${_libqb_descriptor}-based packages" \
		            "built with ${_pkg_binutils_libqb} [${_outfile_qb}]"
		do_install "${_libqb_descriptor}"
		# from now on, we can work fully offline, also to speed
		# the whole thing up (and not to bother the mirrors)
		mock_args="${mock_args} --offline"

		for _pkg_binutils_interlib in none "${pkg_binutils_228}" "${pkg_binutils_229}"; do

			case "${_pkg_binutils_interlib}" in
			none) _outfile="${_outfile_qb}";;
			${pkg_binutils_228}) _outfile="${_outfile_qb}_il+";;
			${pkg_binutils_229}) _outfile="${_outfile_qb}_il-";;
			*) _outfile="${_outfile_qb}_?";;
			esac

			case "${_picktest}" in
			""|"${_outfile}*")
				case "${_pkg_binutils_interlib}" in
				none)	;;
				*)
					do_progress "installing ${_pkg_binutils_interlib}" \
					            "so as to build interlib [${_outfile}]"
					do_install "${_pkg_binutils_interlib}"

					do_progress "building interlib with ${_libqb_descriptor_archive}" \
					            "+ ${_pkg_binutils_interlib} [${_outfile}]" \
					            "{${_makevars}}"
					do_compile_interlib "${_libqb_descriptor_archive}" "${_makevars}"
					;;
				esac;;
			*) do_progress "skipping dealing with interlib (no match with '${_picktest}')";;
			esac

			for _pkg_binutils_client in "${pkg_binutils_228}" "${pkg_binutils_229}"; do

				_client=client
				test "${_pkg_binutils_interlib}" = none || _client=interclient

				case "${_pkg_binutils_client}" in
				${pkg_binutils_228}) _outfile_client="${_outfile}_c+";;
				${pkg_binutils_229}) _outfile_client="${_outfile}_c-";;
				*) _outfile_client="${_outfile}_?";;
				esac

				test -n "${_picktest}" && test "${_picktest}" != "${_outfile_client}" \
				  && continue

				do_progress "installing ${_pkg_binutils_client}" \
				            "so as to build ${_client} [${_outfile_client}]"
				do_install "${_pkg_binutils_client}"
				do_progress "building ${_client} with ${_libqb_descriptor_archive}" \
				            "+ ${_pkg_binutils_client} [${_outfile_client}]" \
				            "{${_makevars}}"
				do_compile_and_test_client "${_libqb_descriptor_archive}" \
				                           "${_client}" "${_resultsdir}" \
				                           "${_outfile_client}" "${_makevars}"
			done
		done
	done
}

{ test $# -eq 0 || test "$1" = -h || test "$1" = --help; } \
  && printf '%s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n %s\n' \
            "usage: $0 {[-{v,n{{,c,i}sc,cl,il}}]* <libqb.src.rpm> | shell}" \
            "- use '-v' to show the compilation steps verbosely" \
            "- use '-nsc' to suppress self-check (\"see whole story\") wholly" \
            "- use '-ncsc' to suppress self-check client-side only" \
            "- use '-nisc' to suppress self-check interlib-side only" \
            "- use '-ncl' to suppress client-side logging" \
            "- use '-nil' to suppress interlib-side logging" \
            "- 'make -C ../.. srpm' (or so) can generate the requested input" \
            "   (in that case, apparently, refer to '../../libqb-X.src.rpm')" \
            "- _pkgs dir caches (intermediate or not) packages to work with" \
            "- results stored in '_results/<timestamp>_<input_name>[_<tag>]'" \
  || do_proceed "$@"
