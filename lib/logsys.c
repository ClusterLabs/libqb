/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>
 * Author: Fabio M. Di Nitto <fdinitto@redhat.com>
 *
 * This file is part of libqb.
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

#include <config.h>

#include "os_base.h"
#include <ctype.h>
#include <sys/time.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>

#include <qb/qbutil.h>
#include <qb/qblist.h>
#include <qb/qbrb.h>
#include <qb/qblogsys.h>

/*
 * syslog prioritynames, facility names to value mapping
 * Some C libraries build this in to their headers, but it is non-portable
 * so qb_logsys supplies its own version.
 */
struct syslog_names {
	const char *c_name;
	int32_t c_val;
};

struct syslog_names prioritynames[] = {
	{"alert", LOG_ALERT},
	{"crit", LOG_CRIT},
	{"debug", LOG_DEBUG},
	{"emerg", LOG_EMERG},
	{"err", LOG_ERR},
	{"error", LOG_ERR},
	{"info", LOG_INFO},
	{"notice", LOG_NOTICE},
	{"warning", LOG_WARNING},
	{NULL, -1}
};

struct syslog_names facilitynames[] = {
	{"auth", LOG_AUTH},
	{"cron", LOG_CRON},
	{"daemon", LOG_DAEMON},
	{"kern", LOG_KERN},
	{"lpr", LOG_LPR},
	{"mail", LOG_MAIL},
	{"news", LOG_NEWS},
	{"syslog", LOG_SYSLOG},
	{"user", LOG_USER},
	{"uucp", LOG_UUCP},
	{"local0", LOG_LOCAL0},
	{"local1", LOG_LOCAL1},
	{"local2", LOG_LOCAL2},
	{"local3", LOG_LOCAL3},
	{"local4", LOG_LOCAL4},
	{"local5", LOG_LOCAL5},
	{"local6", LOG_LOCAL6},
	{"local7", LOG_LOCAL7},
	{NULL, -1}
};

struct record {
	uint32_t rec_ident;
	const char *file_name;
	const char *function_name;
	int32_t file_line;
	char *buffer;
	struct qb_list_head list;
};

/*
 * need unlogical order to preserve 64bit alignment
 */
struct qb_logsys_logger {
	char subsys[QB_LOGSYS_MAX_SUBSYS_NAMELEN];	/* subsystem name */
	char *logfile;		/* log to file */
	FILE *logfile_fp;	/* track file descriptor */
	uint32_t mode;		/* subsystem mode */
	uint32_t debug;		/* debug on|off */
	int32_t syslog_facility;	/* facility */
	int32_t syslog_priority;	/* priority */
	int32_t logfile_priority;	/* priority to file */
	int32_t init_status;	/* internal field to handle init queues
				   for subsystems */
};

static qb_ringbuffer_t *rb = NULL;

#define COMBINE_BUFFER_SIZE 2048

/* values for logsys_logger init_status */
#define LOGSYS_LOGGER_INIT_DONE		0
#define LOGSYS_LOGGER_NEEDS_INIT	1

static int32_t logsys_system_needs_init = LOGSYS_LOGGER_NEEDS_INIT;

static int32_t logsys_memory_used = 0;

static int32_t logsys_sched_param_queued = 0;

static int32_t logsys_sched_policy;

static struct sched_param logsys_sched_param;

static int32_t logsys_after_log_ops_yield = 10;

static struct qb_logsys_logger logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT + 1];

static int32_t wthread_active = 0;

static int32_t wthread_should_exit = 0;

static pthread_mutex_t qb_logsys_config_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t records_written = 1;

static pthread_t logsys_thread_id;

static sem_t logsys_thread_start;

static sem_t logsys_print_finished;

static qb_thread_lock_t *logsys_flt_lock;
static qb_thread_lock_t *logsys_wthread_lock;

static char *format_buffer = NULL;

static int32_t logsys_dropped_messages = 0;

void *qb_logsys_rec_end;

static QB_DECLARE_LIST_INIT(logsys_print_finished_records);

#define FDMAX_ARGS	64

/* forward declarations */
static void qb_logsys_close_logfile(int32_t subsysid);

#ifdef QB_LOGSYS_DEBUG
static char *decode_mode(int32_t subsysid, char *buf, size_t buflen)
{
	memset(buf, 0, buflen);

	if (logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_OUTPUT_FILE)
		snprintf(buf + strlen(buf), buflen, "FILE,");

	if (logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_OUTPUT_STDERR)
		snprintf(buf + strlen(buf), buflen, "STDERR,");

	if (logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_OUTPUT_SYSLOG)
		snprintf(buf + strlen(buf), buflen, "SYSLOG,");

	if (subsysid == QB_LOGSYS_MAX_SUBSYS_COUNT) {
		if (logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_FORK)
			snprintf(buf + strlen(buf), buflen, "FORK,");

		if (logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_THREADED)
			snprintf(buf + strlen(buf), buflen, "THREADED,");
	}

	memset(buf + strlen(buf) - 1, 0, 1);

	return buf;
}

