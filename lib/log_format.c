/*
 * Copyright (C) 2011,2016 Red Hat, Inc.
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
#include "os_base.h"
#include <ctype.h>

#include <qb/qbdefs.h>
#include "log_int.h"

static qb_log_tags_stringify_fn _user_tags_stringify_fn;

/*
 * syslog prioritynames, facility names to value mapping
 * Some C libraries build this in to their headers, but it is non-portable
 * so logsys supplies its own version.
 */
struct syslog_names {
	const char *c_name;
	int32_t c_val;
};

static struct syslog_names prioritynames[] = {
	{"emerg", LOG_EMERG},
	{"alert", LOG_ALERT},
	{"crit", LOG_CRIT},
	{"error", LOG_ERR},
	{"warning", LOG_WARNING},
	{"notice", LOG_NOTICE},
	{"info", LOG_INFO},
	{"debug", LOG_DEBUG},
	{"trace", LOG_TRACE},
	{NULL, -1}
};

static struct syslog_names facilitynames[] = {
	{"auth", LOG_AUTH},
#if defined(LOG_AUTHPRIV)
	{"authpriv", LOG_AUTHPRIV},
#endif
	{"cron", LOG_CRON},
	{"daemon", LOG_DAEMON},
#if defined(LOG_FTP)
	{"ftp", LOG_FTP},
#endif
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

static const char log_month_name[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static pthread_rwlock_t _formatlock;

void
qb_log_format_init(void)
{
	int32_t l;
	struct qb_log_target *t;
	enum qb_log_target_slot i;

	l = pthread_rwlock_init(&_formatlock, NULL);
	assert(l == 0);

	for (i = QB_LOG_TARGET_START; i < QB_LOG_TARGET_MAX; i++) {
		t = qb_log_target_get(i);
		t->format = strdup("[%p] %b");
	}
}

void
qb_log_format_fini(void)
{
	struct qb_log_target *t;
	enum qb_log_target_slot i;

	pthread_rwlock_destroy(&_formatlock);

	for (i = QB_LOG_TARGET_START; i < QB_LOG_TARGET_MAX; i++) {
		t = qb_log_target_get(i);
		free(t->format);
	}
}

void
qb_log_format_set(int32_t target, const char *format)
{
	char modified_format[256];
	struct qb_log_target *t = qb_log_target_get(target);

	pthread_rwlock_wrlock(&_formatlock);

	free(t->format);

	if (format) {
		qb_log_target_format_static(target, format, modified_format);
		t->format = strdup(modified_format);
	} else {
		t->format = strdup("[%p] %b");
	}
	assert(t->format != NULL);

	pthread_rwlock_unlock(&_formatlock);
}

/* Convert string "auth" to equivalent number "LOG_AUTH" etc. */
int32_t
qb_log_facility2int(const char *fname)
{
	int32_t i;

	if (fname == NULL) {
		return -EINVAL;
	}

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (strcmp(fname, facilitynames[i].c_name) == 0) {
			return facilitynames[i].c_val;
		}
	}
	return -EINVAL;
}

/* Convert number "LOG_AUTH" to equivalent string "auth" etc. */
const char *
qb_log_facility2str(int32_t fnum)
{
	int32_t i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (facilitynames[i].c_val == fnum) {
			return facilitynames[i].c_name;
		}
	}
	return NULL;
}

const char *
qb_log_priority2str(uint8_t priority)
{
	if (priority > LOG_TRACE) {
		return prioritynames[LOG_TRACE].c_name;
	}
	return prioritynames[priority].c_name;
}

void
qb_log_tags_stringify_fn_set(qb_log_tags_stringify_fn fn)
{
	_user_tags_stringify_fn = fn;
}

static int
_strcpy_cutoff(char *dest, const char *src, size_t cutoff, int ralign,
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
	if (ralign) {
		memset(dest, ' ', cutoff - len);
		memcpy(dest + cutoff - len, src, len);
	} else {
		memcpy(dest, src, len);
		memset(dest + len, ' ', cutoff - len);
	}

	dest[cutoff] = '\0';

	return cutoff;
}

