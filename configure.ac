
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])

dnl inject zero as a "patch" component of the version if missing in tag;
dnl care to bump "X.Y.Z-yank" template below upon each release very desirable
AC_INIT([libqb],
	m4_esyscmd([build-aux/git-version-gen $(echo '$Format:%h??%D$'\
	            | sed -ne 's/^[$]Format:[^$]*[$]$/.tarball-version/p;tend'\
	                  -e 's/.*tag: v\([^, ][^, ]*\).*/\1-yank/;tv'\
	                  -e 's/^\([[:xdigit:]][[:xdigit:]]*\).*/1.9.0-yank\1/'\
	                  -e ':v' -e 'w .snapshot-version' -e 'i\
	                  .snapshot-version' -e ':end' -e 'q')\
	              's/^\(v[0-9][0-9]*\.[0-9][0-9]*\)\([^.].*\)\?$/\1.0\2/']),
	[developers@clusterlabs.org])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([lib/ringbuffer.c])
AC_CONFIG_HEADERS([include/config.h include/qb/qbconfig.h])
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([-Wno-portability dist-xz subdir-objects])
dnl automake >= 1.11 offers --enable-silent-rules for suppressing the output from
dnl normal compilation.  When a failure occurs, it will then display the full
dnl command line
dnl Wrap in m4_ifdef to avoid breaking on older platforms
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
LT_PREREQ([2.2.6])
# --enable-new-dtags: Use RUNPATH instead of RPATH.
# It is necessary to have this done before libtool does linker detection.
# See also: https://github.com/kronosnet/kronosnet/issues/107
AX_CHECK_LINK_FLAG([-Wl,--enable-new-dtags],
		   [AM_LDFLAGS=-Wl,--enable-new-dtags],
		   [AC_MSG_ERROR(["Linker support for --enable-new-dtags is required"])])
AC_SUBST([AM_LDFLAGS])
saved_LDFLAGS="$LDFLAGS"
LDFLAGS="$AM_LDFLAGS $LDFLAGS"
LT_INIT
LDFLAGS="$saved_LDFLAGS"

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

AC_PROG_CXX
AM_CONDITIONAL(HAVE_GXX, [test "x$GXX" = xyes]) 
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
AC_PROG_CC_C99
if test "x$ac_cv_prog_cc_c99" = "xno"; then
	AC_MSG_ERROR(["C99 support is required"])
fi
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_CHECK_PROGS([PKGCONFIG], [pkg-config])
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AM_CONDITIONAL(HAVE_DOXYGEN, test -n "${DOXYGEN}")
AC_CHECK_TOOLS([NM], [eu-nm nm], [:])
AC_CHECK_TOOLS([READELF], [eu-readelf readelf], [:])
AM_PATH_PYTHON([2.6],, [:])
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

## local helper functions

# this function checks if CC support options passed as
# args. Global CFLAGS are ignored during this test.
cc_supports_flag() {
	BACKUP="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $@ -Werror"
	AC_MSG_CHECKING([whether $CC supports "$@"])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
			  [RC=0; AC_MSG_RESULT([yes])],
			  [RC=1; AC_MSG_RESULT([no])])
	CPPFLAGS="$BACKUP"
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
AX_PTHREAD(,[AC_MSG_ERROR([POSIX threads support is required])])
AX_SAVE_FLAGS
AC_SEARCH_LIBS([dlopen],[dl],,[AC_MSG_ERROR([cannot find dlopen() function])])
AC_SUBST([dlopen_LIBS],[$LIBS])
AX_RESTORE_FLAGS
AX_SAVE_FLAGS
AC_SEARCH_LIBS([socket], [socket],,[AC_MSG_ERROR([cannot find socket() function])])
AC_SUBST([socket_LIBS],[$LIBS])
AX_RESTORE_FLAGS
AX_SAVE_FLAGS
AC_SEARCH_LIBS([gethostbyname], [nsl],,[AC_MSG_ERROR([cannot find gethostbyname() function])])
AC_SUBST([nsl_LIBS],[$LIBS])
AX_RESTORE_FLAGS
AX_SAVE_FLAGS
AC_SEARCH_LIBS([clock_gettime], [rt],,[AC_MSG_ERROR([cannot find clock_gettime() function])])
AC_SUBST([rt_LIBS],[$LIBS])
AX_RESTORE_FLAGS