static const char *decode_debug(int32_t subsysid)
{
	if (logsys_loggers[subsysid].debug)
		return "on";

	return "off";
}

static const char *decode_status(int32_t subsysid)
{
	if (!logsys_loggers[subsysid].init_status)
		return "INIT_DONE";

	return "NEEDS_INIT";
}

static void dump_subsys_config(int32_t subsysid)
{
	char modebuf[1024];

	fprintf(stderr,
		"ID: %d\n"
		"subsys: %s\n"
		"logfile: %s\n"
		"logfile_fp: %p\n"
		"mode: %s\n"
		"debug: %s\n"
		"syslog_fac: %s\n"
		"syslog_pri: %s\n"
		"logfile_pri: %s\n"
		"init_status: %s\n",
		subsysid,
		logsys_loggers[subsysid].subsys,
		logsys_loggers[subsysid].logfile,
		logsys_loggers[subsysid].logfile_fp,
		decode_mode(subsysid, modebuf, sizeof(modebuf)),
		decode_debug(subsysid),
		qb_logsys_facility_name_get(logsys_loggers
					    [subsysid].syslog_facility),
		qb_logsys_priority_name_get(logsys_loggers
					    [subsysid].syslog_priority),
		qb_logsys_priority_name_get(logsys_loggers
					    [subsysid].logfile_priority),
		decode_status(subsysid));
}

static void dump_full_config(void)
{
	int32_t i;

	for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (strlen(logsys_loggers[i].subsys) > 0)
			dump_subsys_config(i);
	}
}
#endif

/*
 * Internal threaded logging implementation
 */
static inline int32_t strcpy_cutoff(char *dest, const char *src, size_t cutoff,
				    size_t buf_len)
{
	size_t len = strlen(src);
	if (buf_len <= 1) {
		if (buf_len == 0)
			dest[0] = 0;
		return 0;
	}

	if (cutoff == 0) {
		cutoff = len;
	}

	cutoff = QB_MIN(cutoff, buf_len - 1);
	len = QB_MIN(len, cutoff);
	memcpy(dest, src, len);
	memset(dest + len, ' ', cutoff - len);
	dest[cutoff] = '\0';

	return (cutoff);
}

