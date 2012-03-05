/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#ifdef S_SPLINT_S
#undef QB_HAVE_ATTRIBUTE_SECTION
#endif /* S_SPLINT_S */

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
 * Simplist possible use:
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
 * @par Configuring log targets.
 * A log target can by syslog, stderr, the blackbox or a text file.
 * By default only syslog is enabled.
 *
 * To enable a target do the following
 * @code
 *	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
 * @endcode
 *
 * syslog, stderr and the blackbox are static (they don't need
 * to be created, just enabled or disabled. However you can open multiple
 * logfiles (32 - QB_LOG_BLACKBOX). To do this use the following code.
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
 * So to make all logs from evil_fnunction() go to stderr do the following:
 * @code
 *	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FUNCTION, "evil_fnunction", LOG_TRACE);
 * @endcode
 *
 * So to make all logs from totem* (with  a priority <= LOG_INFO) go to stderr do the following:
 * @code
 *	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FILE, "totem", LOG_INFO);
 * @endcode
 *
 * So to make all logs with the substring "ringbuffer" go to stderr do the following:
 * @code
 *	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
 *			  QB_LOG_FILTER_FORMAT, "ringbuffer", LOG_TRACE);
 * @endcode
 *
 * @par Threaded logging.
 * To achieve non-blocking logging you can use threaded logging. So any
 * calls to write() or syslog() will not hold up your program.
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

/* will be assigned by ld linker magic */
#ifdef QB_HAVE_ATTRIBUTE_SECTION
extern struct qb_log_callsite __start___verbose[];
extern struct qb_log_callsite __stop___verbose[];

#define QB_LOG_INIT_DATA(name)						\
    void name(void);							\
    void name(void) { if (__start___verbose != __stop___verbose) {assert(1);} }	\
    void __attribute__ ((constructor)) name(void);
#else
#define QB_LOG_INIT_DATA(name)
#endif

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
 * before hand. So try doing basic pre-filtering.
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
				 ...);

/**
 * Get or create a callsite at the give position.
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
				    va_list ap);

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
	__attribute__((section("__verbose"), aligned(8))) =		\
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

/**
 * This is similar to perror except it goes into the logging system.
 *
 * @param priority this takes syslog priorities.
 * @param fmt usual printf style format specifiers
 * @param args usual printf style args
 */
#ifndef S_SPLINT_S
#define qb_perror(priority, fmt, args...) do {				\
	char _perr_buf_[QB_LOG_STRERROR_MAX_LEN];			\
	const char *_perr_str_ = qb_strerror_r(errno, _perr_buf_, sizeof(_perr_buf_));	\
	qb_logt(priority, 0, fmt ": %s (%d)", ##args, _perr_str_, errno);		\
    } while(0)
#else
#define qb_perror
#endif

#define qb_enter() qb_log(LOG_TRACE, "ENTERING %s()", __func__)
#define qb_leave() qb_log(LOG_TRACE, "LEAVING %s()", __func__)

#define QB_LOG_SYSLOG 0
#define QB_LOG_STDERR 1
#define QB_LOG_BLACKBOX 2
#define QB_LOG_STDOUT 3

#define QB_LOG_TARGET_MAX 32

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
};

enum qb_log_filter_type {
	QB_LOG_FILTER_FILE,
	QB_LOG_FILTER_FUNCTION,
	QB_LOG_FILTER_FORMAT,
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
 * Flushes the last message to their destinations.
 */
void qb_log_fini(void);

/**
 * If you are using dynamically loadable modules via dlopen() and
 * you load them after qb_log_init() then after you load the module
 * you will need to do the following to get the filters to work
 * in that module.
 * @code
 * 	_start = dlsym (dl_handle, "__start___verbose");
 *	_stop = dlsym (dl_handle, "__stop___verbose");
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
 * @param conf_type what to configure
 * @param arg the new value
 * @see qb_log_conf
 *
 * @retval -errno on error
 * @retval 0 on success
 * @retval qb_log_target_state for QB_LOG_CONF_STATE_GET
 */
int32_t qb_log_ctl(int32_t target, enum qb_log_conf conf_type, int32_t arg);

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
 * qb_bit_clear() like the following below.
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
 * any number between % and character specify field length to pad or chop
 */
void qb_log_format_set(int32_t t, const char* format);

/**
 * Open a log file.
 *
 * @retval -errno on error
 * @retval 3 to 31 (to be passed into other qb_log_* functions)
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
 * @retval 3 to 31 (to be passed into other qb_log_* functions)
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
 * Associate user data with this log target
 * @note only use this with custom targets
 */
int32_t qb_log_target_user_data_set(int32_t t, void *user_data);

/**
 * format the callsite and timestamp info according to the format
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