USE_JOURNAL="no"
PKG_CHECK_MODULES([SYSTEMD], [libsystemd], [have_systemd=yes],[have_systemd=no])
if test "x$enable_systemd_journal" = "xyes" ; then
  if test "x$have_systemd" = "xyes" ; then
     AC_DEFINE_UNQUOTED(USE_JOURNAL, 1, [Use systemd journal logging])
     USE_JOURNAL="yes"
  else
     echo "systemd libraries not found, will just use syslog"
  fi
fi

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

# For building doxygen2man and man pages
PKG_CHECK_MODULES([libxml], [libxml-2.0])

# if we are not cross-compiling, we can use the locally built
# version of doxygen2man, otherwise we can look for
# a locally installed version. If neither are usable, then
# don´t build the man pages
if test "x$cross_compiling" = "xno"; then
  AM_CONDITIONAL([BUILD_MAN], [true])
  DOXYGEN2MAN="\$(abs_builddir)/../doxygen2man/doxygen2man"
else
  AC_CHECK_PROGS([DOXYGEN2MAN], [doxygen2man])
  if test "x$DOXYGEN2MAN" = "x"; then
    AM_CONDITIONAL([BUILD_MAN], [false])
  else
    AM_CONDITIONAL([BUILD_MAN], [true])
  fi
fi
AC_SUBST(DOXYGEN2MAN)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT

AC_CHECK_HEADERS([arpa/inet.h link.h fcntl.h inttypes.h limits.h netinet/in.h \
		  stdint.h stddef.h stdlib.h string.h strings.h \
		  dlfcn.h time.h sys/time.h sys/types.h sys/stat.h \
		  sys/param.h sys/socket.h sys/time.h sys/poll.h sys/epoll.h \
		  sys/uio.h sys/event.h sys/sockio.h sys/un.h sys/resource.h \
		  syslog.h errno.h unistd.h sys/mman.h \
		  sys/sem.h sys/ipc.h sys/msg.h netdb.h])

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
AC_TYPE_UINT64_T
AC_TYPE_UINT32_T
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

AC_CHECK_MEMBER([struct sockaddr_un.sun_len],
                [AC_DEFINE([HAVE_STRUCT_SOCKADDR_UN_SUN_LEN],
                           1,
                           [Define to 1 if struct sockaddr_un has a member sun_len])],
                [],
                [#include <sys/un.h>])

AC_MSG_CHECKING(looking for union semun in sys/sem.h)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[#include <sys/types.h>
  #include <sys/ipc.h>
  #include <sys/sem.h>]],
[[union semun arg; semctl(0, 0, 0, arg);]])],
                    [
                      AC_MSG_RESULT([yes])
                      AC_DEFINE_UNQUOTED([HAVE_SEMUN], 1, [Define to 1 if you have union semun.])
                    ],
                    [
                      AC_MSG_RESULT([no])
                    ]
                 )

AX_SAVE_FLAGS
LIBS="$LIBS $rt_LIBS"
AC_MSG_CHECKING(for a working clock_getres(CLOCK_MONOTONIC, &ts))
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[#include <time.h>]],
[[struct timespec ts; if(clock_getres(CLOCK_MONOTONIC, &ts)) return -1;]])],
                    [
                      AC_MSG_RESULT([yes])
                      AC_DEFINE_UNQUOTED([HAVE_CLOCK_GETRES_MONOTONIC], 1, [Define to 1 if clock_getres(CLOCK_MONOTONIC, &ts) works])
                    ],
                    [
                      AC_MSG_RESULT([no])
                    ]
                 )
AC_CHECK_FUNCS([clock_gettime])
AX_RESTORE_FLAGS


# Checks for library functions
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_MMAP
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([alarm fsync fdatasync ftruncate posix_fallocate \
		gettimeofday localtime localtime_r \
		memset munmap socket \
		strchr strrchr strdup strstr strcasecmp \
		poll epoll_create epoll_create1 kqueue \
		random rand getrlimit sysconf \
		getpeerucred getpeereid \
		openat unlinkat])

AX_SAVE_FLAGS
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
LIBS="$LIBS $PTHREAD_LIBS"
AC_CHECK_FUNCS([pthread_spin_lock pthread_setschedparam \
		pthread_mutexattr_setpshared \
		pthread_condattr_setpshared \
		sem_timedwait semtimedop])