static const char log_month_name[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*
 * %s SUBSYSTEM
 * %n FUNCTION NAME
 * %f FILENAME
 * %l FILELINE
 * %p PRIORITY
 * %t TIMESTAMP
 * %b BUFFER
 *
 * any number between % and character specify field length to pad or chop
*/
static void log_printf_to_logs(uint32_t rec_ident,
			       const char *file_name,
			       const char *function_name,
			       int32_t file_line, const char *buffer)
{
	char normal_output_buffer[COMBINE_BUFFER_SIZE];
	char syslog_output_buffer[COMBINE_BUFFER_SIZE];
	char char_time[128];
	char line_no[30];
	uint32_t format_buffer_idx = 0;
	uint32_t normal_output_buffer_idx = 0;
	uint32_t syslog_output_buffer_idx = 0;
	struct timeval tv;
	size_t cutoff;
	uint32_t normal_len, syslog_len;
	int32_t subsysid;
	uint32_t level;
	int32_t c;
	struct tm tm_res;

	if (QB_LOGSYS_DECODE_RECID(rec_ident) != QB_LOGSYS_RECID_LOG) {
		return;
	}

	subsysid = QB_LOGSYS_DECODE_SUBSYSID(rec_ident);
	level = QB_LOGSYS_DECODE_LEVEL(rec_ident);

	while ((c = format_buffer[format_buffer_idx])) {
		cutoff = 0;
		if (c != '%') {
			normal_output_buffer[normal_output_buffer_idx++] = c;
			syslog_output_buffer[syslog_output_buffer_idx++] = c;
			format_buffer_idx++;
		} else {
			const char *normal_p, *syslog_p;

			format_buffer_idx += 1;
			if (isdigit(format_buffer[format_buffer_idx])) {
				cutoff =
				    atoi(&format_buffer[format_buffer_idx]);
			}
			while (isdigit(format_buffer[format_buffer_idx])) {
				format_buffer_idx += 1;
			}

			switch (format_buffer[format_buffer_idx]) {
			case 's':
				normal_p = logsys_loggers[subsysid].subsys;
				syslog_p = logsys_loggers[subsysid].subsys;
				break;

			case 'n':
				normal_p = function_name;
				syslog_p = function_name;
				break;

			case 'f':
				normal_p = file_name;
				syslog_p = file_name;
				break;

			case 'l':
				sprintf(line_no, "%d", file_line);
				normal_p = line_no;
				syslog_p = line_no;
				break;

			case 't':
				gettimeofday(&tv, NULL);
				(void)localtime_r((time_t *) & tv.tv_sec,
						  &tm_res);
				snprintf(char_time, sizeof(char_time),
					 "%s %02d %02d:%02d:%02d",
					 log_month_name[tm_res.tm_mon],
					 tm_res.tm_mday, tm_res.tm_hour,
					 tm_res.tm_min, tm_res.tm_sec);
				normal_p = char_time;

				/*
				 * syslog does timestamping on its own.
				 * also strip extra space in case.
				 */
				syslog_p = "";
				break;

			case 'b':
				normal_p = buffer;
				syslog_p = buffer;
				break;

			case 'p':
				normal_p =
				    logsys_loggers
				    [QB_LOGSYS_MAX_SUBSYS_COUNT].subsys;
				syslog_p = "";
				break;

			default:
				normal_p = "";
				syslog_p = "";
				break;
			}
			normal_len =
			    strcpy_cutoff(normal_output_buffer +
					  normal_output_buffer_idx, normal_p,
					  cutoff, (sizeof(normal_output_buffer)
						   - normal_output_buffer_idx));
			normal_output_buffer_idx += normal_len;
			syslog_len =
			    strcpy_cutoff(syslog_output_buffer +
					  syslog_output_buffer_idx, syslog_p,
					  cutoff, (sizeof(syslog_output_buffer)
						   - syslog_output_buffer_idx));
			syslog_output_buffer_idx += syslog_len;
			format_buffer_idx += 1;
		}
		if ((normal_output_buffer_idx >=
		     sizeof(normal_output_buffer) - 2)
		    || (syslog_output_buffer_idx >=
			sizeof(syslog_output_buffer) - 1)) {
			/* Note: we make allowance for '\0' at the end of
			 * both of these arrays and normal_output_buffer also
			 * needs a '\n'.
			 */
			break;
		}
	}

	normal_output_buffer[normal_output_buffer_idx] = '\0';
	syslog_output_buffer[syslog_output_buffer_idx] = '\0';

	/*
	 * Output to syslog
	 */
	if ((logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_OUTPUT_SYSLOG) &&
	    ((level <= logsys_loggers[subsysid].syslog_priority) ||
	     (logsys_loggers[subsysid].debug != 0))) {
		syslog(level | logsys_loggers[subsysid].syslog_facility, "%s",
		       syslog_output_buffer);
	}

	/*
	 * Terminate string with \n \0
	 */
	normal_output_buffer[normal_output_buffer_idx++] = '\n';
	normal_output_buffer[normal_output_buffer_idx] = '\0';

	/*
	 * Output to configured file
	 */
	if (((logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_OUTPUT_FILE) &&
	     (logsys_loggers[subsysid].logfile_fp != NULL)) &&
	    ((level <= logsys_loggers[subsysid].logfile_priority) ||
	     (logsys_loggers[subsysid].debug != 0))) {
		/*
		 * Output to a file
		 */
		if ((fwrite
		     (normal_output_buffer, strlen(normal_output_buffer), 1,
		      logsys_loggers[subsysid].logfile_fp) < 1)
		    || (fflush(logsys_loggers[subsysid].logfile_fp) == EOF)) {
			char tmpbuffer[1024];
			/*
			 * if we are here, it's bad.. it's really really bad.
			 * Best thing would be to light a candle in a church
			 * and pray.
			 */
			snprintf(tmpbuffer, sizeof(tmpbuffer),
				 "QB_LOGSYS EMERGENCY: %s Unable to write to %s.",
				 logsys_loggers[subsysid].subsys,
				 logsys_loggers[subsysid].logfile);
			pthread_mutex_lock(&qb_logsys_config_mutex);
			qb_logsys_close_logfile(subsysid);
			logsys_loggers[subsysid].mode &=
			    ~QB_LOGSYS_MODE_OUTPUT_FILE;
			pthread_mutex_unlock(&qb_logsys_config_mutex);
			log_printf_to_logs(QB_LOGSYS_ENCODE_RECID
					   (QB_LOGSYS_LEVEL_EMERG, subsysid,
					    QB_LOGSYS_RECID_LOG), __FILE__,
					   __FUNCTION__, __LINE__, tmpbuffer);
		}
	}

	/*
	 * Output to stderr
	 */
	if ((logsys_loggers[subsysid].mode & QB_LOGSYS_MODE_OUTPUT_STDERR) &&
	    ((level <= logsys_loggers[subsysid].logfile_priority) ||
	     (logsys_loggers[subsysid].debug != 0))) {
		if (write
		    (STDERR_FILENO, normal_output_buffer,
		     strlen(normal_output_buffer)) < 0) {
			char tmpbuffer[1024];
			/*
			 * if we are here, it's bad.. it's really really bad.
			 * Best thing would be to light 20 candles for each saint
			 * in the calendar and pray a lot...
			 */
			pthread_mutex_lock(&qb_logsys_config_mutex);
			logsys_loggers[subsysid].mode &=
			    ~QB_LOGSYS_MODE_OUTPUT_STDERR;
			pthread_mutex_unlock(&qb_logsys_config_mutex);
			snprintf(tmpbuffer, sizeof(tmpbuffer),
				 "QB_LOGSYS EMERGENCY: %s Unable to write to STDERR.",
				 logsys_loggers[subsysid].subsys);
			log_printf_to_logs(QB_LOGSYS_ENCODE_RECID
					   (QB_LOGSYS_LEVEL_EMERG, subsysid,
					    QB_LOGSYS_RECID_LOG), __FILE__,
					   __FUNCTION__, __LINE__, tmpbuffer);
		}
	}
}

static void log_printf_to_logs_wthread(uint32_t rec_ident,
				       const char *file_name,
				       const char *function_name,
				       int32_t file_line, const char *buffer)
{
	struct record *rec;
	uint32_t length;

	rec = malloc(sizeof(struct record));
	if (rec == NULL) {
		return;
	}

	length = strlen(buffer);

	rec->rec_ident = rec_ident;
	rec->file_name = file_name;
	rec->function_name = function_name;
	rec->file_line = file_line;
	rec->buffer = malloc(length + 1);
	if (rec->buffer == NULL) {
		free(rec);
		return;
	}
	memcpy(rec->buffer, buffer, length + 1);

	qb_list_init(&rec->list);
	qb_thread_lock(logsys_wthread_lock);
	logsys_memory_used += length + 1 + sizeof(struct record);
	if (logsys_memory_used > 512000) {
		free(rec->buffer);
		free(rec);
		logsys_memory_used =
		    logsys_memory_used - length - 1 - sizeof(struct record);
		logsys_dropped_messages += 1;
		qb_thread_unlock(logsys_wthread_lock);
		return;

	} else {
		qb_list_add_tail(&rec->list, &logsys_print_finished_records);
	}
	qb_thread_unlock(logsys_wthread_lock);

	sem_post(&logsys_print_finished);
}

static void *logsys_worker_thread(void *data) __attribute__ ((noreturn));
static void *logsys_worker_thread(void *data)
{
	struct record *rec;
	int32_t dropped = 0;
	int32_t res;

	/*
	 * Signal wthread_create that the initialization process may continue
	 */
	sem_post(&logsys_thread_start);
	for (;;) {
		dropped = 0;
		sem_wait(&logsys_print_finished);

		qb_thread_lock(logsys_wthread_lock);
		if (wthread_should_exit) {
			int32_t value;

			res = sem_getvalue(&logsys_print_finished, &value);
			if (value == 0) {
				qb_thread_unlock(logsys_wthread_lock);
				pthread_exit(NULL);
			}
		}

		rec =
		    qb_list_entry(logsys_print_finished_records.next,
				  struct record, list);
		qb_list_del(&rec->list);
		logsys_memory_used = logsys_memory_used - strlen(rec->buffer) -
		    sizeof(struct record) - 1;
		dropped = logsys_dropped_messages;
		logsys_dropped_messages = 0;
		qb_thread_unlock(logsys_wthread_lock);
		if (dropped) {
			printf("%d messages lost\n", dropped);
		}
		log_printf_to_logs(rec->rec_ident,
				   rec->file_name,
				   rec->function_name,
				   rec->file_line, rec->buffer);
		free(rec->buffer);
		free(rec);
	}
}

static void wthread_create(void)
{
	int32_t res;

	if (wthread_active) {
		return;
	}

	wthread_active = 1;

	/*
	 * TODO: propagate pthread_create errors back to the caller
	 */
	res = pthread_create(&logsys_thread_id, NULL,
			     logsys_worker_thread, NULL);
	sem_wait(&logsys_thread_start);

	if (res == 0) {
		if (logsys_sched_param_queued == 1) {
			/*
			 * TODO: propagate logsys_thread_priority_set errors back to
			 * the caller
			 */
			res = qb_logsys_thread_priority_set(logsys_sched_policy,
							    &logsys_sched_param,
							    logsys_after_log_ops_yield);
			logsys_sched_param_queued = 0;
		}
	} else {
		wthread_active = 0;
	}
}

static int32_t _logsys_config_subsys_get_unlocked(const char *subsys)
{
	uint32_t i;

	if (!subsys) {
		return QB_LOGSYS_MAX_SUBSYS_COUNT;
	}

	for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (strcmp(logsys_loggers[i].subsys, subsys) == 0) {
			return i;
		}
	}

	return (-1);
}