/*
 * This function will do static formatting (for things that don't
 * change on each log message).
 *
 * %P PID
 * %N name passed into qb_log_init
 * %H hostname
 *
 * any number between % and character specify field length to pad or chop
 */
void
qb_log_target_format_static(int32_t target, const char * format,
			    char *output_buffer)
{
	char tmp_buf[255];
	unsigned int format_buffer_idx = 0;
	unsigned int output_buffer_idx = 0;
	size_t cutoff;
	uint32_t len;
	int ralign;
	int c;
	struct qb_log_target *t = qb_log_target_get(target);

	if (format == NULL) {
		return;
	}

	while ((c = format[format_buffer_idx])) {
		cutoff = 0;
		ralign = QB_FALSE;
		if (c != '%') {
			output_buffer[output_buffer_idx++] = c;
			format_buffer_idx++;
		} else {
			const char *p;
			unsigned int percent_buffer_idx = format_buffer_idx;

			format_buffer_idx += 1;
			if (format[format_buffer_idx] == '-') {
				ralign = QB_TRUE;
				format_buffer_idx += 1;
			}

			if (isdigit(format[format_buffer_idx])) {
				cutoff = atoi(&format[format_buffer_idx]);
			}
			while (isdigit(format[format_buffer_idx])) {
				format_buffer_idx += 1;
			}

			switch (format[format_buffer_idx]) {
			case 'P':
				snprintf(tmp_buf, 30, "%d", getpid());
				p = tmp_buf;
				break;

			case 'N':
				p = t->name;
				break;

			case 'H':
				if (gethostname(tmp_buf, sizeof(tmp_buf)) == 0) {
					tmp_buf[sizeof(tmp_buf) - 1] = '\0';
				} else {
					(void)strlcpy(tmp_buf, "localhost",
						      sizeof(tmp_buf));
				}
				p = tmp_buf;
				break;

			default:
				p = &format[percent_buffer_idx];
				cutoff = (format_buffer_idx - percent_buffer_idx + 1);
				ralign = QB_FALSE;
				break;
			}
			len = _strcpy_cutoff(output_buffer + output_buffer_idx,
					     p, cutoff, ralign,
					     (t->max_line_length -
					      output_buffer_idx));
			output_buffer_idx += len;
			format_buffer_idx += 1;
		}
		if (output_buffer_idx >= t->max_line_length - 1) {
			break;
		}
	}

	output_buffer[output_buffer_idx] = '\0';
}

/*
 * %n FUNCTION NAME
 * %f FILENAME
 * %l FILELINE
 * %p PRIORITY
 * %t TIMESTAMP
 * %T TIMESTAMP with milliseconds
 * %b BUFFER
 * %g SUBSYSTEM
 *
 * any number between % and character specify field length to pad or chop
 */