AX_RESTORE_FLAGS

# Checks for defined macros
AC_MSG_CHECKING(for MSG_NOSIGNAL)
AC_TRY_COMPILE([#include <sys/socket.h>],
	       [ int f = MSG_NOSIGNAL; ],
	       [ AC_MSG_RESULT(yes)
	         AC_DEFINE(HAVE_MSG_NOSIGNAL, 1,
	         [Define this symbol if you have MSG_NOSIGNAL])],
	       [ AC_MSG_RESULT(no)])

AC_MSG_CHECKING(for SO_NOSIGPIPE )
AC_TRY_COMPILE([#include <sys/socket.h>],
		[ int f = SO_NOSIGPIPE; ],
		[ AC_MSG_RESULT(yes)
		  AC_DEFINE(HAVE_SO_NOSIGPIPE, 1,
		  [Define this symbol if you have SO_NOSIGPIPE]) ],
		[ AC_MSG_RESULT(no)])
AC_MSG_CHECKING([for RTLD_NEXT])
AC_TRY_COMPILE([#ifndef _GNU_SOURCE
		#define _GNU_SOURCE
		#endif
                #include <dlfcn.h>],
		[ void *h = RTLD_NEXT; ],
		[ AC_MSG_RESULT([yes])
		  AC_DEFINE([HAVE_FAILURE_INJECTION], 1,
			    [have failure injection])
		  have_RTLD_NEXT=yes ],
		[ AC_MSG_RESULT([no])])


AM_CONDITIONAL(HAVE_SEM_TIMEDWAIT,
	       [test "x$ac_cv_func_sem_timedwait" = xyes])
AM_CONDITIONAL(HAVE_EPOLL,
	       [test "x$ac_cv_func_epoll_create" = xyes])
AM_CONDITIONAL(HAVE_POLL,
	       [test "x$ac_cv_func_poll" = xyes])
AM_CONDITIONAL(HAVE_KQUEUE,
	       [test "x$ac_cv_func_kqueue" = xyes])
AM_CONDITIONAL(HAVE_FAILURE_INJECTION,
	       [test "x$have_RTLD_NEXT" = xyes])

AC_CONFIG_LIBOBJ_DIR(lib)
AC_REPLACE_FUNCS(strlcpy strlcat strchrnul)

## local defines
PACKAGE_FEATURES=""

if test x$ac_cv_func_epoll_create = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES epoll"
fi

nongcc_memory_barrier_needed=no
arch_force_shmlba=no
file_sync=fdatasync
AC_MSG_CHECKING([for architecture in ${host_cpu}])
case $host_cpu in
	sparc*)
		AC_MSG_RESULT([sparc])
		AC_DEFINE_UNQUOTED([QB_ARCH_SPARC], [1], [sparc])
		nongcc_memory_barrier_needed=yes
		arch_force_shmlba=yes
		;;
	alpha*)
		AC_MSG_RESULT([alpha])
		AC_DEFINE_UNQUOTED([QB_ARCH_ALPHA], [1], [alpha])
		nongcc_memory_barrier_needed=yes
		;;
	powerpc*)
		AC_MSG_RESULT([powerpc])
		AC_DEFINE_UNQUOTED([QB_ARCH_POWERPC], [1], [powerpc])
		nongcc_memory_barrier_needed=yes
		arch_force_shmlba=yes
		;;
	ia64)
		AC_MSG_RESULT([ia64])
		AC_DEFINE_UNQUOTED([QB_ARCH_IA64], [1], [ia64])
		nongcc_memory_barrier_needed=yes
		;;
	arm*)
		AC_MSG_RESULT([arm])
		AC_DEFINE_UNQUOTED([QB_ARCH_ARM], [1], [arm])
		arch_force_shmlba=yes
		;;
	hppa*)
		AC_MSG_RESULT([hppa])
		AC_DEFINE_UNQUOTED([QB_ARCH_HPPA], [1], [hppa])
		;;
	mips*)
		AC_MSG_RESULT([ia64])
		AC_DEFINE_UNQUOTED([QB_ARCH_MIPS], [1], [mips])
		arch_force_shmlba=yes
		;;
	*)
		AC_MSG_RESULT([${host_cpu}])
		;;
esac

if test $arch_force_shmlba = yes; then
	AC_DEFINE_UNQUOTED([QB_FORCE_SHM_ALIGN], [1],
			   [shared and fixed mmap must align on 16k])