static void syslog_facility_reconf(void)
{
	closelog();
	openlog(logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT].subsys,
		LOG_CONS | LOG_PID,
		logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT].syslog_facility);
}

/*
 * this is always invoked within the mutex, so it's safe to parse the
 * whole thing as we need.
 */
static void qb_logsys_close_logfile(int32_t subsysid)
{
	int32_t i;

	if ((logsys_loggers[subsysid].logfile_fp == NULL) &&
	    (logsys_loggers[subsysid].logfile == NULL)) {
		return;
	}

	/*
	 * if there is another subsystem or system using the same fp,
	 * then we clean our own structs, but we can't close the file
	 * as it is in use by somebody else.
	 * Only the last users will be allowed to perform the fclose.
	 */
	for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((logsys_loggers[i].logfile_fp ==
		     logsys_loggers[subsysid].logfile_fp) && (i != subsysid)) {
			logsys_loggers[subsysid].logfile = NULL;
			logsys_loggers[subsysid].logfile_fp = NULL;
			return;
		}
	}

	/*
	 * if we are here, we are the last users of that fp, so we can safely
	 * close it.
	 */
	fclose(logsys_loggers[subsysid].logfile_fp);
	logsys_loggers[subsysid].logfile_fp = NULL;
	free(logsys_loggers[subsysid].logfile);
	logsys_loggers[subsysid].logfile = NULL;
}

