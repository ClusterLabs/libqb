/*
 * Copyright 2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *         Jan Pokorny <jpokorny@redhat.com>
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QB_LOG_H_DEFINED
#define QB_LOG_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <qb/qbutil.h>
#include <qb/qbconfig.h>

#if defined(QB_KILL_ATTRIBUTE_SECTION) || defined(S_SPLINT_S)
#undef QB_HAVE_ATTRIBUTE_SECTION
#endif  /* defined(QB_KILL_ATTRIBUTE_SECTION) || defined(S_SPLINT_S) */

#ifdef QB_HAVE_ATTRIBUTE_SECTION
#include <assert.h>  /* possibly needed for QB_LOG_INIT_DATA */
#include <dlfcn.h>  /* dynamic linking: dlopen, dlsym, dladdr, ... */
#endif

/**
 * @file qblog.h
 * The logging API provides four main parts (basics, filtering, threading & blackbox).
 *
 * The idea behind this logging system is not to be prescriptive but to provide a
 * set of tools to help the developer achieve what they want quickly and easily.
 *
 * @par Basic logging API.
 * Call qb_log() to generate a log message. Then to write the message
 * somewhere meaningful call qb_log_ctl() to configure the targets.
 *
 * Simplest possible use:
 * @code
 * main() {
 *	qb_log_init("simple-log", LOG_DAEMON, LOG_INFO);
 * 	// ...
 *	qb_log(LOG_WARNING, "watch out");
 * 	// ...
 *	qb_log_fini();
 * }
 * @endcode
 *
 * @note
 * In practice, such a minimalistic approach hardly caters real use cases.
 * Following section discusses the customization.  Moreover when employing
 * the log module is bound to its active use (some log messages are assuredly
 * emitted within the target compilation unit), it's quite vital to instrument
 * the target side with @c QB_LOG_INIT_DATA() macro placed in the top file
 * scope in exactly one source file (preferably the main one) to be mixed into
 * the resulting compilation unit.  This is a self-defensive measure for when
 * the linker-assisted collection of callsite data silently fails, which could
 * otherwise go unnoticed, causing troubles down the road.
 *
 * @par Configuring log targets.
 * A log target can be syslog, stderr, the blackbox, stdout, or a text file.
 * By default only syslog is enabled.
 *
 * To enable a target do the following:
 * @code
 *	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
 * @endcode
 *
 * syslog, stderr, the blackbox, and stdout are static (they don't need
 * to be created, just enabled or disabled). However you can open multiple
 * logfiles (QB_LOG_TARGET_MAX - QB_LOG_TARGET_STATIC_MAX).
 * To do this, use the following code:
 * @code
 *	mytarget = qb_log_file_open("/var/log/mylogfile");
 *	qb_log_ctl(mytarget, QB_LOG_CONF_ENABLED, QB_TRUE);
 * @endcode
 *
 * Once your targets are enabled/opened you can configure them as follows:
 * Configure the size of blackbox
 * @code
 *	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 1024*10);
 * @endcode
 *
 * Make logging to file threaded:
 * @code
 *	qb_log_ctl(mytarget, QB_LOG_CONF_THREADED, QB_TRUE);
 * @endcode
 *
 * To workaround your syslog daemon filtering all messages > LOG_INFO
 * @code
 *	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP,
 *		   LOG_INFO - LOG_DEBUG);
 * @endcode
 *
 * To ensure all logs to file targets are fsync'ed (default QB_FALSE)
 * @code
 *	qb_log_ctl(mytarget, QB_LOG_CONF_FILE_SYNC, QB_TRUE);
 * @endcode
 *
 *
 * @par Filtering messages.
 * To have more power over what log messages go to which target you can apply
 * filters to the targets. What happens is the desired callsites have the
 * correct bit set. Then when the log message is generated it gets sent to the
 * targets based on which bit is set in the callsite's "target" bitmap.
 * Messages can be filtered based on the:
 * -# filename + priority
 * -# function name + priority
 * -# format string + priority
 *
 * So to make all logs from evil_function() go to stderr, do the following:
 * @code
 *	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FUNCTION, "evil_function", LOG_TRACE);
 * @endcode
 *
 * So to make all logs from totem* (with  a priority <= LOG_INFO) go to stderr,
 * do the following:
 * @code
 *	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FILE, "totem", LOG_INFO);
 * @endcode
 *
 * So to make all logs with the substring "ringbuffer" go to stderr,
 * do the following:
 * @code
 *	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FORMAT, "ringbuffer", LOG_TRACE);
 * @endcode
 *
 * @par Thread safe non-blocking logging.
 * Logging is only thread safe when threaded logging is in use. If you plan
 * on logging from multiple threads, you must initialize libqb's logger thread
 * and use qb_log_filter_ctl to set the QB_LOG_CONF_THREADED flag on all the
 * logging targets in use.
 *
 * To achieve non-blocking logging, so that any calls to write() or syslog()
 * will not hold up your program, you can use threaded logging as well.
 *
 * Threaded logging use:
 * @code
 * main() {
 *	qb_log_init("simple-log", LOG_DAEMON, LOG_INFO);
 *	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_THREADED, QB_TRUE);
 * 	// ...
 * 	daemonize();
 * 	// call this after you fork()
 * 	qb_log_thread_start();
 * 	// ...
 *	qb_log(LOG_WARNING, "watch out");
 * 	// ...
 *	qb_log_fini();
 * }
 * @endcode
 *
 * @par A blackbox for in-field diagnosis.
 * This stores log messages in a ringbuffer so they can be written to
 * file if the program crashes (you will need to catch SIGSEGV). These
 * can then be easily printed out later.
 *
 * @note the blackbox is not enabled by default.
 *
 * Blackbox usage:
 * @code
 *
 * static void sigsegv_handler(int sig)
 * {
 * 	(void)signal (SIGSEGV, SIG_DFL);
 * 	qb_log_blackbox_write_to_file("simple-log.fdata");
 * 	qb_log_fini();
 * 	raise(SIGSEGV);
 * }
 *
 * main() {
 *
 *	signal(SIGSEGV, sigsegv_handler);
 *
 *	qb_log_init("simple-log", LOG_DAEMON, LOG_INFO);
 *	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
 *	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 1024*10);
 *	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
 * 	// ...
 *	qb_log(LOG_WARNING, "watch out");
 * 	// ...
 *	qb_log_fini();
 * }
 * @endcode
 *
 * @par Tagging messages.
 * You can tag messages using the second argument to qb_logt() or
 * by using qb_log_filter_ctl().
 * This can be used to add feature or sub-system information to the logs.
 *
 * @code
 * const char* my_tags_stringify(uint32_t tags) {
 * 	if (qb_bit_is_set(tags, QB_LOG_TAG_LIBQB_MSG_BIT) {
 * 		return "libqb";
 * 	} else if (tags == 3) {
 * 		return "three";
 * 	} else {
 * 		return "MAIN";
 * 	}
 * }
 * main() {
 * 	// ...
 * 	qb_log_tags_stringify_fn_set(my_tags_stringify);
 * 	qb_log_format_set(QB_LOG_STDERR, "[%5g] %p %b");
 * 	// ...
 * 	qb_logt(LOG_INFO, 3, "hello");
 * 	qb_logt(LOG_INFO, 0, "hello");
 * }
 * @endcode
 * The code above will produce:
 * @code
 * [libqb] some message
 * [three] info hello
 * [MAIN ] info hello
 * @endcode
 *
 * @example simplelog.c
 */