fi

# OS detection
# THIS SECTION MUST DIE!
CP=cp
AC_MSG_CHECKING([for os in ${host_os}])
case "$host_os" in
	*linux*)
		AC_DEFINE_UNQUOTED([QB_LINUX], [1],
				   [Compiling for Linux platform])
		AC_MSG_RESULT([Linux])
	;;
	*cygwin*)
		AC_DEFINE_UNQUOTED([QB_CYGWIN], [1],
				   [Compiling for Cygwin platform])
		nongcc_memory_barrier_needed=yes
		gcc_has_builtin_sync_operations=no
		AC_MSG_RESULT([Cygwin])
	;;
	darwin*)
		AC_DEFINE_UNQUOTED([QB_DARWIN], [1],
				   [Compiling for Darwin platform])
		CP=rsync
		dnl Attribute section appears to work here but fails later with:
		dnl cc1: error in backend: Global variable 'descriptor.4902'
		dnl has an invalid section specifier '__verbose': mach-o
		dnl section specifier requires a segment and section
		dnl separated by a comma
		AC_DEFINE_UNQUOTED([DISABLE_POSIX_THREAD_PROCESS_SHARED], [1],
				   [Disable _POSIX_THREAD_PROCESS_SHARED])
		AC_MSG_RESULT([Darwin])
	;;
	*bsd*)
		AC_DEFINE_UNQUOTED([QB_BSD], [1],
				   [Compiling for BSD platform])
		case "$host_os" in
			*netbsd*)
				AC_DEFINE_UNQUOTED([UNIX_PATH_MAX], [103],
					   [Unix path length])
			;;
			*openbsd*)
				AC_DEFINE_UNQUOTED([UNIX_PATH_MAX], [104],
					   [Unix path length])
			;;
		esac
		AC_MSG_RESULT([BSD])
	;;
	*solaris*)
		AC_DEFINE_UNQUOTED(DISABLE_IPC_SHM, 1,
				   [Disable shared mem ipc])
		AC_DEFINE_UNQUOTED([QB_SOLARIS], [1],
				   [Compiling for Solaris platform])
		CP=rsync
		AC_MSG_RESULT([Solaris])
	;;
	*gnu*)
		AC_DEFINE_UNQUOTED([QB_GNU], [1],
				   [Compiling for GNU/Hurd platform])
		AC_MSG_RESULT([GNU])
	;;
	*)
		AC_MSG_ERROR([Unsupported OS? hmmmm])
	;;
esac

dnl break on first hit, fallthrough otherwise, until empty or unsupported
dnl string reached; output QB_FILE_SYNC macro operates on a file descriptor
while : ; do
	case ${file_sync} in
	fdatasync)
		test "x${ac_cv_func_fdatasync}" = "xno" || break
		file_sync=fsync;;
	fsync)
		test "x${ac_cv_func_fsync}" = "xno" || break
		file_sync=;;
	"")
		break;;
	*)
		AC_MSG_ERROR([Cannot select file sync method]);;
	esac
done
if test "x${file_sync}" != x; then
	AC_DEFINE_UNQUOTED([QB_FILE_SYNC(fd)], [${file_sync}(fd)], [File sync method])
else
	AC_MSG_WARN([No file sync method applicable!])
	AC_DEFINE([QB_FILE_SYNC(x)], [], [File sync method])
fi

AC_MSG_CHECKING([whether GCC supports builtin sync intrinsics])
if test -z "$gcc_has_builtin_sync_operations"; then
	gcc_has_builtin_sync_operations=no
	if test x"$GCC" = xyes && test x$have_mingw != xyes; then
		AC_TRY_LINK([],
			    [int i;
			     __sync_synchronize ();
			     __sync_bool_compare_and_swap (&i, 0, 1);
			     __sync_fetch_and_add (&i, 1);
			     ],
			     [gcc_has_builtin_sync_operations=yes],
			     [gcc_has_builtin_sync_operations=no])
	fi
fi
AC_MSG_RESULT($gcc_has_builtin_sync_operations)
AM_CONDITIONAL(HAVE_GCC_BUILTINS_FOR_SYNC_OPERATIONS,
	       [test "x$gcc_has_builtin_sync_operations" = xyes])