/*
 * we need a version that can work when somebody else is already
 * holding a config mutex lock or we will never get out of here
 */
static int32_t qb_logsys_config_file_set_unlocked(int32_t subsysid,
						  const char **error_string,
						  const char *file)
{
	static char error_string_response[512];
	int32_t i;

	qb_logsys_close_logfile(subsysid);

	if ((file == NULL) ||
	    (strcmp(logsys_loggers[subsysid].subsys, "") == 0)) {
		return (0);
	}

	if (strlen(file) >= PATH_MAX) {
		snprintf(error_string_response,
			 sizeof(error_string_response),
			 "%s: logfile name exceed maximum system filename lenght\n",
			 logsys_loggers[subsysid].subsys);
		*error_string = error_string_response;
		return (-1);
	}

	for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((logsys_loggers[i].logfile != NULL) &&
		    (strcmp(logsys_loggers[i].logfile, file) == 0) &&
		    (i != subsysid)) {
			logsys_loggers[subsysid].logfile =
			    logsys_loggers[i].logfile;
			logsys_loggers[subsysid].logfile_fp =
			    logsys_loggers[i].logfile_fp;
			return (0);
		}
	}

	logsys_loggers[subsysid].logfile = strdup(file);
	if (logsys_loggers[subsysid].logfile == NULL) {
		snprintf(error_string_response,
			 sizeof(error_string_response),
			 "Unable to allocate memory for logfile '%s'\n", file);
		*error_string = error_string_response;
		return (-1);
	}

	logsys_loggers[subsysid].logfile_fp = fopen(file, "a+");
	if (logsys_loggers[subsysid].logfile_fp == NULL) {
		char error_str[100];
		strerror_r(errno, error_str, 100);
		free(logsys_loggers[subsysid].logfile);
		logsys_loggers[subsysid].logfile = NULL;
		snprintf(error_string_response,
			 sizeof(error_string_response),
			 "Can't open logfile '%s' for reason (%s).\n",
			 file, error_str);
		*error_string = error_string_response;
		return (-1);
	}

	return (0);
}

static void qb_logsys_subsys_init(const char *subsys, int32_t subsysid)
{
	if (logsys_system_needs_init == LOGSYS_LOGGER_NEEDS_INIT) {
		logsys_loggers[subsysid].init_status = LOGSYS_LOGGER_NEEDS_INIT;
	} else {
		memcpy(&logsys_loggers[subsysid],
		       &logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT],
		       sizeof(logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT]));
		logsys_loggers[subsysid].init_status = LOGSYS_LOGGER_INIT_DONE;
	}
	strncpy(logsys_loggers[subsysid].subsys, subsys,
		QB_LOGSYS_MAX_SUBSYS_NAMELEN);
}

/*
 * Internal API - exported
 */

int32_t _logsys_system_setup(const char *mainsystem,
			     uint32_t mode,
			     uint32_t debug,
			     const char *logfile,
			     int32_t logfile_priority,
			     int32_t syslog_facility, int32_t syslog_priority)
{
	int32_t i;
	const char *errstr;
	char tempsubsys[QB_LOGSYS_MAX_SUBSYS_NAMELEN];

	if ((mainsystem == NULL) ||
	    (strlen(mainsystem) >= QB_LOGSYS_MAX_SUBSYS_NAMELEN)) {
		return -1;
	}

	i = QB_LOGSYS_MAX_SUBSYS_COUNT;

	pthread_mutex_lock(&qb_logsys_config_mutex);

	snprintf(logsys_loggers[i].subsys,
		 QB_LOGSYS_MAX_SUBSYS_NAMELEN, "%s", mainsystem);

	logsys_loggers[i].mode = mode;

	logsys_loggers[i].debug = debug;

	if (qb_logsys_config_file_set_unlocked(i, &errstr, logfile) < 0) {
		pthread_mutex_unlock(&qb_logsys_config_mutex);
		return (-1);
	}
	logsys_loggers[i].logfile_priority = logfile_priority;

	logsys_loggers[i].syslog_facility = syslog_facility;
	logsys_loggers[i].syslog_priority = syslog_priority;
	syslog_facility_reconf();

	logsys_loggers[i].init_status = LOGSYS_LOGGER_INIT_DONE;

	logsys_system_needs_init = LOGSYS_LOGGER_INIT_DONE;

	for (i = 0; i < QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if ((strcmp(logsys_loggers[i].subsys, "") != 0) &&
		    (logsys_loggers[i].init_status ==
		     LOGSYS_LOGGER_NEEDS_INIT)) {
			strncpy(tempsubsys, logsys_loggers[i].subsys,
				QB_LOGSYS_MAX_SUBSYS_NAMELEN);
			qb_logsys_subsys_init(tempsubsys, i);
		}
	}

	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return (0);
}