#undef LOG_TRACE
#define LOG_TRACE    (LOG_DEBUG + 1)

#define QB_LOG_MAX_LEN 512
#define QB_LOG_STRERROR_MAX_LEN 128

typedef const char *(*qb_log_tags_stringify_fn)(uint32_t tags);

/**
 * An instance of this structure is created in a special
 * ELF section at every dynamic debug callsite.  At runtime,
 * the special section is treated as an array of these.
 */
struct qb_log_callsite {
	const char *function;
	const char *filename;
	const char *format;
	uint8_t priority;
	uint32_t lineno;
	uint32_t targets;
	uint32_t tags;
} __attribute__((aligned(8)));

typedef void (*qb_log_filter_fn)(struct qb_log_callsite * cs);

/* will be assigned by linker magic (assuming linker supports that):
 * https://sourceware.org/binutils/docs/ld/Orphan-Sections.html
 */
#ifdef QB_HAVE_ATTRIBUTE_SECTION

#define QB_ATTR_SECTION			__verbose  /* conforms to C ident. */
#define QB_ATTR_SECTION_STR		QB_PP_STRINGIFY(QB_ATTR_SECTION)
#define QB_ATTR_SECTION_START		QB_PP_JOIN(__start_, QB_ATTR_SECTION)
#define QB_ATTR_SECTION_STOP		QB_PP_JOIN(__stop_, QB_ATTR_SECTION)
#define QB_ATTR_SECTION_START_STR	QB_PP_STRINGIFY(QB_ATTR_SECTION_START)
#define QB_ATTR_SECTION_STOP_STR	QB_PP_STRINGIFY(QB_ATTR_SECTION_STOP)

