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


typedef struct qb_log_filter qb_log_filter_t;

typedef void (*qb_log_logger_fn)(struct qb_log_callsite *cs,
				 const char *msg);

/* will be assigned by ld linker magic */
extern struct qb_log_callsite __start___verbose[];
extern struct qb_log_callsite __stop___verbose[];

/**
 * Internal function use qb_log()
 */
void qb_log_real_(struct qb_log_callsite *cs,
		  int32_t error_number, ...);

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
	qb_log_real_(&descriptor, 0, ##args);			\
    } while(0)
#else
#define qb_log
#endif

/**
 * Create a filter to tag the callsites.
 */
qb_log_filter_t* qb_log_filter_create(void);

/**
 * destroy a filter.
 */
void qb_log_filter_destroy(struct qb_log_filter* flt);

/**
 * Set the priority on the filter
 */
int32_t qb_log_filter_priority_set(qb_log_filter_t* flt, uint8_t priority);

/**
 * add a chunk of code to a filter.
 */
void qb_log_filter_file_add(struct qb_log_filter* flt,
			    const char* filename,
			    int32_t start, int32_t end);

/**
 * tag the callsites defined by the filter.
 */
void qb_log_tag(struct qb_log_filter* flt, int32_t is_set, int32_t tag_bit);

/**
 * set your log handling function.
 */
void qb_log_handler_set(qb_log_logger_fn logger_fn);

/**
 * Start the logging pthread.
 */
void qb_log_thread_start(void);

/**
 * Stop the logging pthread
 */
void qb_log_thread_stop(void);

/**
 * Initialize the blackbox
 *
 * @param size the size of the blackbox.
 */
void qb_log_blackbox_start(size_t size);

/**
 * Add the log message to the blackbox.
 *
 * @note call this from your log handler
 * @see qb_log_handler_set()
 */
void qb_log_blackbox_append(struct qb_log_callsite *cs,
		      const char *buffer);

/**
 * Write the blackbox to file.
 */
ssize_t qb_log_blackbox_write_to_file(const char *filename);

/**
 * Read the blackbox for file and print it out.
 */
void qb_log_blackbox_print_from_file(const char* filename);

#endif /* QB_LOG_H_DEFINED */
