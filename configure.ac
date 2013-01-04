
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])

AC_INIT([libqb],
	m4_esyscmd([build-aux/git-version-gen .tarball-version]),
	[quarterback-devel@fedorahosted.org])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([lib/ringbuffer.c])
AC_CONFIG_HEADERS([include/config.h include/qb/qbconfig.h])
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([-Wno-portability dist-xz])
dnl automake >= 1.11 offers --enable-silent-rules for suppressing the output from
dnl normal compilation.  When a failure occurs, it will then display the full
dnl command line
dnl Wrap in m4_ifdef to avoid breaking on older platforms
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
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

AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LN_S
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
	BACKUP="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $@"
	AC_MSG_CHECKING([whether $CC supports "$@"])
	AC_PREPROC_IFELSE([AC_LANG_PROGRAM([])],
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
dnl librt from glibc NEEDs libpthread
dnl so. if test for libpthread after librt
dnl it will always be "none needed", but it is not true
dnl when linking libraries. Looks like a bug.
AC_SEARCH_LIBS([pthread_create], [pthread])
AC_SEARCH_LIBS([mq_open], [rt])
AC_SEARCH_LIBS([dlopen], [dl])
AC_SEARCH_LIBS([socket], [socket])
AC_SEARCH_LIBS([gethostbyname], [nsl])

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


# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_MMAP
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([alarm clock_gettime ftruncate gettimeofday \
		localtime localtime_r memset munmap socket \
		strchr strrchr strdup strstr strcasecmp \
		poll epoll_create epoll_create1 kqueue \
		random rand getrlimit sysconf \
		pthread_spin_lock pthread_setschedparam \
                pthread_mutexattr_setpshared \
                pthread_condattr_setpshared \
		sem_timedwait semtimedop \
		sched_get_priority_max sched_setscheduler \
		getpeerucred getpeereid])

AM_CONDITIONAL(HAVE_SEM_TIMEDWAIT,
	       [test "x$ac_cv_func_sem_timedwait" = xyes])
AM_CONDITIONAL(HAVE_EPOLL,
	       [test "x$ac_cv_func_epoll_create" = xyes])
AM_CONDITIONAL(HAVE_POLL,
	       [test "x$ac_cv_func_poll" = xyes])
AM_CONDITIONAL(HAVE_KQUEUE,
	       [test "x$ac_cv_func_kqueue" = xyes])

AC_CONFIG_LIBOBJ_DIR(lib)
AC_REPLACE_FUNCS(strlcpy strlcat strchrnul)

## local defines
PACKAGE_FEATURES=""

if test x$ac_cv_func_epoll_create = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES epoll"
fi

nongcc_memory_barrier_needed=no
arch_force_shmlba=no
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
		ac_cv_link_attribute_section=no
		nongcc_memory_barrier_needed=yes
		arch_force_shmlba=yes
		;;
	ia64)
		AC_MSG_RESULT([ia64])
		AC_DEFINE_UNQUOTED([QB_ARCH_IA64], [1], [ia64])
		nongcc_memory_barrier_needed=yes
		;;
	arm*)
		AC_MSG_RESULT([ia64])
		AC_DEFINE_UNQUOTED([QB_ARCH_ARM], [1], [arm])
		arch_force_shmlba=yes
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
		ac_cv_link_attribute_section=no
		nongcc_memory_barrier_needed=yes
		gcc_has_builtin_atomic_operations=no
		AC_MSG_RESULT([Cygwin])
	;;
	darwin*)
		AC_DEFINE_UNQUOTED([QB_DARWIN], [1],
				   [Compiling for Darwin platform])
		CP=rsync
		ac_cv_link_attribute_section=no
		dnl Attribute section appears to work here but fails later with:
		dnl cc1: error in backend: Global variable 'descriptor.4902'
		dnl has an invalid section specifier '__verbose': mach-o
		dnl section specifier requires a segment and section
		dnl separated by a comma
		AC_DEFINE_UNQUOTED([DISABLE_POSIX_THREAD_PROCESS_SHARED], [1],
				   [Disable _POSIX_THREAD_PROCESS_SHARED])
		AC_MSG_RESULT([Dawin])
	;;
	*bsd*)
		AC_DEFINE_UNQUOTED([QB_BSD], [1],
				   [Compiling for BSD platform])
		case "$host_os" in
			*freebsd[[234567]]*)
			;;
			*freebsd*)
				AC_DEFINE_UNQUOTED([QB_FREEBSD_GE_8], [1],
					   [Compiling for FreeBSD >= 8 platform])
			;;
			*netbsd*)
				# this is because dlopen within a dl_iterate_phdr
				# callback locks up.
				ac_cv_link_attribute_section=no
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
		ac_cv_link_attribute_section=no
		AC_DEFINE_UNQUOTED(DISABLE_IPC_SHM, 1,
				   [Disable shared mem ipc])
		AC_DEFINE_UNQUOTED([QB_SOLARIS], [1],
				   [Compiling for Solaris platform])
		CP=rsync
		AC_MSG_RESULT([Solaris])
	;;
	*)
		AC_MSG_ERROR([Unsupported OS? hmmmm])
	;;