extern struct qb_log_callsite QB_ATTR_SECTION_START[];
extern struct qb_log_callsite QB_ATTR_SECTION_STOP[];

/* Related to the next macro that is -- unlike this one -- a public API */
#ifndef _GNU_SOURCE
#define QB_NONAPI_LOG_INIT_DATA_EXTRA_					\
	_Pragma(QB_PP_STRINGIFY(GCC warning QB_PP_STRINGIFY(		\
	        without "_GNU_SOURCE" defined (directly or not) 	\
		QB_LOG_INIT_DATA cannot check sanity of libqb proper)))
#else
#define QB_NONAPI_LOG_INIT_DATA_EXTRA_					\
    { Dl_info work_dli;							\
    /* libqb sanity (locating libqb by it's relatively unique		\
       -- and currently only such per-linkage global one --		\
       non-functional symbol, due to possible confusion otherwise) */	\
    if (dladdr(dlsym(RTLD_DEFAULT, "facilitynames"), &work_dli)		\
        && (work_handle = dlopen(work_dli.dli_fname,			\
                                 RTLD_LOCAL|RTLD_LAZY)) != NULL) {	\
        work_s1 = (struct qb_log_callsite *)				\
                  dlsym(work_handle, QB_ATTR_SECTION_START_STR);	\
        work_s2 = (struct qb_log_callsite *)				\
                  dlsym(work_handle, QB_ATTR_SECTION_STOP_STR);		\
        assert("libqb's callsite section is observable, otherwise \
libqb's build is at fault, preventing reliable logging"			\
               && work_s1 != NULL && work_s2 != NULL);			\
        assert("libqb's callsite section is populated, otherwise \
libqb's build is at fault, preventing reliable logging"			\
               && work_s1 != work_s2);					\
        dlclose(work_handle); } }
#endif  /* _GNU_SOURCE */

/**
 * Optional on-demand self-check of 1/ toolchain sanity (prerequisite for
 * the logging subsystem to work properly) and 2/ non-void active use of
 * logging (satisfied with a justifying existence of a logging callsite as
 * defined with a @c qb_logt invocation) at the target (but see below), which
 * is supposedly assured by it's author(!) as of relying on this very macro
 * [technically, the symbols that happen to be resolved under the respective
 * identifiers do not necessarily originate in the same compilation unit as
 * when it's not the end executable (or by induction, a library positioned
 * earlier in the symbol lookup order) but a shared library, the former takes
 * a precedence unless that site comes short of exercising the logging,
 * making its callsite section empty and, in turn, without such boundary
 * symbols, hence making the resolution continue further in the lookup order
 * -- despite fuzzily targeted attestation, the check remains reasonable];
 * only effective when link-time ("run-time amortizing") callsite collection
 * is;  as a side effect, it can ensure the boundary-denoting symbols for the
 * target collection area are kept alive with some otherwise unkind linkers.
 *
 * Applying this macro in the target program/library is strongly recommended
 * whenever the logging as framed by this header file is in use.
 * Moreover, it's important to state that using this check while not ensuring
 * @c _GNU_SOURCE macro definition is present at compile-time means only half
 * of the available sanity checking will be performed, possibly resulting
 * in libqb's own internally logged messages being lost without warning.
 */
#define QB_LOG_INIT_DATA(name)						\
    void name(void);							\
    void name(void) {							\
    void *work_handle; struct qb_log_callsite *work_s1, *work_s2;	\
    /* our own (target's) sanity, or possibly that of higher priority	\
       symbol resolution site (unless target equals end executable)	\
       or even the lower one if no such predecessor defines these */	\
    if ((work_handle = dlopen(NULL, RTLD_LOCAL|RTLD_LAZY)) != NULL) {	\
        work_s1 = (struct qb_log_callsite *)				\
                  dlsym(work_handle, QB_ATTR_SECTION_START_STR);	\
        work_s2 = (struct qb_log_callsite *)				\
                  dlsym(work_handle, QB_ATTR_SECTION_STOP_STR);		\
        assert("implicit callsite section is observable, otherwise \
target's and/or libqb's build is at fault, preventing reliable logging" \
               && work_s1 != NULL && work_s2 != NULL);			\
        dlclose(work_handle);  /* perhaps overly eager thing to do */ }	\
    /* better targeted attestations when available  */			\
    QB_NONAPI_LOG_INIT_DATA_EXTRA_;					\
    /* finally, original, straightforward check */			\
    assert("implicit callsite section is populated, otherwise \
target's build is at fault, preventing reliable logging"		\
           && QB_ATTR_SECTION_START != QB_ATTR_SECTION_STOP); }		\
    void __attribute__ ((constructor)) name(void);
