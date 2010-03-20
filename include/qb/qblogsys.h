/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * Author: Steven Dake (sdake@redhat.com)
 * Author: Lon Hohberger (lhh@redhat.com)
 * Author: Fabio M. Di Nitto (fdinitto@redhat.com)
 *
 * All rights reserved.
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef QB_LOGSYS_H_DEFINED
#define QB_LOGSYS_H_DEFINED

#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All of the QB_LOGSYS_MODE's can be ORed together for combined behavior
 *
 * FORK and THREADED are ignored for SUBSYSTEMS
 */
#define QB_LOGSYS_MODE_OUTPUT_FILE		(1<<0)
#define QB_LOGSYS_MODE_OUTPUT_STDERR	(1<<1)
#define QB_LOGSYS_MODE_OUTPUT_SYSLOG	(1<<2)
#define QB_LOGSYS_MODE_FORK		(1<<3)
#define QB_LOGSYS_MODE_THREADED		(1<<4)

/*
 * Log priorities, compliant with syslog and SA Forum Log spec.
 */
#define QB_LOGSYS_LEVEL_EMERG		LOG_EMERG
#define QB_LOGSYS_LEVEL_ALERT		LOG_ALERT
#define QB_LOGSYS_LEVEL_CRIT		LOG_CRIT
#define QB_LOGSYS_LEVEL_ERROR		LOG_ERR
#define QB_LOGSYS_LEVEL_WARNING		LOG_WARNING
#define QB_LOGSYS_LEVEL_NOTICE		LOG_NOTICE
#define QB_LOGSYS_LEVEL_INFO		LOG_INFO
#define QB_LOGSYS_LEVEL_DEBUG		LOG_DEBUG

/*
 * All of the QB_LOGSYS_RECID's are mutually exclusive. Only one RECID at any time
 * can be specified.
 *
 * RECID_LOG indicates a message that should be sent to log. Anything else
 * is stored only in the flight recorder.
 */

#define QB_LOGSYS_RECID_MAX		((UINT_MAX) >> QB_LOGSYS_SUBSYSID_END)

#define QB_LOGSYS_RECID_LOG		(QB_LOGSYS_RECID_MAX - 1)
#define QB_LOGSYS_RECID_ENTER		(QB_LOGSYS_RECID_MAX - 2)
#define QB_LOGSYS_RECID_LEAVE		(QB_LOGSYS_RECID_MAX - 3)
#define QB_LOGSYS_RECID_TRACE1		(QB_LOGSYS_RECID_MAX - 4)
#define QB_LOGSYS_RECID_TRACE2		(QB_LOGSYS_RECID_MAX - 5)
#define QB_LOGSYS_RECID_TRACE3		(QB_LOGSYS_RECID_MAX - 6)
#define QB_LOGSYS_RECID_TRACE4		(QB_LOGSYS_RECID_MAX - 7)
#define QB_LOGSYS_RECID_TRACE5		(QB_LOGSYS_RECID_MAX - 8)
#define QB_LOGSYS_RECID_TRACE6		(QB_LOGSYS_RECID_MAX - 9)
#define QB_LOGSYS_RECID_TRACE7		(QB_LOGSYS_RECID_MAX - 10)
#define QB_LOGSYS_RECID_TRACE8		(QB_LOGSYS_RECID_MAX - 11)


/*
 * Internal APIs that must be globally exported
 * (External API below)
 */

/*
 * qb_logsys_logger bits
 *
 * SUBSYS_COUNT defines the maximum number of subsystems
 * SUBSYS_NAMELEN defines the maximum len of a subsystem name
 */
#define QB_LOGSYS_MAX_SUBSYS_COUNT		64
#define QB_LOGSYS_MAX_SUBSYS_NAMELEN	64

/*
 * rec_ident explained:
 *
 * rec_ident is an unsigned int and carries bitfields information
 * on subsys_id, log priority (level) and type of message (RECID).
 *
 * level values are imported from syslog.h.
 * At the time of writing it's a 3 bits value (0 to 7).
 *
 * subsys_id is any value between 0 and 64 (QB_LOGSYS_MAX_SUBSYS_COUNT)
 *
 * RECID identifies the type of message. A set of predefined values
 * are available via qb_logsys, but other custom values can be defined
 * by users.
 *
 * ----
 * bitfields:
 *
 * 0  - 2 level
 * 3  - 9 subsysid
 * 10 - n RECID
 */

#define QB_LOGSYS_LEVEL_END		(3)
#define QB_LOGSYS_SUBSYSID_END		(QB_LOGSYS_LEVEL_END + 7)