uint32_t _logsys_subsys_create(const char *subsys)
{
	int32_t i;

	if ((subsys == NULL) ||
	    (strlen(subsys) >= QB_LOGSYS_MAX_SUBSYS_NAMELEN)) {
		return -1;
	}

	pthread_mutex_lock(&qb_logsys_config_mutex);

	i = _logsys_config_subsys_get_unlocked(subsys);
	if ((i > -1) && (i < QB_LOGSYS_MAX_SUBSYS_COUNT)) {
		pthread_mutex_unlock(&qb_logsys_config_mutex);
		return i;
	}

	for (i = 0; i < QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
		if (strcmp(logsys_loggers[i].subsys, "") == 0) {
			qb_logsys_subsys_init(subsys, i);
			break;
		}
	}

	if (i >= QB_LOGSYS_MAX_SUBSYS_COUNT) {
		i = -1;
	}

	pthread_mutex_unlock(&qb_logsys_config_mutex);
	return i;
}

int32_t _logsys_wthread_create(void)
{
	if (((logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT].mode &
	      QB_LOGSYS_MODE_FORK) == 0)
	    &&
	    ((logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT].mode &
	      QB_LOGSYS_MODE_THREADED) != 0)) {
		wthread_create();
		atexit(qb_logsys_atexit);
	}
	return (0);
}

int32_t _logsys_rec_init(uint32_t fltsize)
{
	sem_init(&logsys_thread_start, 0, 0);

	sem_init(&logsys_print_finished, 0, 0);

	logsys_flt_lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	logsys_wthread_lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);

	rb = qb_rb_open("logsys", fltsize,
			QB_RB_FLAG_OVERWRITE | QB_RB_FLAG_CREATE);

	return (0);
}

/*
 * u32 RECORD SIZE
 * u32 record ident
 * u32 arg count
 * u32 file line
 * u32 subsys length
 * buffer null terminated subsys
 * u32 filename length
 * buffer null terminated filename
 * u32 filename length
 * buffer null terminated function
 * u32 arg1 length
 * buffer arg1
 * ... repeats length & arg
 */

void _logsys_log_rec(uint32_t rec_ident,
		     const char *function_name,
		     const char *file_name, int32_t file_line, ...)
{
	va_list ap;
	const void *buf_args[FDMAX_ARGS];
	uint32_t buf_len[FDMAX_ARGS];
	uint32_t i;
	uint32_t idx;
	uint32_t arguments = 0;
	uint32_t record_reclaim_size = 0;
	int32_t subsysid;
	int32_t *flt_data;

	subsysid = QB_LOGSYS_DECODE_SUBSYSID(rec_ident);

	/*
	 * Decode VA Args
	 */
	va_start(ap, file_line);
	arguments = 3;
	for (;;) {
		buf_args[arguments] = va_arg(ap, void *);
		if (buf_args[arguments] == QB_LOGSYS_REC_END) {
			break;
		}
		buf_len[arguments] = va_arg(ap, int32_t);
		record_reclaim_size += ((buf_len[arguments] + 3) >> 2) + 1;
		arguments++;
		if (arguments >= FDMAX_ARGS) {
			break;
		}
	}
	va_end(ap);

	/*
	 * Encode qb_logsys subsystem identity, filename, and function
	 */
	buf_args[0] = logsys_loggers[subsysid].subsys;
	buf_len[0] = strlen(logsys_loggers[subsysid].subsys) + 1;
	buf_args[1] = file_name;
	buf_len[1] = strlen(file_name) + 1;
	buf_args[2] = function_name;
	buf_len[2] = strlen(function_name) + 1;
	for (i = 0; i < 3; i++) {
		record_reclaim_size += ((buf_len[i] + 3) >> 2) + 1;
	}

	record_reclaim_size += 4;
	qb_thread_lock(logsys_flt_lock);

	flt_data = qb_rb_chunk_alloc(rb,
				       (record_reclaim_size * sizeof(uint32_t)));
	assert(flt_data != NULL);
	idx = 0;

	flt_data[idx++] = rec_ident;
	flt_data[idx++] = file_line;
	flt_data[idx++] = records_written;
	/*
	 * Encode all of the arguments into the log message
	 */
	for (i = 0; i < arguments; i++) {
		uint32_t bytes;
		uint32_t full_words;
		uint32_t total_words;

		bytes = buf_len[i];
		full_words = bytes >> 2;
		total_words = (bytes + 3) >> 2;

		flt_data[idx++] = total_words;
		memcpy(&flt_data[idx], buf_args[i], buf_len[i]);

		idx += total_words;
	}

	/*
	 * Commit the write of the record size now that the full record
	 * is in the memory buffer
	 */
	if (record_reclaim_size < idx) {
		printf("record_reclaim_size:%u idx:%d commit_size:%lu",
		       record_reclaim_size, idx, (idx * sizeof(uint32_t)));
		sleep(1);
		assert(0);
	}
	qb_rb_chunk_commit(rb, (idx * sizeof(uint32_t)));

	qb_thread_unlock(logsys_flt_lock);
	records_written++;
}