void
qb_log_target_format(int32_t target,
		     struct qb_log_callsite *cs,
		     struct timespec *the_ts,
		     const char *formatted_message, char *output_buffer)
{
	char tmp_buf[128];
	struct tm tm_res;
	time_t time_sec;
	unsigned int format_buffer_idx = 0;
	unsigned int output_buffer_idx = 0;
	size_t cutoff;
	uint32_t len;
	int ralign;
	int c;
	struct qb_log_target *t = qb_log_target_get(target);

	pthread_rwlock_rdlock(&_formatlock);
	if (t->format == NULL) {
		pthread_rwlock_unlock(&_formatlock);
		return;
	}

	while ((c = t->format[format_buffer_idx])) {
		cutoff = 0;
		ralign = QB_FALSE;
		if (c != '%') {
			output_buffer[output_buffer_idx++] = c;
			format_buffer_idx++;
		} else {
			const char *p;

			format_buffer_idx += 1;
			if (t->format[format_buffer_idx] == '-') {
				ralign = QB_TRUE;
				format_buffer_idx += 1;
			}

			if (isdigit(t->format[format_buffer_idx])) {
				cutoff = atoi(&t->format[format_buffer_idx]);
			}
			while (isdigit(t->format[format_buffer_idx])) {
				format_buffer_idx += 1;
			}

			switch (t->format[format_buffer_idx]) {
			case 'g':
				if (_user_tags_stringify_fn) {
					p = _user_tags_stringify_fn(cs->tags);
				} else {
					p = "";
				}
				break;

			case 'n':
				p = cs->function;
				break;

			case 'f':
#ifdef BUILDING_IN_PLACE
				p = cs->filename;
#else
				p = strrchr(cs->filename, '/');
				if (p == NULL) {
					p = cs->filename;
				} else {
					p++; /* move past the "/" */
				}
#endif /* BUILDING_IN_PLACE */
				break;

			case 'l':
#ifndef S_SPLINT_S
				snprintf(tmp_buf, 30, "%" PRIu32, cs->lineno);
#endif /* S_SPLINT_S */
				p = tmp_buf;
				break;

			case 't':
				time_sec = the_ts->tv_sec;
				(void)localtime_r(&time_sec, &tm_res);
				snprintf(tmp_buf, TIME_STRING_SIZE,
					 "%s %02d %02d:%02d:%02d",
					 log_month_name[tm_res.tm_mon],
					 tm_res.tm_mday, tm_res.tm_hour,
					 tm_res.tm_min, tm_res.tm_sec);
				p = tmp_buf;
				break;

			case 'T':
				time_sec = the_ts->tv_sec;
				(void)localtime_r(&time_sec, &tm_res);
				snprintf(tmp_buf, TIME_STRING_SIZE,
					 "%s %02d %02d:%02d:%02d.%03llu",
					 log_month_name[tm_res.tm_mon],
					 tm_res.tm_mday, tm_res.tm_hour,
					 tm_res.tm_min, tm_res.tm_sec,
					 the_ts->tv_nsec/QB_TIME_NS_IN_MSEC);
				p = tmp_buf;
				break;

			case 'b':
				p = formatted_message;
				break;

			case 'p':
				if (cs->priority > LOG_TRACE) {
					p = prioritynames[LOG_TRACE].c_name;
				} else {
					p = prioritynames[cs->priority].c_name;
				}
				break;

			default:
				p = "";
				break;
			}
			len = _strcpy_cutoff(output_buffer + output_buffer_idx,
					     p, cutoff, ralign,
					     (t->max_line_length -
					      output_buffer_idx));
			output_buffer_idx += len;
			format_buffer_idx += 1;
		}
		if (output_buffer_idx >= t->max_line_length - 1) {
			break;
		}
	}
	pthread_rwlock_unlock(&_formatlock);

	if (output_buffer[output_buffer_idx - 1] == '\n') {
		output_buffer[output_buffer_idx - 1] = '\0';
	} else {
		output_buffer[output_buffer_idx] = '\0';
	}

	/* Indicate truncation */
	if (t->ellipsis && output_buffer_idx >= t->max_line_length-1) {
		output_buffer[output_buffer_idx-3] = '.';
		output_buffer[output_buffer_idx-2] = '.';
		output_buffer[output_buffer_idx-1] = '.';
	}
}


/*
 * These wrappers around strl* functions just return the
 * number of characters written, not the number of characters
 * requested to be written.
 */
static size_t
my_strlcpy(char *dest, const char * src, size_t maxlen)
{
	size_t rc = strlcpy(dest, src, maxlen);
	/* maxlen includes NUL, so -1 */
	return QB_MIN(rc, maxlen-1);
}

static size_t
my_strlcat(char *dest, const char * src, size_t maxlen)
{
	size_t rc = strlcat(dest, src, maxlen);
	return QB_MIN(rc, maxlen-1);
}

