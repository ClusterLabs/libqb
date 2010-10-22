
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AC_INIT([libqb], [0.3.0], [quarterback-devel@fedorahosted.org])
AC_CONFIG_SRCDIR([lib/ringbuffer.c])
AC_CONFIG_HEADERS([include/config.h])
AM_INIT_AUTOMAKE([-Wno-portability])
LT_PREREQ([2.2.6])
LT_INIT

AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST
AC_PROG_LIBTOOL

AC_LANG([C])

dnl Fix default variables - "prefix" variable if not specified
if test "$prefix" = "NONE"; then
	prefix="/usr"
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

if test "$srcdir" = "."; then
	AC_MSG_NOTICE([building in place srcdir:$srcdir])
	AC_DEFINE([BUILDING_IN_PLACE], 1, [building in place])
else
	AC_MSG_NOTICE([building out of tree srcdir:$srcdir])
fi

# Checks for programs.

# check stolen from gnulib/m4/gnu-make.m4
if ! ${MAKE-make} --version /cannot/make/this >/dev/null 2>&1; then
	AC_MSG_ERROR([you don't seem to have GNU make; it is required])
fi

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_CHECK_PROGS([PKGCONFIG], [pkg-config])
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AM_CONDITIONAL(HAVE_DOXYGEN, test -n "${DOXYGEN}")
AC_CHECK_PROGS([SPLINT], [splint])
AM_CONDITIONAL(HAVE_SPLINT, test -n "${SPLINT}")

## local helper functions

# this function checks if CC support options passed as
# args. Global CFLAGS are ignored during this test.
cc_supports_flag() {
	local CFLAGS="$@"
	AC_MSG_CHECKING([whether $CC supports "$@"])
	AC_COMPILE_IFELSE([int main(){return 0;}] ,
			  [RC=0; AC_MSG_RESULT([yes])],
			  [RC=1; AC_MSG_RESULT([no])])
	return $RC
}

## cleanup
AC_MSG_NOTICE(Sanitizing prefix: ${prefix})
case $prefix in
  NONE) prefix=/usr/local;;
esac

AC_MSG_NOTICE(Sanitizing exec_prefix: ${exec_prefix})
case $exec_prefix in
  NONE)   exec_prefix=$prefix;;
  prefix) exec_prefix=$prefix;;
esac

# Checks for libraries.
AC_CHECK_LIB([rt], [mq_open])
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([socket], [socket])

# look for testing harness "check"
PKG_CHECK_MODULES([CHECK], [check >= 0.9.4],[with_check=yes],[with_check=no])
AM_CONDITIONAL(HAVE_CHECK, test "${with_check}" = "yes")

# look for GLIB (used for testing integration)
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.0, have_glib=yes, have_glib=no)
AM_CONDITIONAL(HAVE_GLIB, test x$have_glib = xyes)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
if test x"$have_glib" = xyes; then
AC_DEFINE_UNQUOTED([HAVE_GLIB], [1], [We have glib])
fi

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT

AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h netinet/in.h stdint.h \
		  time.h sys/time.h stdlib.h string.h strings.h sys/types.h sys/stat.h \
		  sys/param.h sys/socket.h sys/time.h \
		  sys/sockio.h sys/un.h syslog.h errno.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([alarm clock_gettime ftruncate gettimeofday memset munmap \
		socket strchr strerror strstr epoll_create epoll_create1 \
		pthread_spin_lock])

# Check for atomic support
gcc_has_builtin_atomic_operations=no
if test x"$GCC" = xyes; then
	AC_MSG_CHECKING([whether GCC supports build-in atomic intrinsics])
	AC_TRY_LINK([],
		    [int i;
		     __sync_synchronize ();
		     __sync_bool_compare_and_swap (&i, 0, 1);
		     __sync_fetch_and_add (&i, 1);
		     ],
		     [gcc_has_builtin_atomic_operations=yes],
		     [gcc_has_builtin_atomic_operations=no])

	AC_MSG_RESULT($gcc_has_builtin_atomic_operations)
	if test $gcc_has_builtin_atomic_operations = yes; then
		memory_barrier_needed=yes
	else
		case $host_cpu in
			sparc*)
			AC_MSG_RESULT([sparcv9])
			AC_DEFINE_UNQUOTED([QB_ARCH_SPARCV9], [1], [sparcv9])
			memory_barrier_needed=yes
			;;
			alpha*)
			AC_MSG_RESULT([alpha])
			AC_DEFINE_UNQUOTED([QB_ARCH_ALPHA], [1], [alpha])
			memory_barrier_needed=yes
			;;
			powerpc*)
			AC_MSG_RESULT([powerpc])
			AC_DEFINE_UNQUOTED([QB_ARCH_POWERPC], [1], [powerpc])
			memory_barrier_needed=yes
			;;
			ia64)
			AC_MSG_RESULT([ia64])
			AC_DEFINE_UNQUOTED([QB_ARCH_IA64], [1], [ia64])
			memory_barrier_needed=yes
			;;
			*)
			AC_MSG_RESULT([none])
			memory_barrier_needed=no
			;;
		esac
		AC_MSG_WARN([-----------------------------])
		AC_MSG_WARN([You have gcc but not __sync_bool_compare_and_swap])
		AC_MSG_WARN([try CFLAGS="-march=<your arch> -mtune=native" ./configure])
		AC_MSG_WARN([-----------------------------])
	fi