if test "x$gcc_has_builtin_sync_operations" = xyes; then
	AC_DEFINE_UNQUOTED(HAVE_GCC_BUILTINS_FOR_SYNC_OPERATIONS, 1,
			   [have builtin sync operations])
fi

# __atomic_XXX
AC_MSG_CHECKING([whether GCC supports builtin atomic intrinsics])
if test -z "$gcc_has_builtin_atomic_operations"; then
	gcc_has_builtin_atomic_operations=no
	if test x"$GCC" = xyes && test x$have_mingw != xyes; then
		AC_TRY_LINK([],
			    [int i;
			     __atomic_load_n(&i, __ATOMIC_ACQUIRE);
			     __atomic_exchange_n(&i, 0, __ATOMIC_RELEASE);
			     ],
			     [gcc_has_builtin_atomic_operations=yes],
			     [gcc_has_builtin_atomic_operations=no])
	fi
fi
AC_MSG_RESULT($gcc_has_builtin_atomic_operations)
AM_CONDITIONAL(HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS,
	       [test "x$gcc_has_builtin_atomic_operations" = xyes])

if test "x$gcc_has_builtin_atomic_operations" = xyes; then
	AC_DEFINE_UNQUOTED(HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS, 1,
			   [have builtin atomic operations])
fi


AC_MSG_CHECKING([whether atomics need memory barrier])
if test -n "$ac_cv_atomic_need_memory_barrier"; then
	memory_barrier_needed=$ac_cv_atomic_need_memory_barrier
else
	if test x$gcc_has_builtin_sync_operations = xyes; then
		memory_barrier_needed=yes
		PACKAGE_FEATURES="$PACKAGE_FEATURES gcc__sync"
	else
		memory_barrier_needed=$nongcc_memory_barrier_needed
		AC_MSG_WARN([-----------------------------])
		AC_MSG_WARN([You have gcc but not __sync_bool_compare_and_swap])
		AC_MSG_WARN([try CFLAGS="-march=<your arch> -mtune=native" ./configure])
		AC_MSG_WARN([-----------------------------])
	fi
fi
AC_MSG_RESULT($memory_barrier_needed)

if test x"$memory_barrier_needed" != xno; then
	AC_DEFINE_UNQUOTED(QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED, 1,
			   [need atomic memory barrier])
fi

LINT_FLAGS="-weak -D__gnuc_va_list=va_list -D__attribute\(x\)= \
	-badflag -fcnuse -syntax -unrecog -sysunrecog -warnposix \
	+ignoresigns +matchanyintegral +posixlib \
	+showscan +showsummary"

# local options
AC_ARG_ENABLE([ansi],
  [AS_HELP_STRING([--enable-ansi],[force to build with ANSI standards])])

AC_ARG_ENABLE([fatal-warnings],
  [AS_HELP_STRING([--enable-fatal-warnings],[enable fatal warnings])])

AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],[enable debug build])])

AC_ARG_WITH([sanitizers],
  [AS_HELP_STRING([--with-sanitizers=...,...],
    [enable SANitizer build, do *NOT* use for production. Only ASAN/UBSAN/TSAN are currently supported])],
  [ SANITIZERS="$withval" ],
  [ SANITIZERS="" ])

AC_ARG_ENABLE([coverage],
  [AS_HELP_STRING([--enable-coverage],[coverage analysis of the codebase])])

AC_ARG_ENABLE([interlib-deps],
  [AS_HELP_STRING([--disable-interlib-deps],
                  [disable inter-library dependencies (might break builds)])])

AC_ARG_ENABLE([slow-tests],
  [AS_HELP_STRING([--enable-slow-tests],[build and run slow tests])])

AC_ARG_WITH([socket-dir],
  [AS_HELP_STRING([--with-socket-dir=DIR],[socket directory @<:@LOCALSTATEDIR/run@:>@])],
	[ SOCKETDIR="$withval" ],
	[ SOCKETDIR="$localstatedir/run" ])

AC_ARG_ENABLE([systemd-journal],
  [AS_HELP_STRING([--enable-systemd-journal],[Allow use of systemd journal instead of syslog])])

AC_ARG_WITH([force-sockets-config-file],
  [AS_HELP_STRING([--with-force-sockets-config-file=FILE],[config file to force IPC to use filesystem sockets (Linux & Cygwin only) @<:@SYSCONFDIR/libqb/force-filesystem-sockets@:>@])],
	[ FORCESOCKETSFILE="$withval" ],
	[ FORCESOCKETSFILE="$sysconfdir/libqb/force-filesystem-sockets" ])

