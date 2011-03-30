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
#include <errno.h>
#include <syslog.h>

/**
 * @file qblog.h
 * The logging API provides four main parts (basics, filtering, threading & blackbox).
 *
 * The idea behind this logging system is not to be prescriptive but to provide a
 * set of tools to help the developer achieve what they want quickly and easily.
 *
 * @par Basic logging API.
 * Call qb_log() to generate a log message. Then to write the message
 * somewhere meaningful call qb_log_handler_set() and write the messages
 * where ever you wish.
 *
 * @par Filtering messages.
 * In your log handler you can filter messages based on priority, but
 * to provide more powerful and flexible filtering you can tag messages
 * based on their location. This means that CPU intensive string
 * comparisons are done up front and in your log handler you only need
 * to check a if a bit is set.
 *
 * @par Threaded logging.
 * To achieve non-blocking logging you can use threaded logging. With
 * this your log handler is called from a new pthread. So any calls to
 * write() or syslog() will not hold up your program.
 *
 * @par A blackbox for in-field diagnosis.
 * This stores log messages in a ringbuffer so they can be written to
 * file if the program crashes (you will need to catch SIGSEGV). These
 * can then be easily printed out later.
 *
 */


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
	uint32_t tags;
} __attribute__((aligned(8)));

/* will be assigned by ld linker magic */
extern struct qb_log_callsite __start___verbose[];
extern struct qb_log_callsite __stop___verbose[];

/**
 * Internal function: use qb_log()
 */
void qb_log_real_(struct qb_log_callsite *cs, ...);

/**
 * This function is to import logs from other code (like libraries)
 * that provide a callback with their logs.
 *
 * @note the performance of this will not impress you, as
 * the filtering is done on each log message, not
 * before hand. So try doing basic pre-filtering.
 */
void qb_log_from_external_source(const char *function,
				 const char *filename,
				 const char *format,
				 uint8_t priority,
				 uint32_t lineno,
				 const char *msg);
/**
 * This is the main function to generate a log message.
 *
 * @param priority this takes syslog priorities.
 * @param fmt usual printf style format specifiers
 * @param args usual printf style args
 */
#ifndef S_SPLINT_S
#define qb_log(priority, fmt, args...) do {			\
	static struct qb_log_callsite descriptor		\
	__attribute__((section("__verbose"), aligned(8))) =	\
	{ __func__, __FILE__, fmt, priority, __LINE__, 0 };	\
	qb_log_real_(&descriptor, ##args);			\
    } while(0)
#else
#define qb_log
#endif

/**
 * This is similar to perror except it goes into the logging system.
 *
 * @param priority this takes syslog priorities.
 * @param fmt usual printf style format specifiers
 * @param args usual printf style args
 */
#ifndef S_SPLINT_S
#define qb_perror(priority, fmt, args...) do {			\
	const char *err = strerror(errno);			\
	qb_log(priority, fmt ": %s (%d)", ##args, err, errno);	\
    } while(0)
#else
#define qb_perror
#endif

/**
 * Utility function to get the priority name from it's ID
 * like this: LOG_ERR - > "error"
 */
const char *qb_log_priority_name_get(uint32_t priority);

#define QB_LOG_SYSLOG 0
#define QB_LOG_STDERR 1
#define QB_LOG_BLACKBOX 2

enum qb_log_conf {
	QB_LOG_CONF_ENABLED,
	QB_LOG_CONF_FACILITY,
	QB_LOG_CONF_DEBUG,
	QB_LOG_CONF_SIZE,
	QB_LOG_CONF_THREADED,
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
};

void qb_log_init(const char *name,
		 int32_t facility,
		 int32_t priority);

int32_t qb_log_ctl(uint32_t t, enum qb_log_conf c, int32_t arg);

int32_t qb_log_filter_ctl(uint32_t t, enum qb_log_filter_conf c,
			  enum qb_log_filter_type type, const char * text,
			  uint32_t priority);

int32_t qb_log_file_open(const char *filename);

/**
 * Start the logging pthread.
 */
void qb_log_thread_start(void);

/**
 * Stop the logging pthread
 */
void qb_log_thread_stop(void);

/**
 * Write the blackbox to file.
 */
ssize_t qb_log_blackbox_write_to_file(const char *filename);

/**
 * Read the blackbox for file and print it out.
 */
void qb_log_blackbox_print_from_file(const char* filename);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */
#endif /* QB_LOG_H_DEFINED */