esac

AC_MSG_CHECKING([whether GCC supports build-in atomic intrinsics])
if test -z "$gcc_has_builtin_atomic_operations"; then
	gcc_has_builtin_atomic_operations=no
	if test x"$GCC" = xyes && test x$have_mingw != xyes; then
		AC_TRY_LINK([],
			    [int i;
			     __sync_synchronize ();
			     __sync_bool_compare_and_swap (&i, 0, 1);
			     __sync_fetch_and_add (&i, 1);
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
	if test x$gcc_has_builtin_atomic_operations = xyes; then
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

LINT_FLAGS="-syntax -weak -unrecog +posixlib +ignoresigns -fcnuse \
	-badflag -D__gnuc_va_list=va_list -D__attribute\(x\)= \
	-warnposix +matchanyintegral"

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

AC_ARG_ENABLE([slow-tests],
	[  --enable-slow-tests     : build and run slow tests. ],
	[ default="no" ])

AC_ARG_WITH([socket-dir],
	[  --with-socket-dir=DIR   : socket dir. ],
	[ SOCKETDIR="$withval" ],
	[ SOCKETDIR="$localstatedir/run" ])

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
	no-format-nonliteral
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

if test "x${enable_slow_tests}" = xyes ; then
	AC_DEFINE([HAVE_SLOW_TESTS], 1,[have slow tests])
	AC_MSG_NOTICE([Enabling Slow tests])
fi
AM_CONDITIONAL(HAVE_SLOW_TESTS, [test "x${enable_slow_tests}" = xyes])

# --- callsite sections ---
if test "x${GCC}" = xyes; then
	AC_MSG_CHECKING([whether GCC supports __attribute__((section())])
	if test "x${ac_cv_link_attribute_section}" = x ; then
		AC_TRY_LINK([],
			    [static int my_var __attribute__((section("__verbose"))) = 5;
			     if (my_var == 5) return 0;
			     else return -1;
			     ],
			     [gcc_has_attribute_section=yes],
			     [gcc_has_attribute_section=no])
	else
		gcc_has_attribute_section=${ac_cv_link_attribute_section}
	fi

	AC_MSG_RESULT($gcc_has_attribute_section)
	if test $gcc_has_attribute_section = yes; then
		AC_DEFINE([QB_HAVE_ATTRIBUTE_SECTION], 1,
			  [Enabling code using __attribute__((section))])
		PACKAGE_FEATURES="$PACKAGE_FEATURES attribute-section"
	fi
fi

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

# final build of *FLAGS
CFLAGS="$ENV_CFLAGS $OPT_CFLAGS $GDB_FLAGS \
	$COVERAGE_CFLAGS $EXTRA_WARNINGS $WERROR_CFLAGS"
CPPFLAGS="$ENV_CPPFLAGS $ANSI_CPPFLAGS"
LDFLAGS="$ENV_LDFLAGS $COVERAGE_LDFLAGS"

if test -f /usr/share/dict/words ; then
	HAVE_DICT_WORDS=yes
	AC_DEFINE([HAVE_DICT_WORDS], 1, "Have /usr/share/dict/words")
fi
AM_CONDITIONAL([HAVE_DICT_WORDS], [test "x$HAVE_DICT_WORDS" = xyes])

# substitute what we need:
AC_SUBST([SOCKETDIR])
AC_SUBST([LINT_FLAGS])

AC_DEFINE_UNQUOTED([SOCKETDIR], "$(eval echo ${SOCKETDIR})", [Socket directory])
AC_DEFINE_UNQUOTED([LOCALSTATEDIR], "$(eval echo ${localstatedir})", [localstate directory])
AC_DEFINE_UNQUOTED([PACKAGE_FEATURES], "${PACKAGE_FEATURES}", [quarterback built-in features])

AC_CONFIG_FILES([Makefile
		 include/Makefile
		 include/qb/Makefile
		 lib/Makefile
		 lib/libqb.pc
		 tools/Makefile
		 tests/Makefile
		 examples/Makefile
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