AC_ARG_ENABLE([install-tests],
  [AS_HELP_STRING([--enable-install-tests],[install tests])],,
  [ enable_install_tests="no" ])
AM_CONDITIONAL([INSTALL_TESTS], [test x$enable_install_tests = xyes])

AC_ARG_WITH([testdir],
  [AS_HELP_STRING([--with-testdir=DIR],[path to /usr/lib../libqb/tests/ dir where to install the test suite])],
        [ TESTDIR="$withval" ],
        [ TESTDIR="$libdir/libqb/tests" ])
AC_SUBST([TESTDIR])

AC_SUBST(CP)
# *FLAGS handling goes here

ENV_CFLAGS="$CFLAGS"
ENV_CPPFLAGS="$CPPFLAGS"
ENV_LDFLAGS="$LDFLAGS"

# debug build stuff
if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Compiling Debugging code])
	OPT_CFLAGS="-O0"
	if test "x${GCC}" = xyes; then
		GDB_FLAGS="-ggdb3"
	else
		GDB_FLAGS="-g"
	fi
	PACKAGE_FEATURES="$PACKAGE_FEATURES debug"
fi

# extra warnings
EXTRA_WARNINGS=""

WARNLIST="
	all
	extra
	unused
	shadow
	missing-prototypes
	missing-declarations
	suggest-attribute=noreturn
	suggest-attribute=format
	property-attribute-mismatch
	strict-prototypes
	pointer-arith
	write-strings
	cast-align
	bad-function-cast
	missing-format-attribute
	float-equal
	format=2
	format-signedness
	shift-overflow
	shift-overflow=2
	overlength-strings
	redundent-decls
	init-self
	uninitialized
	unknown-pragmas
	no-unused-parameter
	unused-const-variable
	no-format-nonliteral
	no-format-truncation
	no-sign-compare
	"
# at least no-sign-compare assumed temporary!

for j in $WARNLIST; do
	if cc_supports_flag -W$j; then
		EXTRA_WARNINGS="$EXTRA_WARNINGS -W$j";
	fi
done

# warnings suppression
gcc_format_complaints=no
if test x"$GCC" = xyes && cc_supports_flag -Wmissing-format-attribute; then
	gcc_format_complaints=yes
	AC_DEFINE([HAVE_GCC_MISSING_FORMAT_ATTRIBUTE], [],
		  [gcc supports -Wmissing-format-attribute])
fi
if test x"$GCC" = xyes && cc_supports_flag -Wsuggest-attribute=format; then
	gcc_format_complaints=yes
	AC_DEFINE([HAVE_GCC_SUGGEST_ATTRIBUTE_FORMAT], [],
		  [gcc supports -Wsuggest-attribute=format])
fi
dnl pretend GCC (<4.6) is not capable of format complaints when it does not
dnl support diagnostic push/pop pragmas (cannot track state reliably, then)
if test x"$gcc_format_complaints" = xyes; then
	backup_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Werror"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#pragma GCC diagnostic push
#pragma GCC diagnostic pop
	]])], AC_DEFINE([HAVE_GCC_FORMAT_COMPLAINTS], [],
			[gcc can complain about missing format attribute]))
	CFLAGS="$backup_CFLAGS"
fi

# --- coverage ---
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

# --- inter-library dependencies ---
# because of debian/ubuntu swimming against the stream
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=702737,
# override the libtool variable by force because the current
# arrangement relies on transitive dependency propagation
AC_MSG_NOTICE([Enabling inter-library dependencies: $enable_interlib_deps])
if test "x${enable_interlib_deps}" = xno; then
    link_all_deplibs=no
else
    link_all_deplibs=yes
fi

# --- slow tests ---
if test "x${enable_slow_tests}" = xyes ; then
	AC_DEFINE([HAVE_SLOW_TESTS], 1,[have slow tests])
	AC_MSG_NOTICE([Enabling Slow tests])
fi
AM_CONDITIONAL(HAVE_SLOW_TESTS, [test "x${enable_slow_tests}" = xyes])
AC_SUBST(HAVE_SLOW_TESTS)