void _logsys_log_vprintf(uint32_t rec_ident,
			 const char *function_name,
			 const char *file_name,
			 int32_t file_line, const char *format, va_list ap)
{
	char logsys_print_buffer[COMBINE_BUFFER_SIZE];
	uint32_t len;
	uint32_t level;
	int32_t subsysid;
	const char *short_file_name;

	subsysid = QB_LOGSYS_DECODE_SUBSYSID(rec_ident);
	level = QB_LOGSYS_DECODE_LEVEL(rec_ident);

	len = vsprintf(logsys_print_buffer, format, ap);
	if (logsys_print_buffer[len - 1] == '\n') {
		logsys_print_buffer[len - 1] = '\0';
		len -= 1;
	}
#ifdef BUILDING_IN_PLACE
	short_file_name = file_name;
#else
	short_file_name = strrchr(file_name, '/');
	if (short_file_name == NULL)
		short_file_name = file_name;
	else
		short_file_name++;	/* move past the "/" */
#endif /* BUILDING_IN_PLACE */

	/*
	 * Create a log record
	 */
	_logsys_log_rec(rec_ident,
			function_name,
			short_file_name,
			file_line,
			logsys_print_buffer, len + 1, QB_LOGSYS_REC_END);

	/*
	 * If logsys is not going to print a message to a log target don't
	 * queue one
	 */
	if ((level > logsys_loggers[subsysid].syslog_priority &&
	     level > logsys_loggers[subsysid].logfile_priority &&
	     logsys_loggers[subsysid].debug == 0) ||
	    (level == QB_LOGSYS_LEVEL_DEBUG &&
	     logsys_loggers[subsysid].debug == 0)) {

		return;
	}

	if ((logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT].mode &
	     QB_LOGSYS_MODE_THREADED) == 0) {
		/*
		 * Output (and block) if the log mode is not threaded otherwise
		 * expect the worker thread to output the log data once signaled
		 */
		log_printf_to_logs(rec_ident,
				   short_file_name,
				   function_name,
				   file_line, logsys_print_buffer);
	} else {
		/*
		 * Signal worker thread to display logging output
		 */
		log_printf_to_logs_wthread(rec_ident,
					   short_file_name,
					   function_name,
					   file_line, logsys_print_buffer);
	}
}

void _logsys_log_printf(uint32_t rec_ident,
			const char *function_name,
			const char *file_name,
			int32_t file_line, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	_logsys_log_vprintf(rec_ident, function_name, file_name, file_line,
			    format, ap);
	va_end(ap);
}

int32_t _logsys_config_subsys_get(const char *subsys)
{
	uint32_t i;

	pthread_mutex_lock(&qb_logsys_config_mutex);

	i = _logsys_config_subsys_get_unlocked(subsys);

	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return i;
}

/*
 * External Configuration and Initialization API
 */
void qb_logsys_fork_completed(void)
{
	logsys_loggers[QB_LOGSYS_MAX_SUBSYS_COUNT].mode &= ~QB_LOGSYS_MODE_FORK;
	_logsys_wthread_create();
}

int32_t qb_logsys_config_mode_set(const char *subsys, uint32_t mode)
{
	int32_t i;

	pthread_mutex_lock(&qb_logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked(subsys);
		if (i >= 0) {
			logsys_loggers[i].mode = mode;
			i = 0;
		}
	} else {
		for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].mode = mode;
		}
		i = 0;
	}
	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return i;
}

uint32_t qb_logsys_config_mode_get(const char *subsys)
{
	int32_t i;

	i = _logsys_config_subsys_get(subsys);
	if (i < 0) {
		return i;
	}

	return logsys_loggers[i].mode;
}

int32_t qb_logsys_config_file_set(const char *subsys,
				  const char **error_string, const char *file)
{
	int32_t i;
	int32_t res;

	pthread_mutex_lock(&qb_logsys_config_mutex);

	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked(subsys);
		if (i < 0) {
			res = i;
		} else {
			res =
			    qb_logsys_config_file_set_unlocked(i, error_string,
							       file);
		}
	} else {
		for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
			res =
			    qb_logsys_config_file_set_unlocked(i, error_string,
							       file);
			if (res < 0) {
				break;
			}
		}
	}

	pthread_mutex_unlock(&qb_logsys_config_mutex);
	return res;
}

int32_t qb_logsys_format_set(const char *format)
{
	int32_t ret = 0;

	pthread_mutex_lock(&qb_logsys_config_mutex);

	if (format_buffer) {
		free(format_buffer);
		format_buffer = NULL;
	}

	format_buffer = strdup(format ? format : "%p [%6s] %b");
	if (format_buffer == NULL) {
		ret = -1;
	}

	pthread_mutex_unlock(&qb_logsys_config_mutex);
	return ret;
}

char *qb_logsys_format_get(void)
{
	return format_buffer;
}