#define QB_LOGSYS_RECID_LEVEL_MASK		(LOG_PRIMASK)
#define QB_LOGSYS_RECID_SUBSYSID_MASK	((2 << (QB_LOGSYS_SUBSYSID_END - 1)) - \
					(LOG_PRIMASK + 1))
#define QB_LOGSYS_RECID_RECID_MASK		(UINT_MAX - \
					(QB_LOGSYS_RECID_SUBSYSID_MASK + LOG_PRIMASK))

#define QB_LOGSYS_ENCODE_RECID(level,subsysid,recid) \
	(((recid) << QB_LOGSYS_SUBSYSID_END) | \
	((subsysid) << QB_LOGSYS_LEVEL_END) | \
	(level))

#define QB_LOGSYS_DECODE_LEVEL(rec_ident) \
	((rec_ident) & QB_LOGSYS_RECID_LEVEL_MASK)

#define QB_LOGSYS_DECODE_SUBSYSID(rec_ident) \
	(((rec_ident) & QB_LOGSYS_RECID_SUBSYSID_MASK) >> QB_LOGSYS_LEVEL_END)

#define QB_LOGSYS_DECODE_RECID(rec_ident) \
	(((rec_ident) & QB_LOGSYS_RECID_RECID_MASK) >> QB_LOGSYS_SUBSYSID_END)

#ifndef QB_LOGSYS_UTILS_ONLY

extern int _logsys_system_setup(
	const char *mainsystem,
	unsigned int mode,
	unsigned int debug,
	const char *logfile,
	int logfile_priority,
	int syslog_facility,
	int syslog_priority);

extern int _logsys_config_subsys_get (
	const char *subsys);

extern unsigned int _logsys_subsys_create (const char *subsys);

extern int _logsys_rec_init (unsigned int size);

extern void _logsys_log_vprintf (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	const char *format,
	va_list ap) __attribute__((format(printf, 5, 0)));

extern void _logsys_log_printf (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	const char *format,
	...) __attribute__((format(printf, 5, 6)));

extern void _logsys_log_rec (
	unsigned int rec_ident,
	const char *function_name,
	const char *file_name,
	int file_line,
	...);

extern int _logsys_wthread_create (void);

static int qb_logsys_subsys_id __attribute__((unused)) = QB_LOGSYS_MAX_SUBSYS_COUNT;

/*
 * External API - init
 * See below:
 *
 * QB_LOGSYS_DECLARE_SYSTEM
 * QB_LOGSYS_DECLARE_SUBSYS
 *
 */
extern void qb_logsys_fork_completed (void);

extern void qb_logsys_atexit (void);

/*
 * External API - misc
 */
extern void qb_logsys_flush (void);

extern int qb_logsys_log_rec_store (const char *filename);

/*
 * External API - configuration
 */

/*
 * configuration bits that can only be done for the whole system
 */
extern int qb_logsys_format_set (
	const char *format);

extern char *qb_logsys_format_get (void);

/*
 * per system/subsystem settings.
 *
 * NOTE: once a subsystem is created and configured, changing
 * the default does NOT affect the subsystems.
 *
 * Pass a NULL subsystem to change them all
 */
extern unsigned int qb_logsys_config_syslog_facility_set (
	const char *subsys,
	unsigned int facility);

extern unsigned int qb_logsys_config_syslog_priority_set (
	const char *subsys,
	unsigned int priority);

extern unsigned int qb_logsys_config_mode_set (
	const char *subsys,
	unsigned int mode);

extern unsigned int qb_logsys_config_mode_get (
	const char *subsys);

/*
 * to close a logfile, just invoke this function with a NULL
 * file or if you want to change logfile, the old one will
 * be closed for you.
 */
extern int qb_logsys_config_file_set (
	const char *subsys,
	const char **error_string,
	const char *file);

extern unsigned int qb_logsys_config_logfile_priority_set (
	const char *subsys,
	unsigned int priority);

/*
 * enabling debug, disable message priority filtering.
 * everything is sent everywhere. priority values
 * for file and syslog are not overwritten.
 */
extern unsigned int qb_logsys_config_debug_set (
	const char *subsys,
	unsigned int value);

/*
 * External API - helpers
 *
 * convert facility/priority to/from name/values
 */
extern int qb_logsys_facility_id_get (
	const char *name);

extern const char *qb_logsys_facility_name_get (
	unsigned int facility);

extern int qb_logsys_priority_id_get (
	const char *name);