#else
#define QB_LOG_INIT_DATA(name)
#endif  /* QB_HAVE_ATTRIBUTE_SECTION */

/**
 * Internal function: use qb_log() or qb_logt()
 */
void qb_log_real_(struct qb_log_callsite *cs, ...);
void qb_log_real_va_(struct qb_log_callsite *cs, va_list ap);

#define QB_LOG_TAG_LIBQB_MSG_BIT 31
#define QB_LOG_TAG_LIBQB_MSG (1 << QB_LOG_TAG_LIBQB_MSG_BIT)

/**
 * This function is to import logs from other code (like libraries)
 * that provide a callback with their logs.
 *
 * @note the performance of this will not impress you, as
 * the filtering is done on each log message, not
 * beforehand. So try doing basic pre-filtering.
 *
 * @param function originating function name
 * @param filename originating filename
 * @param format format string
 * @param priority this takes syslog priorities.
 * @param lineno file line number
 * @param tags this is a uint32_t that you can use with
 *             qb_log_tags_stringify_fn_set() to "tag" a log message
 *             with a feature or sub-system then you can use "%g"
 *             in the format specifer to print it out.
 */
void qb_log_from_external_source(const char *function,
				 const char *filename,
				 const char *format,
				 uint8_t priority,
				 uint32_t lineno,
				 uint32_t tags,
				 ...)
	__attribute__ ((format (printf, 3, 7)));

/**
 * Get or create a callsite at the given position.
 *
 * The result can then be passed into qb_log_real_()
 *
 * @param function originating function name
 * @param filename originating filename
 * @param format format string
 * @param priority this takes syslog priorities.
 * @param lineno file line number
 * @param tags the tag
 */
struct qb_log_callsite* qb_log_callsite_get(const char *function,
					    const char *filename,
					    const char *format,
					    uint8_t priority,
					    uint32_t lineno,
					    uint32_t tags);

void qb_log_from_external_source_va(const char *function,
				    const char *filename,
				    const char *format,
				    uint8_t priority,
				    uint32_t lineno,
				    uint32_t tags,
				    va_list ap)
	__attribute__ ((format (printf, 3, 0)));

/**
 * This is the function to generate a log message if you want to
 * manually add tags.
 *
 * @param priority this takes syslog priorities.
 * @param tags this is a uint32_t that you can use with
 *             qb_log_tags_stringify_fn_set() to "tag" a log message
 *             with a feature or sub-system then you can use "%g"
 *             in the format specifer to print it out.
 * @param fmt usual printf style format specifiers
 * @param args usual printf style args
 */
#ifdef QB_HAVE_ATTRIBUTE_SECTION
#define qb_logt(priority, tags, fmt, args...) do {			\
	static struct qb_log_callsite descriptor			\
	__attribute__((section(QB_ATTR_SECTION_STR), aligned(8))) =	\
	{ __func__, __FILE__, fmt, priority, __LINE__, 0, tags };	\
	qb_log_real_(&descriptor, ##args);				\
    } while(0)
#else
#define qb_logt(priority, tags, fmt, args...) do {	\
	struct qb_log_callsite* descriptor_pt =		\
	qb_log_callsite_get(__func__, __FILE__, fmt,	\
			    priority, __LINE__, tags);	\
	qb_log_real_(descriptor_pt, ##args);		\
    } while(0)
#endif /* QB_HAVE_ATTRIBUTE_SECTION */


/**
 * This is the main function to generate a log message.
 *
 * @param priority this takes syslog priorities.
 * @param fmt usual printf style format specifiers
 * @param args usual printf style args
 */
#define qb_log(priority, fmt, args...) qb_logt(priority, 0, fmt, ##args)