int32_t qb_logsys_config_syslog_facility_set(const char *subsys,
					     uint32_t facility)
{
	int32_t i;

	pthread_mutex_lock(&qb_logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked(subsys);
		if (i >= 0) {
			logsys_loggers[i].syslog_facility = facility;
			if (i == QB_LOGSYS_MAX_SUBSYS_COUNT) {
				syslog_facility_reconf();
			}
			i = 0;
		}
	} else {
		for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].syslog_facility = facility;
		}
		syslog_facility_reconf();
		i = 0;
	}
	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return i;
}

int32_t qb_logsys_config_syslog_priority_set(const char *subsys,
					     uint32_t priority)
{
	int32_t i;

	pthread_mutex_lock(&qb_logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked(subsys);
		if (i >= 0) {
			logsys_loggers[i].syslog_priority = priority;
			i = 0;
		}
	} else {
		for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].syslog_priority = priority;
		}
		i = 0;
	}
	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return i;
}

int32_t qb_logsys_config_logfile_priority_set(const char *subsys,
					      uint32_t priority)
{
	int32_t i;

	pthread_mutex_lock(&qb_logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked(subsys);
		if (i >= 0) {
			logsys_loggers[i].logfile_priority = priority;
			i = 0;
		}
	} else {
		for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].logfile_priority = priority;
		}
		i = 0;
	}
	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return i;
}

int32_t qb_logsys_config_debug_set(const char *subsys, uint32_t debug)
{
	int32_t i;

	pthread_mutex_lock(&qb_logsys_config_mutex);
	if (subsys != NULL) {
		i = _logsys_config_subsys_get_unlocked(subsys);
		if (i >= 0) {
			logsys_loggers[i].debug = debug;
			i = 0;
		}
	} else {
		for (i = 0; i <= QB_LOGSYS_MAX_SUBSYS_COUNT; i++) {
			logsys_loggers[i].debug = debug;
		}
		i = 0;
	}
	pthread_mutex_unlock(&qb_logsys_config_mutex);

	return i;
}

int32_t qb_logsys_facility_id_get(const char *name)
{
	uint32_t i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, facilitynames[i].c_name) == 0) {
			return (facilitynames[i].c_val);
		}
	}
	return (-1);
}

const char *qb_logsys_facility_name_get(uint32_t facility)
{
	uint32_t i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (facility == facilitynames[i].c_val) {
			return (facilitynames[i].c_name);
		}
	}
	return (NULL);
}

int32_t qb_logsys_priority_id_get(const char *name)
{
	uint32_t i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (strcasecmp(name, prioritynames[i].c_name) == 0) {
			return (prioritynames[i].c_val);
		}
	}
	return (-1);
}

const char *qb_logsys_priority_name_get(uint32_t priority)
{
	uint32_t i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (priority == prioritynames[i].c_val) {
			return (prioritynames[i].c_name);
		}
	}
	return (NULL);
}

int32_t qb_logsys_thread_priority_set(int32_t policy,
				      const struct sched_param * param,
				      uint32_t after_log_ops_yield)
{
	int32_t res = 0;

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && defined(HAVE_SCHED_GET_PRIORITY_MAX)
	if (wthread_active == 0) {
		qb_logsys_sched_policy = policy;
		memcpy(&qb_logsys_sched_param, &param,
		       sizeof(struct sched_param));
		qb_logsys_sched_param_queued = 1;
	} else {
		res = pthread_setschedparam(qb_logsys_thread_id, policy, param);
	}
#endif

	if (after_log_ops_yield > 0) {
		logsys_after_log_ops_yield = after_log_ops_yield;
	}

	return (res);
}

int32_t qb_logsys_log_rec_store(const char *filename)
{
	int32_t fd;
	ssize_t written_size;

	fd = open(filename, O_CREAT | O_RDWR, 0700);
	if (fd < 0) {
		return (-1);
	}

	written_size = qb_rb_write_to_file(rb, fd);
	if (close(fd) != 0)
		return (-1);
	if (written_size < 0) {
		return (-1);
	}

	return (0);
}

void qb_logsys_atexit(void)
{
	int32_t res;
	int32_t value;
	struct record *rec;

	if (wthread_active == 0) {
		for (;;) {
			qb_thread_lock(logsys_wthread_lock);

			res = sem_getvalue(&logsys_print_finished, &value);
			if (value == 0) {
				qb_thread_unlock(logsys_wthread_lock);
				return;
			}
			sem_wait(&logsys_print_finished);

			rec =
			    qb_list_entry(logsys_print_finished_records.next,
					  struct record, list);
			qb_list_del(&rec->list);
			logsys_memory_used =
			    logsys_memory_used - strlen(rec->buffer) -
			    sizeof(struct record) - 1;
			qb_thread_unlock(logsys_wthread_lock);
			log_printf_to_logs(rec->rec_ident,
					   rec->file_name,
					   rec->function_name,
					   rec->file_line, rec->buffer);
			free(rec->buffer);
			free(rec);
		}
	} else {
		wthread_should_exit = 1;
		sem_post(&logsys_print_finished);
		pthread_join(logsys_thread_id, NULL);
	}
}

void qb_logsys_flush(void)
{
}