extern const char *qb_logsys_priority_name_get (
	unsigned int priority);

extern int qb_logsys_thread_priority_set (
	int policy,
	const struct sched_param *param,
	unsigned int after_log_ops_yield);

/*
 * External definitions
 */
extern void *qb_logsys_rec_end;

#define QB_LOGSYS_REC_END (&qb_logsys_rec_end)

#define QB_LOGSYS_DECLARE_SYSTEM(name,mode,debug,file,file_priority,	\
		syslog_facility,syslog_priority,format,fltsize)		\
__attribute__ ((constructor))						\
static void qb_logsys_system_init (void)					\
{									\
	if (_logsys_system_setup (name,mode,debug,file,file_priority,	\
			syslog_facility,syslog_priority) < 0) {		\
		fprintf (stderr,					\
			"Unable to setup logging system: %s.\n", name);	\
		exit (-1);						\
	}								\
									\
	if (qb_logsys_format_set (format) < 0) {				\
		fprintf (stderr,					\
			"Unable to setup logging format.\n");		\
		exit (-1);						\
	}								\
									\
	if (_logsys_rec_init (fltsize) < 0) {				\
		fprintf (stderr,					\
			"Unable to initialize log flight recorder.\n");	\
		exit (-1);						\
	}								\
									\
	if (_logsys_wthread_create() < 0) {				\
		fprintf (stderr,					\
			"Unable to initialize logging thread.\n");	\
		exit (-1);						\
	}								\
}

#define QB_LOGSYS_DECLARE_SUBSYS(subsys)					\
__attribute__ ((constructor))						\
static void qb_logsys_subsys_init (void)					\
{									\
	qb_logsys_subsys_id =						\
		_logsys_subsys_create ((subsys));			\
	if (qb_logsys_subsys_id == -1) {					\
		fprintf (stderr,					\
		"Unable to create logging subsystem: %s.\n", subsys);	\
		exit (-1);						\
	}								\
}

#define log_rec(rec_ident, args...)					\
do {									\
	_logsys_log_rec (rec_ident,  __FUNCTION__,			\
		__FILE__,  __LINE__, ##args,				\
		QB_LOGSYS_REC_END);					\
} while(0)

#define log_printf(level, format, args...)				\
 do {									\
	_logsys_log_printf (						\
		QB_LOGSYS_ENCODE_RECID(level,				\
				    qb_logsys_subsys_id,			\
				    QB_LOGSYS_RECID_LOG),			\
		 __FUNCTION__, __FILE__, __LINE__,			\
		format, ##args);					\
} while(0)

#define ENTER() do {							\
	_logsys_log_rec (						\
		QB_LOGSYS_ENCODE_RECID(QB_LOGSYS_LEVEL_DEBUG,			\
				    qb_logsys_subsys_id,			\
				    QB_LOGSYS_RECID_ENTER),		\
		__FUNCTION__, __FILE__,  __LINE__, QB_LOGSYS_REC_END);	\
} while(0)

#define LEAVE() do {							\
	_logsys_log_rec (						\
		QB_LOGSYS_ENCODE_RECID(QB_LOGSYS_LEVEL_DEBUG,			\
				    qb_logsys_subsys_id,			\
				    QB_LOGSYS_RECID_LEAVE),		\
		__FUNCTION__, __FILE__,  __LINE__, QB_LOGSYS_REC_END);	\
} while(0)

#define TRACE(recid, format, args...) do {				\
	_logsys_log_printf (						\
		QB_LOGSYS_ENCODE_RECID(QB_LOGSYS_LEVEL_DEBUG,			\
				    qb_logsys_subsys_id,			\
				    recid),				\
		 __FUNCTION__, __FILE__, __LINE__,			\
		format, ##args);					\
} while(0)

#define TRACE1(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE1, format, ##args);			\
} while(0)

#define TRACE2(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE2, format, ##args);			\
} while(0)

#define TRACE3(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE3, format, ##args);			\
} while(0)

#define TRACE4(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE4, format, ##args);			\
} while(0)

#define TRACE5(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE5, format, ##args);			\
} while(0)

#define TRACE6(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE6, format, ##args);			\
} while(0)

#define TRACE7(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE7, format, ##args);			\
} while(0)

#define TRACE8(format, args...) do {					\
	TRACE(QB_LOGSYS_RECID_TRACE8, format, ##args);			\
} while(0)

#endif /* QB_LOGSYS_UTILS_ONLY */

#ifdef __cplusplus
}
#endif

#endif /* QB_LOGSYS_H_DEFINED */