fi

if test x"$memory_barrier_needed" != xno; then
	AC_DEFINE_UNQUOTED(QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED, 1,
			   [need atomic memory barrier])
	fi

AM_CONDITIONAL(HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS,
	       [test $gcc_has_builtin_atomic_operations = yes])


## local defines
PACKAGE_FEATURES=""

LINT_FLAGS="-weak -unrecog +posixlib +ignoresigns -fcnuse \
	-badflag -D__gnuc_va_list=va_list -D__attribute\(x\)= \
	-warnposix +matchanyintegral"

# libraries SONAME
SOMAJOR="0"
SOMINOR="1"
SOMICRO="0"
SONAME="${SOMAJOR}.${SOMINOR}.${SOMICRO}"

# local options
AC_ARG_ENABLE([ansi],
	[  --enable-ansi           : force to build with ANSI standards. ],
	[ default="no" ])

AC_ARG_ENABLE([fatal-warnings],
	[  --enable-fatal-warnings : enable fatal warnings. ],
	[ default="no" ])

AC_ARG_ENABLE([debug],
	[  --enable-debug          : enable debug build. ],
	[ default="no" ])

AC_ARG_ENABLE([coverage],
	[  --enable-coverage       : coverage analysis of the codebase. ],
	[ default="no" ])

AC_ARG_WITH([socket-dir],
	[  --with-socket-dir=DIR   : socket dir. ],
	[ SOCKETDIR="$withval" ],
	[ SOCKETDIR="$localstatedir/run" ])

# OS detection
# THIS SECTION MUST DIE!
CP=cp
case "$host_os" in
	*linux*)
		AC_DEFINE_UNQUOTED([QB_LINUX], [1],
				   [Compiling for Linux platform])
		LINT_FLAGS+=" -expect 22"
	;;
	darwin*)
		AC_DEFINE_UNQUOTED([QB_DARWIN], [1],
				   [Compiling for Darwin platform])
		CP=rsync
		AC_DEFINE_UNQUOTED([MAP_ANONYMOUS], [MAP_ANON],
				   [Shared memory define for Darwin platform])
		AC_DEFINE_UNQUOTED([PATH_MAX], [4096],
				   [Number of chars in a path name including nul])
		AC_DEFINE_UNQUOTED([NAME_MAX], [255],
				   [Number of chars in a file name])
	;;
	*bsd*)
		AC_DEFINE_UNQUOTED([QB_BSD], [1],
				   [Compiling for BSD platform])
		AC_DEFINE_UNQUOTED([MAP_ANONYMOUS], [MAP_ANON],
				   [Shared memory define for Darwin platform])
		case "$host_os" in
			*freebsd[[234567]]*)
			;;
			*freebsd*)
				AC_DEFINE_UNQUOTED([QB_FREEBSD_GE_8], [1],
					   [Compiling for FreeBSD >= 8 platform])
			;;
		esac
		LINT_FLAGS="$LINT_FLAGS -expect 22"
	;;
	*solaris*)
		AC_DEFINE_UNQUOTED([QB_SOLARIS], [1],
				   [Compiling for Solaris platform])
		AC_DEFINE_UNQUOTED([TS_CLASS], [1],
				   [Prevent being scheduled RR])
		AC_DEFINE_UNQUOTED([_SEM_SEMUN_UNDEFINED], [1],
				   [The semun structure is undefined])
		CP=rsync
	;;
	*)
		AC_MSG_ERROR([Unsupported OS? hmmmm])
	;;
esac

AC_SUBST(CP)
# *FLAGS handling goes here

ENV_CFLAGS="$CFLAGS"
ENV_CPPFLAGS="$CPPFLAGS"
ENV_LDFLAGS="$LDFLAGS"

# debug build stuff
if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Compiling Debugging code])
	OPT_CFLAGS="-O0"
	PACKAGE_FEATURES="$PACKAGE_FEATURES debug"
else
	OPT_CFLAGS="-O3"
fi

# gdb flags
if test "x${GCC}" = xyes; then
	GDB_FLAGS="-ggdb3"
else
	GDB_FLAGS="-g"
fi

# extra warnings
EXTRA_WARNINGS=""