# --- ansi ---
if test "x${enable_ansi}" = xyes && \
		cc_supports_flag -std=iso9899:199409 ; then
	AC_MSG_NOTICE([Enabling ANSI Compatibility])
	ANSI_CPPFLAGS="-ansi -D_GNU_SOURCE -DANSI_ONLY"
	PACKAGE_FEATURES="$PACKAGE_FEATURES ansi"
else
	ANSI_CPPFLAGS=""
fi

# --- fatal warnings ---
if test "x${enable_fatal_warnings}" = xyes && \
		cc_supports_flag -Werror ; then
	AC_MSG_NOTICE([Enabling Fatal Warnings (-Werror)])
	WERROR_CFLAGS="-Werror"
	PACKAGE_FEATURES="$PACKAGE_FEATURES fatal-warnings"
else
	WERROR_CFLAGS=""
fi

# --- ASAN/UBSAN/TSAN (see man gcc) ---
# when using SANitizers, we need to pass the -fsanitize..
# to both CFLAGS and LDFLAGS. The CFLAGS/LDFLAGS must be
# specified as first in the list or there will be runtime
# issues (for example user has to LD_PRELOAD asan for it to work
# properly).

if test -n "${SANITIZERS}"; then
  SANITIZERS=$(echo $SANITIZERS | sed -e 's/,/ /g')
  for SANITIZER in $SANITIZERS; do
    case $SANITIZER in
      asan|ASAN)
	SANITIZERS_CFLAGS="$SANITIZERS_CFLAGS -fsanitize=address"
	SANITIZERS_LDFLAGS="$SANITIZERS_LDFLAGS -fsanitize=address -lasan"
	AC_CHECK_LIB([asan],[main],,AC_MSG_ERROR([Unable to find libasan]))
	;;
      ubsan|UBSAN)
	SANITIZERS_CFLAGS="$SANITIZERS_CFLAGS -fsanitize=undefined"
	SANITIZERS_LDFLAGS="$SANITIZERS_LDFLAGS -fsanitize=undefined -lubsan"
	AC_CHECK_LIB([ubsan],[main],,AC_MSG_ERROR([Unable to find libubsan]))
	;;
      tsan|TSAN)
	SANITIZERS_CFLAGS="$SANITIZERS_CFLAGS -fsanitize=thread"
	SANITIZERS_LDFLAGS="$SANITIZERS_LDFLAGS -fsanitize=thread -ltsan"
	AC_CHECK_LIB([tsan],[main],,AC_MSG_ERROR([Unable to find libtsan]))
	;;
    esac
  done
fi

# final build of *FLAGS
CFLAGS="$SANITIZERS_CFLAGS $ENV_CFLAGS $OPT_CFLAGS $GDB_FLAGS \
	$COVERAGE_CFLAGS $EXTRA_WARNINGS $WERROR_CFLAGS"
CPPFLAGS="$ENV_CPPFLAGS $ANSI_CPPFLAGS"
LDFLAGS="$SANITIZERS_LDFLAGS $ENV_LDFLAGS $COVERAGE_LDFLAGS"

if test -f /usr/share/dict/words ; then
	HAVE_DICT_WORDS=yes
	AC_DEFINE([HAVE_DICT_WORDS], 1, "Have /usr/share/dict/words")
fi
AM_CONDITIONAL([HAVE_DICT_WORDS], [test "x$HAVE_DICT_WORDS" = xyes])

# substitute what we need:
AC_SUBST([SOCKETDIR])
AC_SUBST([LINT_FLAGS])
AC_SUBST([FORCESOCKETSFILE])

AC_DEFINE_UNQUOTED([SOCKETDIR], "$(eval echo ${SOCKETDIR})", [Socket directory])
AC_DEFINE_UNQUOTED([LOCALSTATEDIR], "$(eval echo ${localstatedir})", [localstate directory])
AC_DEFINE_UNQUOTED([PACKAGE_FEATURES], "${PACKAGE_FEATURES}", [quarterback built-in features])
AC_DEFINE_UNQUOTED([FORCESOCKETSFILE], "$(eval echo ${FORCESOCKETSFILE})", [for sockets config file])

# version parsing (for qbconfig.h)
AC_DEFINE_UNQUOTED([QB_VER_MAJOR],
                   [$(echo "${VERSION}" \
		      | sed -e 's|^\([[0-9]][[0-9]]*\).*|\1|' \
		            -e t -e 's|.*|1|')],
		   [libqb major version])