/* Define the character used to mark the beginning of "extended" information;
 * a string equivalent is also defined so clients can use it like:
 *    qb_log(level, "blah blah "QB_XS" yada yada", __func__);
 */
#define QB_XC '\a'
#define QB_XS "\a"

/**
 * This is similar to perror except it goes into the logging system.
 *
 * @param priority this takes syslog priorities.
 * @param fmt usual printf style format specifiers
 * @param args usual printf style args
 *
 * @note Because qb_perror() adds the system error message and error number onto
 *       the end of the given fmt, that information will become extended
 *       information if QB_XS is used inside fmt and will not show up in any
 *       logs that strip extended information.
 */
#ifndef S_SPLINT_S
#define qb_perror(priority, fmt, args...) do {				\
	char _perr_buf_[QB_LOG_STRERROR_MAX_LEN];			\
	const char *_perr_str_ = qb_strerror_r(errno, _perr_buf_, sizeof(_perr_buf_));	\
	qb_logt(priority, 0, fmt ": %s (%d)", ##args, _perr_str_, errno); \
    } while(0)
#else
#define qb_perror
#endif

#define qb_enter() qb_log(LOG_TRACE, "ENTERING %s()", __func__)
#define qb_leave() qb_log(LOG_TRACE, "LEAVING %s()", __func__)

/*
 * Note that QB_LOG_TARGET_{STATIC_,}MAX are sentinel indexes
 * as non-inclusive higher bounds of the respective categories
 * (static and all the log targets) and also denote the number
 * of (reserved) items in the category.  Both are possibly subject
 * of change, hence it is only adequate to always refer to them
 * via these defined values.
 * Similarly, there are QB_LOG_TARGET_{STATIC_,DYNAMIC_,}START
 * and QB_LOG_TARGET_{STATIC_,DYNAMIC_,}END values, but these
 * are inclusive lower and higher bounds, respectively.
 */
enum qb_log_target_slot {
	QB_LOG_TARGET_START,

	/* static */
	QB_LOG_TARGET_STATIC_START = QB_LOG_TARGET_START,
	QB_LOG_SYSLOG = QB_LOG_TARGET_STATIC_START,
	QB_LOG_STDERR,
	QB_LOG_BLACKBOX,
	QB_LOG_STDOUT,
	QB_LOG_TARGET_STATIC_MAX,
	QB_LOG_TARGET_STATIC_END = QB_LOG_TARGET_STATIC_MAX - 1,

	/* dynamic */
	QB_LOG_TARGET_DYNAMIC_START = QB_LOG_TARGET_STATIC_MAX,

	QB_LOG_TARGET_MAX = 32,
	QB_LOG_TARGET_DYNAMIC_END = QB_LOG_TARGET_MAX - 1,
	QB_LOG_TARGET_END = QB_LOG_TARGET_DYNAMIC_END,
};

enum qb_log_target_state {
	QB_LOG_STATE_UNUSED = 1,
	QB_LOG_STATE_DISABLED = 2,
	QB_LOG_STATE_ENABLED = 3,
};

enum qb_log_conf {
	QB_LOG_CONF_ENABLED,
	QB_LOG_CONF_FACILITY,
	QB_LOG_CONF_DEBUG,
	QB_LOG_CONF_SIZE,
	QB_LOG_CONF_THREADED,
	QB_LOG_CONF_PRIORITY_BUMP,
	QB_LOG_CONF_STATE_GET,
	QB_LOG_CONF_FILE_SYNC,
	QB_LOG_CONF_EXTENDED,
	QB_LOG_CONF_IDENT,
};

enum qb_log_filter_type {
	QB_LOG_FILTER_FILE,
	QB_LOG_FILTER_FUNCTION,
	QB_LOG_FILTER_FORMAT,
	QB_LOG_FILTER_FILE_REGEX,
	QB_LOG_FILTER_FUNCTION_REGEX,
	QB_LOG_FILTER_FORMAT_REGEX,
};

enum qb_log_filter_conf {
	QB_LOG_FILTER_ADD,
	QB_LOG_FILTER_REMOVE,
	QB_LOG_FILTER_CLEAR_ALL,
	QB_LOG_TAG_SET,
	QB_LOG_TAG_CLEAR,
	QB_LOG_TAG_CLEAR_ALL,
};

typedef void (*qb_log_logger_fn)(int32_t t,
				 struct qb_log_callsite *cs,
				 time_t timestamp,
				 const char *msg);