size_t
qb_vsnprintf_serialize(char *serialize, size_t max_len,
		       const char *fmt, va_list ap)
{
	char *format;
	char *p;
	char *qb_xc;
	int type_long = QB_FALSE;
	int type_longlong = QB_FALSE;
	size_t sformat_length = 0;
	int sformat_precision = QB_FALSE;
	uint32_t location = my_strlcpy(serialize, fmt, max_len) + 1;

	/* Assume serialized output always wants extended information
	 * (@todo: add variant of this function that takes argument for whether
	 * to print extended information, and make this a macro with that
	 * argument set to QB_TRUE, so callers can honor extended setting)
	 */
	if ((qb_xc = strchr(serialize, QB_XC)) != NULL) {
		*qb_xc = *(qb_xc + 1)? '|' : '\0';
	}

	format = (char *)fmt;
	for (;;) {
		type_long = QB_FALSE;
		type_longlong = QB_FALSE;
		p = strchrnul((const char *)format, '%');
		if (*p == '\0') {
			break;
		}
		format = p + 1;
reprocess:
		switch (format[0]) {
		case '#': /* alternate form conversion, ignore */
		case '-': /* left adjust, ignore */
		case ' ': /* a space, ignore */
		case '+': /* a sign should be used, ignore */
		case '\'': /* group in thousands, ignore */
		case 'I': /* glibc-ism locale alternative, ignore */
                    format++;
                    goto reprocess;
		case '.': /* precision, ignore */
                    format++;
                    sformat_precision = QB_TRUE;
                    goto reprocess;
		case '0': /* field width, ignore */
		case '1': /* field width, ignore */
		case '2': /* field width, ignore */
		case '3': /* field width, ignore */
		case '4': /* field width, ignore */
		case '5': /* field width, ignore */
		case '6': /* field width, ignore */
		case '7': /* field width, ignore */
		case '8': /* field width, ignore */
		case '9': /* field width, ignore */
                        if (sformat_precision) {
                            sformat_length *= 10;
                            sformat_length += (format[0] - '0');
                        }
			format++;
			goto reprocess;
		case '*': /* variable field width, save */ {
			int arg_int = va_arg(ap, int);
			if (location + sizeof (int) > max_len) {
				return max_len;
			}
			memcpy(&serialize[location], &arg_int, sizeof (int));
			location += sizeof(int);
			format++;
			goto reprocess;
		}
		case 'l':
			format++;
			type_long = QB_TRUE;
			if (*format == 'l') {
				type_long = QB_FALSE;
				type_longlong = QB_TRUE;
				format++;
			}
			goto reprocess;
		case 'z':
			format++;
			if (sizeof(size_t) == sizeof(long long)) {
				type_longlong = QB_TRUE;
			} else {
				type_long = QB_TRUE;
			}
			goto reprocess;
		case 't':
			format++;
			if (sizeof(ptrdiff_t) == sizeof(long long)) {
				type_longlong = QB_TRUE;
			} else {
				type_long = QB_TRUE;
			}
			goto reprocess;
		case 'j':
			format++;
			if (sizeof(intmax_t) == sizeof(long long)) {
				type_longlong = QB_TRUE;
			} else {
				type_long = QB_TRUE;
			}
			goto reprocess;
		case 'd': /* int argument */
		case 'i': /* int argument */
		case 'o': /* unsigned int argument */
		case 'u':
		case 'x':
		case 'X':
			if (type_long) {
				long int arg_int;

				if (location + sizeof (long int) > max_len) {
					return max_len;
				}
				arg_int = va_arg(ap, long int);
				memcpy(&serialize[location], &arg_int,
				       sizeof(long int));
				location += sizeof(long int);
				format++;
				break;
			} else if (type_longlong) {
				long long int arg_int;

				if (location + sizeof (long long int) > max_len) {
					return max_len;
				}
				arg_int = va_arg(ap, long long int);
				memcpy(&serialize[location], &arg_int,
				       sizeof(long long int));
				location += sizeof(long long int);
				format++;
				break;
			} else {
				int arg_int;

				if (location + sizeof (int) > max_len) {
					return max_len;
				}
				arg_int = va_arg(ap, int);
				memcpy(&serialize[location], &arg_int,
				       sizeof(int));
				location += sizeof(int);
				format++;
				break;
			}
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
			{
			double arg_double;

			if (location + sizeof (double) > max_len) {
				return max_len;
			}
			arg_double = va_arg(ap, double);
			memcpy (&serialize[location], &arg_double, sizeof (double));
			location += sizeof(double);
			format++;
			break;
			}
		case 'c':
			{
			int arg_int;
			unsigned char arg_char;

			if (location + sizeof (unsigned char) > max_len) {
				return max_len;
			}
			/* va_arg only takes fully promoted types */
			arg_int = va_arg(ap, unsigned int);
			arg_char = (unsigned char)arg_int;
			memcpy (&serialize[location], &arg_char, sizeof (unsigned char));
			location += sizeof(unsigned char);
			break;
			}
		case 's':
			{
			char *arg_string;
			arg_string = va_arg(ap, char *);
			if (arg_string == NULL) {
				location += my_strlcpy(&serialize[location],
						   "(null)",
						   QB_MIN(strlen("(null)") + 1,
							  max_len - location));
			} else if (sformat_length) {
				location += my_strlcpy(&serialize[location],
						   arg_string,
						   QB_MIN(sformat_length + 1,
						   (max_len - location)));
			} else {
				location += my_strlcpy(&serialize[location],
						   arg_string,
						   QB_MIN(strlen(arg_string) + 1,
							  max_len - location));
			}
			location++;
			break;
			}
		case 'p':
			{
			ptrdiff_t arg_pointer = va_arg(ap, ptrdiff_t);
			if (location + sizeof (ptrdiff_t) > max_len) {
				return max_len;
			}
			memcpy(&serialize[location], &arg_pointer, sizeof(ptrdiff_t));
			location += sizeof(ptrdiff_t);
			break;
			}
		case '%':
			if (location + 1 > max_len) {
				return max_len;
			}
			serialize[location++] = '%';
                        sformat_length = 0;
                        sformat_precision = QB_FALSE;
			break;

		}
	}
	return (location);
}