WARNLIST="
	all
	shadow
	missing-prototypes
	missing-declarations
	strict-prototypes
	declaration-after-statement
	pointer-arith
	write-strings
	cast-align
	bad-function-cast
	missing-format-attribute
	format=2
	format-security
	format-nonliteral
	no-long-long
	unsigned-char
	gnu89-inline
	no-strict-aliasing
	"

for j in $WARNLIST; do
	if cc_supports_flag -W$j; then
		EXTRA_WARNINGS="$EXTRA_WARNINGS -W$j";
	fi
done

if test "x${enable_coverage}" = xyes && \
		cc_supports_flag -ftest-coverage && \
		cc_supports_flag -fprofile-arcs ; then
	AC_MSG_NOTICE([Enabling Coverage (enable -O0 by default)])
	OPT_CFLAGS="-O0"
	COVERAGE_CFLAGS="-ftest-coverage -fprofile-arcs"
	COVERAGE_LDFLAGS="-ftest-coverage -fprofile-arcs"
	PACKAGE_FEATURES="$PACKAGE_FEATURES coverage"
else
	COVERAGE_CFLAGS=""
	COVERAGE_LDFLAGS=""
fi

if test "x${enable_ansi}" = xyes && \
		cc_supports_flag -std=iso9899:199409 ; then
	AC_MSG_NOTICE([Enabling ANSI Compatibility])
	ANSI_CPPFLAGS="-ansi -D_GNU_SOURCE -DANSI_ONLY"
	PACKAGE_FEATURES="$PACKAGE_FEATURES ansi"
else
	ANSI_CPPFLAGS=""
fi

if test "x${enable_fatal_warnings}" = xyes && \
		cc_supports_flag -Werror ; then
	AC_MSG_NOTICE([Enabling Fatal Warnings (-Werror)])
	WERROR_CFLAGS="-Werror"
	PACKAGE_FEATURES="$PACKAGE_FEATURES fatal-warnings"
else
	WERROR_CFLAGS=""
fi

# final build of *FLAGS
CFLAGS="$ENV_CFLAGS $OPT_CFLAGS $GDB_FLAGS \
	$COVERAGE_CFLAGS $EXTRA_WARNINGS $WERROR_CFLAGS"
CPPFLAGS="$ENV_CPPFLAGS $ANSI_CPPFLAGS"
LDFLAGS="$ENV_LDFLAGS $COVERAGE_LDFLAGS"

# substitute what we need:
AC_SUBST([SOCKETDIR])
AC_SUBST([SOMAJOR])
AC_SUBST([SOMINOR])
AC_SUBST([SOMICRO])
AC_SUBST([SONAME])

AC_SUBST([LINT_FLAGS])

AC_DEFINE_UNQUOTED([SOCKETDIR], "$(eval echo ${SOCKETDIR})", [Socket directory])
AC_DEFINE_UNQUOTED([LOCALSTATEDIR], "$(eval echo ${localstatedir})", [localstate directory])
AC_DEFINE_UNQUOTED([PACKAGE_FEATURES], "${PACKAGE_FEATURES}", [quarterback built-in features])

AC_CONFIG_FILES([Makefile
		 include/Makefile
		 include/qb/Makefile
		 lib/Makefile
		 lib/libqb.pc
		 tests/Makefile
		 docs/Makefile
		 docs/man.dox
		 docs/html.dox])

AC_OUTPUT

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Man pages                = ${mandir}])
AC_MSG_RESULT([  Doc dir                  = ${docdir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  Arch-independent files   = ${datadir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([  SOCKETDIR                = ${SOCKETDIR}])
AC_MSG_RESULT([  Features                 =${PACKAGE_FEATURES}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Library SONAME           = ${SONAME}])
AC_MSG_RESULT([  Default optimization     = ${OPT_CFLAGS}])
AC_MSG_RESULT([  Default debug options    = ${GDB_CFLAGS}])
AC_MSG_RESULT([  Extra compiler warnings  = ${EXTRA_WARNING}])
AC_MSG_RESULT([  Env. defined CFLAG       = ${ENV_CFLAGS}])
AC_MSG_RESULT([  Env. defined CPPFLAGS    = ${ENV_CPPFLAGS}])
AC_MSG_RESULT([  Env. defined LDFLAGS     = ${ENV_LDFLAGS}])
AC_MSG_RESULT([  ANSI defined CPPFLAGS    = ${ANSI_CPPFLAGS}])
AC_MSG_RESULT([  Coverage     CFLAGS      = ${COVERAGE_CFLAGS}])
AC_MSG_RESULT([  Coverage     LDFLAGS     = ${COVERAGE_LDFLAGS}])
AC_MSG_RESULT([  Fatal War.   CFLAGS      = ${WERROR_CFLAGS}])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