typedef void (*qb_log_vlogger_fn)(int32_t t,
				 struct qb_log_callsite *cs,
				 time_t timestamp,
				 va_list ap);

typedef void (*qb_log_close_fn)(int32_t t);
typedef void (*qb_log_reload_fn)(int32_t t);

/**
 * Init the logging system.
 *
 * @param name will be passed into openlog()
 * @param facility default for all new targets.
 * @param priority a basic filter with this priority will be added.
 */
void qb_log_init(const char *name,
		 int32_t facility,
		 uint8_t priority);

/**
 * Logging system finalization function.
 *
 * It releases any shared memory.
 * Stops the logging thread if running.
 * Flushes the last messages to their destinations.
 */
void qb_log_fini(void);

/**
 * If you are using dynamically loadable modules via dlopen() and
 * you load them after qb_log_init() then after you load the module
 * you will need to do the following to get the filters to work
 * in that module:
 * @code
 * 	_start = dlsym (dl_handle, QB_ATTR_SECTION_START_STR);
 *	_stop = dlsym (dl_handle, QB_ATTR_SECTION_STOP_STR);
 *	qb_log_callsites_register(_start, _stop);
 * @endcode
 */
int32_t qb_log_callsites_register(struct qb_log_callsite *_start, struct qb_log_callsite *_stop);

/**
 * Dump the callsite info to stdout.
 */
void qb_log_callsites_dump(void);

/**
 * Main logging control function.
 *
 * @param target QB_LOG_SYSLOG, QB_LOG_STDERR or result from qb_log_file_open()
 * @param conf_type configuration directive ("what to configure") that accepts
 *        @c int32_t argument determining the new value unless ignored
 *        for particular directive altogether
 *        (incompatible directives: QB_LOG_CONF_IDENT)
 * @param arg the new value for a state-changing configuration directive,
 *        ignored otherwise
 * @see qb_log_conf
 *
 * @retval -errno on error
 * @retval 0 on success
 * @retval qb_log_target_state for QB_LOG_CONF_STATE_GET
 */
int32_t qb_log_ctl(int32_t target, enum qb_log_conf conf_type, int32_t arg);

typedef union {
	int32_t i32;
	const char *s;
} qb_log_ctl2_arg_t;

/**
 * Extension of main logging control function accepting also strings.
 *
 * @param target QB_LOG_SYSLOG, QB_LOG_STDERR or result from qb_log_file_open()
 * @param conf_type configuration directive ("what to configure") that accepts
 *        either @c int32_t or a null-terminated string argument
 *        determining the new value unless ignored for particular directive
 *        (compatible directives: those valid for qb_log_ctl
 *                                + QB_LOG_CONF_IDENT)
 * @param arg the new value for a state-changing configuration directive,
 *        ignored otherwise;  for QB_LOG_CONF_IDENT, 's' member as new
 *        identifier to openlog(), for all qb_log_ctl-compatible ones,
 *        'i32' member is assumed (although a preferred way is to use
 *        that original function directly as it allows for more type safety)
 * @see qb_log_ctl
 *
 * @note You can use @ref QB_LOG_CTL2_I32 and @ref QB_LOG_CTL2_S macros
 *       for a convenient on-the-fly construction of the object
 *       to be passed as an @p arg argument.
 */
int32_t qb_log_ctl2(int32_t target, enum qb_log_conf conf_type,
		    qb_log_ctl2_arg_t arg);

# ifndef S_SPLINT_S
#define QB_LOG_CTL2_I32(a)  ((qb_log_ctl2_arg_t) { .i32 = (a) })
#define QB_LOG_CTL2_S(a)    ((qb_log_ctl2_arg_t) { .s = (a) })
#else
#define QB_LOG_CTL2_I32(a)  ((qb_log_ctl2_arg_t)(a))
#define QB_LOG_CTL2_S(a)    ((qb_log_ctl2_arg_t)(a))
#endif

/**
 * This allows you modify the 'tags' and 'targets' callsite fields at runtime.
 */
int32_t qb_log_filter_ctl(int32_t value, enum qb_log_filter_conf c,
			  enum qb_log_filter_type type, const char * text,
			  uint8_t low_priority);


/**
 * This extends qb_log_filter_ctl() by been able to provide a high_priority.
 */
int32_t qb_log_filter_ctl2(int32_t value, enum qb_log_filter_conf c,
			  enum qb_log_filter_type type, const char * text,
			  uint8_t high_priority, uint8_t low_priority);