AC_DEFINE_UNQUOTED([QB_VER_MINOR],
                   [$(echo "${VERSION}" \
		      | sed -e 's|^[[0-9]][[0-9]]*\.\([[0-9]][[0-9]]*\).*|\1|' \
		            -e t -e 's|.*|0|')],
		   [libqb minor version])
AC_DEFINE_UNQUOTED([QB_VER_MICRO],
	           [$(echo "${VERSION}" \
		      | sed -e 's|^[[0-9]][[0-9]]*\.[[0-9]][[0-9]]*\.\([[0-9]][[0-9]]*\).*|\1|' \
		            -e t -e 's|.*|0|')],
		   [libqb patch version])
AC_DEFINE_UNQUOTED([QB_VER_REST],
	           [$(echo "${VERSION}" \
		      | sed -e 's|^[[0-9]][[0-9]]*\(\.[[0-9]][[0-9]]*\)\{0,2\}\(.*\)|"\2"|' \
		            -e t -e 's|.*|""|')],
		   [libqb patch version])

AX_PROG_DATE
AS_IF([test "$ax_cv_prog_date_gnu_date:$ax_cv_prog_date_gnu_utc" = yes:yes],
	[UTC_DATE_AT="date -u -d@"],
	[AS_IF([test "x$ax_cv_prog_date_bsd_date" = xyes],
		[UTC_DATE_AT="date -u -r"],
		[AC_MSG_ERROR([date utility unable to convert epoch to UTC])])])
AC_SUBST([UTC_DATE_AT])

AC_ARG_VAR([SOURCE_EPOCH],[last modification date of the source])
AC_MSG_NOTICE([trying to determine source epoch])
AC_MSG_CHECKING([for source epoch in \$SOURCE_EPOCH])
AS_IF([test -n "$SOURCE_EPOCH"],
	[AC_MSG_RESULT([yes])],
	[AC_MSG_RESULT([no])
	 AC_MSG_CHECKING([for source epoch in source_epoch file])
	 AS_IF([test -e "$srcdir/source_epoch"],
		[read SOURCE_EPOCH <"$srcdir/source_epoch"
		 AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([no])
		 AC_MSG_CHECKING([for source epoch baked in by gitattributes export-subst])
		 SOURCE_EPOCH='$Format:%ct$' # template for rewriting by git-archive
		 AS_CASE([$SOURCE_EPOCH],
			[?Format:*], # was not rewritten
				[AC_MSG_RESULT([no])
				 AC_MSG_CHECKING([for source epoch in \$SOURCE_DATE_EPOCH])
				 AS_IF([test "x$SOURCE_DATE_EPOCH" != x],
					[SOURCE_EPOCH="$SOURCE_DATE_EPOCH"
					 AC_MSG_RESULT([yes])],
					[AC_MSG_RESULT([no])
					 AC_MSG_CHECKING([whether git log can provide a source epoch])
					 SOURCE_EPOCH=f${SOURCE_EPOCH#\$F} # convert into git log --pretty format
					 SOURCE_EPOCH=$(cd "$srcdir" && git log -1 --pretty=${SOURCE_EPOCH%$} 2>/dev/null)
					 AS_IF([test -n "$SOURCE_EPOCH"],
						[AC_MSG_RESULT([yes])],
						[AC_MSG_RESULT([no, using current time and breaking reproducibility])
						 SOURCE_EPOCH=$(date +%s)])])],
			[AC_MSG_RESULT([yes])]
		 )])
	])
AC_MSG_NOTICE([using source epoch $($UTC_DATE_AT$SOURCE_EPOCH +'%F %T %Z')])

AC_CONFIG_FILES([Makefile
		 include/Makefile
		 include/qb/Makefile
		 lib/Makefile
		 lib/libqb.pc
		 tools/Makefile
		 tests/Makefile
		 tests/test.conf
		 examples/Makefile
		 doxygen2man/Makefile
		 docs/Makefile
		 docs/man.dox
		 lib/qblog_script.la:lib/qblog_script.la.in])

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
AC_MSG_RESULT([  Features                 = ${PACKAGE_FEATURES}])
AC_MSG_RESULT([  Use systemd journal      = ${USE_JOURNAL}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Optimization             = ${OPT_CFLAGS}])
AC_MSG_RESULT([  Debug options            = ${GDB_CFLAGS}])
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