#define MINI_FORMAT_STR_LEN 20

size_t
qb_vsnprintf_deserialize(char *string, size_t str_len, const char *buf)
{
	char *p;
	char *format;
	char fmt[MINI_FORMAT_STR_LEN];
	int fmt_pos;

	uint32_t location = 0;
	uint32_t data_pos = strlen(buf) + 1;
	int type_long = QB_FALSE;
	int type_longlong = QB_FALSE;
	int len;

	string[0] = '\0';
	format = (char *)buf;
	for (;;) {
		type_long = QB_FALSE;
		type_longlong = QB_FALSE;
		p = strchrnul((const char *)format, '%');
		if (*p == '\0') {
			return my_strlcat(string, format, str_len) + 1;
		}
		/* copy from current to the next % */
		len = p - format;
		memcpy(&string[location], format, len);
		location += len;
		format = p;

		/* start building up the format for snprintf */
		fmt_pos = 0;
		fmt[fmt_pos++] = *format;
		format++;
reprocess:
		switch (format[0]) {
		case '#': /* alternate form conversion, ignore */
		case '-': /* left adjust, ignore */
		case ' ': /* a space, ignore */
		case '+': /* a sign should be used, ignore */
		case '\'': /* group in thousands, ignore */
		case 'I': /* glibc-ism locale alternative, ignore */
		case '.': /* precision, ignore */
		case '0': /* field width, ignore */
		case '1': /* field width, ignore */
		case '2': /* field width, ignore */
		case '3': /* field width, ignore */
		case '4': /* field width, ignore */
		case '5': /* field width, ignore */
		case '6': /* field width, ignore */
		case '7': /* field width, ignore */
		case '8': /* field width, ignore */
		case '9': /* field width, ignore */
			fmt[fmt_pos++] = *format;
			format++;
			goto reprocess;

		case '*': {
			int arg_int;
			memcpy(&arg_int, &buf[data_pos], sizeof(int));
			data_pos += sizeof(int);
			fmt_pos += snprintf(&fmt[fmt_pos],
					   MINI_FORMAT_STR_LEN - fmt_pos,
					   "%d", arg_int);
			format++;
			goto reprocess;
		}
		case 'l':
			fmt[fmt_pos++] = *format;
			format++;
			type_long = QB_TRUE;
			if (*format == 'l') {
				type_long = QB_FALSE;
				type_longlong = QB_TRUE;
			}
			goto reprocess;
		case 'z':
			fmt[fmt_pos++] = *format;
			format++;
			if (sizeof(size_t) == sizeof(long long)) {
				type_long = QB_FALSE;
				type_longlong = QB_TRUE;
			} else {
				type_longlong = QB_FALSE;
				type_long = QB_TRUE;
			}
			goto reprocess;
		case 't':
			fmt[fmt_pos++] = *format;
			format++;
			if (sizeof(ptrdiff_t) == sizeof(long long)) {
				type_longlong = QB_TRUE;
			} else {
				type_long = QB_TRUE;
			}
			goto reprocess;
		case 'j':
			fmt[fmt_pos++] = *format;
			format++;
			if (sizeof(intmax_t) == sizeof(long long)) {
				type_longlong = QB_TRUE;
			} else {
				type_long = QB_TRUE;
			}
			goto reprocess;
		case 'd': /* int argument */
		case 'i': /* int argument */
		case 'o': /* unsigned int argument */
		case 'u':
		case 'x':
		case 'X':
			if (type_long) {
				long int arg_int;

				fmt[fmt_pos++] = *format;
				fmt[fmt_pos++] = '\0';
				memcpy(&arg_int, &buf[data_pos], sizeof(long int));
				location += snprintf(&string[location],
						     str_len - location,
						     fmt, arg_int);
				data_pos += sizeof(long int);
				format++;
				break;
			} else if (type_longlong) {
				long long int arg_int;

				fmt[fmt_pos++] = *format;
				fmt[fmt_pos++] = '\0';
				memcpy(&arg_int, &buf[data_pos], sizeof(long long int));
				location += snprintf(&string[location],
						     str_len - location,
						     fmt, arg_int);
				data_pos += sizeof(long long int);
				format++;
				break;
			} else {
				int arg_int;

				fmt[fmt_pos++] = *format;
				fmt[fmt_pos++] = '\0';
				memcpy(&arg_int, &buf[data_pos], sizeof(int));
				location += snprintf(&string[location],
						     str_len - location,
						     fmt, arg_int);
				data_pos += sizeof(int);
				format++;
				break;
			}
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
			{
			double arg_double;

			fmt[fmt_pos++] = *format;
			fmt[fmt_pos++] = '\0';
			memcpy(&arg_double, &buf[data_pos], sizeof(double));
			location += snprintf(&string[location],
					     str_len - location,
					     fmt, arg_double);
			data_pos += sizeof(double);
			format++;
			break;
			}
		case 'c':
			{
			unsigned char *arg_char;

			fmt[fmt_pos++] = *format;
			fmt[fmt_pos++] = '\0';
			arg_char = (unsigned char*)&buf[data_pos];
			location += snprintf(&string[location],
					     str_len - location,
					     fmt, *arg_char);
			data_pos += sizeof(unsigned char);
			format++;
			break;
			}
		case 's':
			{
			fmt[fmt_pos++] = *format;
			fmt[fmt_pos++] = '\0';
			len = snprintf(&string[location],
				       str_len - location,
				       fmt, &buf[data_pos]);
			location += len;
			/* don't use len as there might be a len modifier */
			data_pos += strlen(&buf[data_pos]) + 1;
			format++;
			break;
			}
		case 'p':
			{
			ptrdiff_t pt;
			memcpy(&pt, &buf[data_pos],
			       sizeof(ptrdiff_t));
			fmt[fmt_pos++] = *format;
			fmt[fmt_pos++] = '\0';
			location += snprintf(&string[location],
					     str_len - location,
					     fmt, pt);
			data_pos += sizeof(void*);
			format++;
			break;
			}
		case '%':
			string[location++] = '%';
			format++;
			break;

		}
	}
	return location;
}