/**
 * Instead of using the qb_log_filter_ctl() functions you
 * can apply the filters manually by defining a callback
 * and setting the targets field using qb_bit_set() and
 * qb_bit_clear() like the following below:
 * @code
 * static void
 * m_filter(struct qb_log_callsite *cs)
 * {
 * 	if ((cs->priority >= LOG_ALERT &&
 * 	     cs->priority <= LOG_DEBUG) &&
 * 	     strcmp(cs->filename, "my_c_file.c") == 0) {
 * 		qb_bit_set(cs->targets, QB_LOG_SYSLOG);
 * 	} else {
 * 		qb_bit_clear(cs->targets, QB_LOG_SYSLOG);
 * 	}
 * }
 * @endcode
 */
int32_t qb_log_filter_fn_set(qb_log_filter_fn fn);

/**
 * Set the callback to map the 'tags' bit map to a string.
 */
void qb_log_tags_stringify_fn_set(qb_log_tags_stringify_fn fn);

/**
 * Set the format specifiers.
 *
 * %n FUNCTION NAME
 * %f FILENAME
 * %l FILELINE
 * %p PRIORITY
 * %t TIMESTAMP
 * %b BUFFER
 * %g TAGS
 * %N name (passed into qb_log_init)
 * %P PID
 * %H hostname
 *
 * Any number between % and character specify field length to pad or chop.
 *
 * @note Some of the fields are immediately evaluated and remembered
 *       for performance reasons, so when there's an objective for log
 *       messages to carry PIDs (not in the default setup) and, moreover,
 *       precisely, this function needs to be reinvoked upon @c fork
 *       (@c clone) in the respective children.  When already linking
 *       to @c libpthread, @c pthread_atfork callback registration
 *       could be useful.
 */
void qb_log_format_set(int32_t t, const char* format);

/**
 * Open a log file.
 *
 * @retval -errno on error
 * @retval value in inclusive range QB_LOG_TARGET_DYNAMIC_START
 *         to QB_LOG_TARGET_DYNAMIC_END
 *         (to be passed into other qb_log_* functions)
 */
int32_t qb_log_file_open(const char *filename);

/**
 * Close a log file and release is resources.
 */
void qb_log_file_close(int32_t t);

/**
 * When using threaded logging set the pthread policy and priority.
 *
 * @retval -errno on error
 * @retval 0 success
 */
int32_t qb_log_thread_priority_set(int32_t policy, int32_t priority);

/**
 * Start the logging pthread.
 */
int32_t qb_log_thread_start(void);

/**
 * Write the blackbox to file.
 */
ssize_t qb_log_blackbox_write_to_file(const char *filename);

/**
 * Read the blackbox for file and print it out.
 */
void qb_log_blackbox_print_from_file(const char* filename);

/**
 * Open a custom log target.
 *
 * @retval -errno on error
 * @retval value in inclusive range QB_LOG_TARGET_DYNAMIC_START
 *         to QB_LOG_TARGET_DYNAMIC_END
 *         (to be passed into other qb_log_* functions)
 */
int32_t qb_log_custom_open(qb_log_logger_fn log_fn,
			   qb_log_close_fn close_fn,
			   qb_log_reload_fn reload_fn,
			   void *user_data);

/**
 * Close a custom log target and release is resources.
 */
void qb_log_custom_close(int32_t t);

/**
 * Retrieve the user data set by either qb_log_custom_open or
 * qb_log_target_user_data_set.
 */
void *qb_log_target_user_data_get(int32_t t);

/**
 * Associate user data with this log target.
 * @note only use this with custom targets
 */
int32_t qb_log_target_user_data_set(int32_t t, void *user_data);

/**
 * Format the callsite and timestamp info according to the format.
 * set using qb_log_format_set()
 * It is intended to be used from your custom logger function.
 */
void qb_log_target_format(int32_t target,
			  struct qb_log_callsite *cs,
			  time_t timestamp,
			  const char* formatted_message,
			  char *output_buffer);

/**
 * Convert string "auth" to equivalent number "LOG_AUTH" etc.
 */
int32_t qb_log_facility2int(const char *fname);

/**
 * Convert number "LOG_AUTH" to equivalent string "auth" etc.
 */
const char * qb_log_facility2str(int32_t fnum);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */
#endif /* QB_LOG_H_DEFINED */
