/*
 * Copyright (c) 2011-2015 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#include "os_base.h"
#include <pthread.h>

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

#ifdef USE_JOURNAL
#include <systemd/sd-journal.h>
#endif

#include "_syslog_override.h"

extern size_t qb_vsnprintf_serialize(char *serialize, size_t max_len, const char *fmt, va_list ap);
extern size_t qb_vsnprintf_deserialize(char *string, size_t strlen, const char *buf);


static void
format_this(char *out, const char *fmt, ...)
{
	char buf[QB_LOG_MAX_LEN];
	va_list ap;

	va_start(ap, fmt);
	qb_vsnprintf_serialize(buf, QB_LOG_MAX_LEN, fmt, ap);
	qb_vsnprintf_deserialize(out, QB_LOG_MAX_LEN, buf);
	va_end(ap);
}

static void
format_this_up_to(char *out, size_t max_len, const char *fmt, ...)
{
	char buf[QB_LOG_MAX_LEN];
	va_list ap;

	va_start(ap, fmt);
	qb_vsnprintf_serialize(buf, max_len, fmt, ap);
	qb_vsnprintf_deserialize(out, QB_LOG_MAX_LEN, buf);
	va_end(ap);
}

START_TEST(test_va_serialize)
{
	char buf[QB_LOG_MAX_LEN];
	char cmp_buf[QB_LOG_MAX_LEN];

	format_this(buf, "one line");
	ck_assert_str_eq(buf, "one line");

	format_this(buf, "p1:%p, p2:%p", format_this, buf);
	snprintf(cmp_buf, QB_LOG_MAX_LEN, "p1:%p, p2:%p", format_this, buf);
	ck_assert_str_eq(buf, cmp_buf);

	format_this(buf, "s1:%s, s2:%s", "Yes", "Never");
	ck_assert_str_eq(buf, "s1:Yes, s2:Never");

	format_this(buf, "s1:%s, s2:%s", "Yes", "Never");
	ck_assert_str_eq(buf, "s1:Yes, s2:Never");

	format_this(buf, "d1:%d, d2:%5i, d3:%04i", 23, 37, 84);
	ck_assert_str_eq(buf, "d1:23, d2:   37, d3:0084");

	format_this(buf, "f1:%.5f, f2:%.2f", 23.34109, 23.34109);
	ck_assert_str_eq(buf, "f1:23.34109, f2:23.34");

	format_this(buf, "%zd", (size_t)13140964);
	ck_assert_str_eq(buf, "13140964");
	format_this(buf, "%jd", (intmax_t)30627823);
	ck_assert_str_eq(buf, "30627823");
	format_this(buf, "%td", buf-cmp_buf);
	snprintf(cmp_buf, QB_LOG_MAX_LEN, "%td", buf-cmp_buf);
	ck_assert_str_eq(buf, cmp_buf);

	format_this(buf, ":%s:", "Hello, world!");
	ck_assert_str_eq(buf, ":Hello, world!:");
	format_this(buf, ":%15s:", "Hello, world!");
	ck_assert_str_eq(buf, ":  Hello, world!:");
	format_this(buf, ":%.10s:", "Hello, world!");
	ck_assert_str_eq(buf, ":Hello, wor:");
	format_this(buf, ":%-10s:", "Hello, world!");
	ck_assert_str_eq(buf, ":Hello, world!:");
	format_this(buf, ":%-15s:", "Hello, world!");
	ck_assert_str_eq(buf, ":Hello, world!  :");
	format_this(buf, ":%.15s:", "Hello, world!");
	ck_assert_str_eq(buf, ":Hello, world!:");
	format_this(buf, ":%15.10s:", "Hello, world!");
	ck_assert_str_eq(buf, ":     Hello, wor:");
	format_this(buf, ":%-15.10s:", "Hello, world!");
	ck_assert_str_eq(buf, ":Hello, wor     :");

	format_this(buf, ":%*d:", 8, 96);
	ck_assert_str_eq(buf, ":      96:");

	format_this_up_to(buf, 11, "123456789____");
	ck_assert_str_eq(buf, "123456789_");

	format_this(buf, "Client %s.%.9s wants to fence (%s) '%s' with device '%s'",
		    "bla", "foooooooooooooooooo",
		    "action", "target", "hoop");

	ck_assert_str_eq(buf,
			 "Client bla.foooooooo wants to fence (action) 'target' with device 'hoop'");

	format_this(buf, "Node %s now has process list: %.32x (was %.32x)",
		    "18builder", 2, 0);
	ck_assert_str_eq(buf, "Node 18builder now has process list: 00000000000000000000000000000002 (was 00000000000000000000000000000000)");


}
END_TEST

START_TEST(test_log_stupid_inputs)
{
	int32_t rc;

	/* shouldn't crash with out an init() */
	qb_log_fini();

	/* not init'ed */
	rc = qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "bla", LOG_TRACE);
	ck_assert_int_eq(rc, -EINVAL);

	rc = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 2000);
	ck_assert_int_eq(rc, -EINVAL);

	qb_log(LOG_INFO, "not init'd");

	qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
				    __LINE__, 0, "also not init'd");

	qb_log_init("test", LOG_USER, LOG_DEBUG);

	/* non-opened log file */
	rc = qb_log_filter_ctl(21, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "bla", LOG_TRACE);
	ck_assert_int_eq(rc, -EBADF);

	rc = qb_log_ctl(21, QB_LOG_CONF_PRIORITY_BUMP, -1);
	ck_assert_int_eq(rc, -EBADF);

	/* target < 0 or >= 32 */
	rc = qb_log_filter_ctl(41, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "bla", LOG_TRACE);
	ck_assert_int_eq(rc, -EBADF);

	rc = qb_log_ctl(-1, QB_LOG_CONF_PRIORITY_BUMP, -1);
	ck_assert_int_eq(rc, -EBADF);

	/* crap values to filter_ctl() */
	rc = qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, NULL, LOG_INFO);
	ck_assert_int_eq(rc, -EINVAL);
	rc = qb_log_filter_ctl(QB_LOG_SYSLOG, 56,
			       QB_LOG_FILTER_FILE, "boja", LOG_INFO);
	ck_assert_int_eq(rc, -EINVAL);

	/* crap values to ctl() */
	rc = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, -2000);
	ck_assert_int_eq(rc, -EINVAL);
	rc = qb_log_ctl(QB_LOG_BLACKBOX, 67, 2000);
	ck_assert_int_eq(rc, -EINVAL);
	rc = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_SIZE, 2000);
	ck_assert_int_eq(rc, -ENOSYS);

}
END_TEST

static char test_buf[4097];
static uint8_t test_priority;
static int32_t num_msgs;
static size_t last_length;

/*
 * to test that we get what we expect.
 */
static void
_test_logger(int32_t t,
	     struct qb_log_callsite *cs,
	     struct timespec *timestamp, const char *msg)
{
	test_buf[0] = '\0';
	qb_log_target_format(t, cs, timestamp, msg, test_buf);
	test_priority = cs->priority;

	num_msgs++;
}

static void
_test_length_logger(int32_t t,
	     struct qb_log_callsite *cs,
	     struct timespec *timestamp, const char *msg)
{
	strcpy(test_buf, msg);
	qb_log_target_format(t, cs, timestamp, msg, test_buf);
	test_priority = cs->priority;

	num_msgs++;
	last_length = strlen(msg);
}

static void log_also(void)
{
	qb_log(LOG_INFO, "yes please");
}

static void log_and_this_too(void)
{
	qb_log(LOG_INFO, "this too please");
}

static void log_it_please(void)
{
	qb_enter();
	qb_log(LOG_TRACE, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_DEBUG, "A:%d B:%d C:%d", 1, 2, 3);
	errno = EEXIST;
	qb_perror(LOG_WARNING, "bogus error");
	errno = 0;
	qb_log(LOG_INFO, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_NOTICE, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_WARNING, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_ERR, "A:%d B:%d C:%d", 1, 2, 3);
	qb_leave();
}


static int32_t _cust_t = -1;
static void
m_filter(struct qb_log_callsite *cs)
{
	if ((cs->priority >= LOG_ALERT &&
	     cs->priority <= LOG_INFO) ||
	    cs->tags > 0) {
		qb_bit_set(cs->targets, _cust_t);
	} else {
		qb_bit_clear(cs->targets, _cust_t);
	}
}


START_TEST(test_log_filter_fn)
{
	int32_t rc;

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	_cust_t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	_ck_assert_int(_cust_t, >, QB_LOG_BLACKBOX);
	rc = qb_log_ctl(_cust_t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	/*
	 * test the custom filter function.
	 * make sure qb_log, and qb_log_from_external_source are filtered.
	 */
	qb_log_filter_fn_set(m_filter);
	num_msgs = 0;

	qb_log(LOG_NOTICE, "qb_log_filter_fn_set good");
	qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
				    __LINE__, 0, "qb_log_filter_fn_set good");
	qb_log(LOG_TRACE, "qb_log_filter_fn_set bad");
	qb_log_from_external_source(__func__, __FILE__, "%s", LOG_DEBUG,
				    __LINE__, 44, "qb_log_filter_fn_set woot");
	qb_log_from_external_source(__func__, __FILE__, "%s", LOG_DEBUG,
				    __LINE__, 0, "qb_log_filter_fn_set bad");

	ck_assert_int_eq(num_msgs, 3);
}
END_TEST

START_TEST(test_file_logging)
{
	struct stat st;
	int rc, lf;

	unlink("test1.log");
	unlink("test2.log");

	qb_log_init("test", LOG_USER, LOG_DEBUG);
	lf = qb_log_file_open("test1.log");
	rc = qb_log_filter_ctl(lf, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE,
			       __FILE__, LOG_DEBUG);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(lf, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	qb_log(LOG_INFO, "write to file 1");
	qb_log(LOG_INFO, "write to file 1 again");

	rc = stat("test1.log", &st);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_ge(st.st_size, 32);

	/* Test reopen with NULL arg */
	rc = qb_log_file_reopen(lf, NULL);
	ck_assert_int_eq(rc, 0);
	qb_log(LOG_INFO, "write to file 1 and again");
	qb_log(LOG_INFO, "write to file 1 yet again");
	rc = stat("test1.log", &st);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_ge(st.st_size, 64);

	/* Test reopen with new file */
	rc = qb_log_file_reopen(lf, "test2.log");
	ck_assert_int_eq(rc, 0);

	qb_log(LOG_INFO, "write to file 2");
	qb_log(LOG_INFO, "write to file 2 again");

	rc = stat("test2.log", &st);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_ge(st.st_size, 32);

	unlink("test1.log");
	unlink("test2.log");
}
END_TEST

START_TEST(test_timestamps)
{
	int32_t t;
	int32_t rc;
	int a,b,c,d;

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "*", LOG_INFO);
	ck_assert_int_eq(rc, 0);

	/* normal timestamp */
	qb_log_format_set(t, "%t %b");
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	qb_log(LOG_INFO, "The time now is (see left)");
	rc = sscanf(test_buf+7, "%d:%d:%d.%d", &a, &b, &c, &d);
	ck_assert_int_eq(rc, 3);

	/* millisecond timestamp */
	qb_log_format_set(t, "%T %b");
	qb_log(LOG_INFO, "The time now is precisely (see left)");
	rc = sscanf(test_buf+7, "%d:%d:%d.%d", &a, &b, &c, &d);
	ck_assert_int_eq(rc, 4);
}
END_TEST


START_TEST(test_line_length)
{
	int32_t t;
	int32_t rc;
	int i;
	char bigbuf[4097];

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_length_logger, NULL, NULL, NULL);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FORMAT, "*", LOG_WARNING);
	ck_assert_int_eq(rc, 0);
	qb_log_format_set(t, "[%p] %b");
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(t, QB_LOG_CONF_MAX_LINE_LEN, 32);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(t, QB_LOG_CONF_ELLIPSIS, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	/* captures last log */
	memset(test_buf, 0, sizeof(test_buf));
	test_priority = 0;
	num_msgs = 0;

	qb_log(LOG_ERR, "Short message");
	qb_log(LOG_ERR, "This is a longer message 123456789012345678901234567890");
	qb_log(LOG_ERR, "Long message with parameters %d %s", 1234, "Oh yes it is");

	ck_assert_int_eq(num_msgs, 3);
	ck_assert_int_eq(last_length, 31);

	ck_assert_str_eq(test_buf+28, "...");

	rc = qb_log_ctl(t, QB_LOG_CONF_ELLIPSIS, QB_FALSE);
	ck_assert_int_eq(rc, 0);

	qb_log(LOG_ERR, "Long message with parameters %d %s", 1234, "Oh yes it is");
	ck_assert_str_ne(test_buf+28, "...");

	/* Long lines */
	num_msgs = 0;
	rc = qb_log_ctl(t, QB_LOG_CONF_MAX_LINE_LEN, 4096);
	ck_assert_int_eq(rc, 0);

	for (i=0; i<4096; i++) {
		bigbuf[i] = '0'+(i%10);
	}
	bigbuf[4096] = '\0';
	qb_log(LOG_ERR, "%s", bigbuf);
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_int_eq(last_length, 4095);
}
END_TEST

START_TEST(test_log_basic)
{
	int32_t t;
	int32_t rc;

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FORMAT, "Angus", LOG_WARNING);
	ck_assert_int_eq(rc, 0);
	qb_log_format_set(t, "%b");
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	/* captures last log */
	memset(test_buf, 0, sizeof(test_buf));
	test_priority = 0;
	num_msgs = 0;

	/*
	 * test filtering by format
	 */
	qb_log(LOG_INFO, "Hello Angus, how are you?");
	qb_log(LOG_WARNING, "Hello Steven, how are you?");
	qb_log(LOG_ERR, "Hello Andrew, how are you?");
	qb_log(LOG_ERR, "Hello Angus, how are you?");
	qb_log(LOG_EMERG, "Hello Anna, how are you?");
	ck_assert_int_eq(test_priority, LOG_ERR);
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_str_eq(test_buf, "Hello Angus, how are you?");


	/*
	 * test filtering by file regex
 	 */
	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FORMAT, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE_REGEX, "^fakefile*", LOG_DEBUG);

	num_msgs = 0;
	qb_log_from_external_source(__func__, "fakefile_logging", "%s bla", LOG_INFO,
				    56, 0, "filename/lineno");
	qb_log_from_external_source(__func__, "do_not_log_fakefile_logging", "%s bla", LOG_INFO,
				    56, 0, "filename/lineno");
	ck_assert_int_eq(num_msgs, 1);

	/*
	 * test filtering by format regex
 	 */
	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FORMAT, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FORMAT_REGEX, "^one", LOG_WARNING);

	num_msgs = 0;
	qb_log(LOG_INFO, "one two three");
	qb_log(LOG_ERR, "testing one two three");
	qb_log(LOG_WARNING, "one two three");
	qb_log(LOG_ERR, "one two three");
	qb_log(LOG_EMERG, "one two three");
	ck_assert_int_eq(num_msgs, 3);

	/*
	 * test filtering by function and regex
	 */
	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION_REGEX, "^log_.*please", LOG_WARNING);

	num_msgs = 0;
	qb_log(LOG_ERR, "try if you: log_it_please()");
	log_it_please();
	ck_assert_int_eq(num_msgs, 3);

	qb_log_filter_ctl(t, QB_LOG_FILTER_REMOVE,
			  QB_LOG_FILTER_FUNCTION_REGEX, "log_it_please", LOG_WARNING);

	/*
	 * test filtering by function
	 */
	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION, "log_it_please", LOG_WARNING);

	num_msgs = 0;
	qb_log(LOG_ERR, "try if you: log_it_please()");
	log_it_please();
	ck_assert_int_eq(num_msgs, 3);

	qb_log_filter_ctl(t, QB_LOG_FILTER_REMOVE,
			  QB_LOG_FILTER_FUNCTION, "log_it_please", LOG_WARNING);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION, __func__, LOG_DEBUG);

	num_msgs = 0;
	log_it_please();
	ck_assert_int_eq(num_msgs, 0);
	qb_log(LOG_DEBUG, "try if you: log_it_please()");
	ck_assert_int_eq(num_msgs, 1);

	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION,
			  "log_also,log_and_this_too",
			  LOG_DEBUG);
	num_msgs = 0;
	log_also();
	log_and_this_too();
	ck_assert_int_eq(num_msgs, 2);

	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "fakefile.c,"__FILE__",otherfakefile", LOG_DEBUG);
	/*
	 * make sure we can pass in a null filename or function name.
	 */
	qb_log_from_external_source(__func__, NULL, "%s", LOG_INFO,
				    __LINE__, 0, "null filename");
	qb_log_from_external_source(NULL, __FILE__, "%s", LOG_INFO,
				    __LINE__, 0, "null function");

	/* check same file/lineno logs with different formats work
	 */
	num_msgs = 0;
	qb_log_from_external_source(__func__, __FILE__, "%s bla", LOG_INFO,
				    56, 0, "filename/lineno");
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_str_eq(test_buf, "filename/lineno bla");

	num_msgs = 0;
	qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
				    56, 0, "same filename/lineno");
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_str_eq(test_buf, "same filename/lineno");

	/* check filtering works on same file/lineno but different
	 * log level.
	 */
	qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, __FILE__, LOG_INFO);

	num_msgs = 0;
	qb_log_from_external_source(__func__, __FILE__,
				    "same filename/lineno, this level %d",
				    LOG_INFO, 56, 0, LOG_INFO);
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_str_eq(test_buf, "same filename/lineno, this level 6");

	num_msgs = 0;
	qb_log_from_external_source(__func__, __FILE__,
				    "same filename/lineno, this level %d",
				    LOG_DEBUG, 56, 0, LOG_DEBUG);
	ck_assert_int_eq(num_msgs, 0);
}
END_TEST

static const char *_test_tags_stringify(uint32_t tags)
{
	if (tags == 1) {
		return "ONE";
	} else if (tags == 8) {
		return "ATE";
	} else {
		return "ANY";
	}
}

START_TEST(test_log_format)
{
	int32_t t;
	/* following size/length related equation holds in the context of use:
	   strlen(cmp_str) = strlen(host_str) + X; X ~ 20 < sizeof(host_str) */
	char host_str[256];
	char cmp_str[2 * sizeof(host_str)];

	qb_log_init("test", LOG_USER, LOG_DEBUG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);

	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_format_set(t, "%p %f %b");

	qb_log(LOG_DEBUG, "Angus");
	ck_assert_str_eq(test_buf, "debug check_log.c Angus");
	qb_log(LOG_INFO, "Angus");
	ck_assert_str_eq(test_buf, "info check_log.c Angus");
	qb_log(LOG_NOTICE, "Angus");
	ck_assert_str_eq(test_buf, "notice check_log.c Angus");
	qb_log(LOG_WARNING, "Angus");
	ck_assert_str_eq(test_buf, "warning check_log.c Angus");
	qb_log(LOG_ERR, "Angus");
	ck_assert_str_eq(test_buf, "error check_log.c Angus");
	qb_log(LOG_CRIT, "Angus");
	ck_assert_str_eq(test_buf, "crit check_log.c Angus");
	qb_log(LOG_ALERT, "Angus");
	ck_assert_str_eq(test_buf, "alert check_log.c Angus");
	qb_log(LOG_EMERG, "Angus");
	ck_assert_str_eq(test_buf, "emerg check_log.c Angus");

	qb_log_tags_stringify_fn_set(_test_tags_stringify);
	qb_log_format_set(t, "%g %b");

	qb_logt(LOG_INFO, 0, "Angus");
	ck_assert_str_eq(test_buf, "ANY Angus");
	qb_logt(LOG_INFO, 1, "Angus");
	ck_assert_str_eq(test_buf, "ONE Angus");
	qb_logt(LOG_INFO, 5, "Angus");
	ck_assert_str_eq(test_buf, "ANY Angus");
	qb_logt(LOG_INFO, 8, "Angus");
	ck_assert_str_eq(test_buf, "ATE Angus");

	qb_log_format_set(t, "%-15f %b");
	qb_log(LOG_WARNING, "Andrew");
	ck_assert_str_eq(test_buf, "    check_log.c Andrew");

	qb_log_tags_stringify_fn_set(NULL);

	gethostname(host_str, sizeof(host_str));
	host_str[sizeof(host_str) - 1] = '\0';

	qb_log_format_set(t, "%P %H %N %b");
	qb_log(LOG_INFO, "Angus");
	snprintf(cmp_str, sizeof(cmp_str), "%d %s test Angus", getpid(),
		 host_str);
	ck_assert_str_eq(test_buf, cmp_str);

	qb_log_format_set(t, "%3N %H %P %b");
	qb_log(LOG_INFO, "Angus");
	snprintf(cmp_str, sizeof(cmp_str), "tes %s %d Angus", host_str,
		 getpid());
	ck_assert_str_eq(test_buf, cmp_str);
}
END_TEST

START_TEST(test_log_enable)
{
	int32_t t;
	int32_t state;

	qb_log_init("test", LOG_USER, LOG_DEBUG);
	state = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_STATE_GET, 0);
	ck_assert_int_eq(state, QB_LOG_STATE_ENABLED);
	state = qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_STATE_GET, 0);
	ck_assert_int_eq(state, QB_LOG_STATE_DISABLED);
	state = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_STATE_GET, 0);
	ck_assert_int_eq(state, QB_LOG_STATE_DISABLED);

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	state = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_STATE_GET, 0);
	ck_assert_int_eq(state, QB_LOG_STATE_DISABLED);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);

	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_format_set(t, "%b");

	qb_log(LOG_DEBUG, "Hello");
	ck_assert_str_eq(test_buf, "Hello");

	num_msgs = 0;
	qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log(LOG_DEBUG, "Goodbye");
	ck_assert_int_eq(num_msgs, 0);
	qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log(LOG_DEBUG, "Hello again");
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_str_eq(test_buf, "Hello again");
}
END_TEST

#define ITERATIONS 100000
static void *thr_send_logs_2(void *ctx)
{
	int32_t i;
	printf("%s\n", __func__);

	for (i = 0; i < ITERATIONS; i++) {
		qb_log(LOG_INFO, "bla bla");
		qb_log(LOG_INFO, "blue blue");
		qb_log(LOG_INFO, "bra bra");
		qb_log(LOG_INFO, "bro bro");
		qb_log(LOG_INFO, "brown brown");
		qb_log(LOG_INFO, "booo booo");
		qb_log(LOG_INFO, "bogus bogus");
		qb_log(LOG_INFO, "bungu bungu");
	}
	return (NULL);
}

static void *thr_send_logs_1(void *ctx)
{
	int32_t i;

	printf("%s\n", __func__);
	for (i = 0; i < ITERATIONS; i++) {
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "foo soup");
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "fungus soup");
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "fruity soup");
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "free soup");
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "frot soup");
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "fresh soup");
		qb_log_from_external_source(__func__, __FILE__, "%s", LOG_INFO,
					    __LINE__, 0, "fattening soup");

	}
	return (NULL);
}

#define THREADS 4
START_TEST(test_log_threads)
{
	pthread_t threads[THREADS];
	pthread_attr_t thread_attr[THREADS];
	int32_t i;
	int32_t rc;
	int32_t lf;
	void *retval;

	qb_log_init("test", LOG_USER, LOG_DEBUG);
	lf = qb_log_file_open("threads.log");
	rc = qb_log_filter_ctl(lf, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE,
					   __FILE__, LOG_DEBUG);
	ck_assert_int_eq(rc, 0);
	qb_log_format_set(lf, "[%p] [%l] %b");
	rc = qb_log_ctl(lf, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	ck_assert_int_eq(rc, 0);

	for (i = 0; i < THREADS/2; i++) {
		pthread_attr_init(&thread_attr[i]);

		pthread_attr_setdetachstate(&thread_attr[i],
					    PTHREAD_CREATE_JOINABLE);
		pthread_create(&threads[i], &thread_attr[i],
			       thr_send_logs_1, NULL);
	}
	for (i = THREADS/2; i < THREADS; i++) {
		pthread_attr_init(&thread_attr[i]);

		pthread_attr_setdetachstate(&thread_attr[i],
					    PTHREAD_CREATE_JOINABLE);
		pthread_create(&threads[i], &thread_attr[i],
			       thr_send_logs_2, NULL);
	}
	for (i = 0; i < THREADS; i++) {
		pthread_join(threads[i], &retval);
	}

}
END_TEST

START_TEST(test_log_long_msg)
{
	int lpc;
	int rc;
	int i, max = 1000;
	char *buffer = calloc(1, max);

	qb_log_init("test", LOG_USER, LOG_DEBUG);
	rc = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 1024);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	ck_assert_int_eq(rc, 0);

	for (lpc = 500; lpc < max; lpc++) {
		lpc++;
		for(i = 0; i < max; i++) {
			buffer[i] = 'a' + (i % 10);
		}
		buffer[lpc%600] = 0;
		qb_log(LOG_INFO, "Message %d %d - %s", lpc, lpc%600, buffer);
	}

        qb_log_blackbox_write_to_file("blackbox.dump");
        qb_log_blackbox_print_from_file("blackbox.dump");
	unlink("blackbox.dump");
	qb_log_fini();
}
END_TEST

START_TEST(test_threaded_logging)
{
	int32_t t;
	int32_t rc;

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "*", LOG_INFO);
	ck_assert_int_eq(rc, 0);
	qb_log_format_set(t, "%b");
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(t, QB_LOG_CONF_THREADED, QB_TRUE);
	ck_assert_int_eq(rc, 0);
	qb_log_thread_start();

	memset(test_buf, 0, sizeof(test_buf));
	test_priority = 0;
	num_msgs = 0;

	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);
	qb_log(LOG_INFO, "Yoda how old are you? - %d", __LINE__);

	qb_log_fini();

	ck_assert_int_eq(num_msgs, 10);
}
END_TEST

#ifdef HAVE_PTHREAD_SETSCHEDPARAM
START_TEST(test_threaded_logging_bad_sched_params)
{
	int32_t t;
	int32_t rc;

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "*", LOG_INFO);
	ck_assert_int_eq(rc, 0);
	qb_log_format_set(t, "%b");
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(t, QB_LOG_CONF_THREADED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

#if defined(SCHED_RR)
#define QB_SCHED SCHED_RR
#elif defined(SCHED_FIFO)
#define QB_SCHED SCHED_FIFO
#else
#define QB_SCHED (-1)
#endif
	rc = qb_log_thread_priority_set(QB_SCHED, -1);
	ck_assert_int_eq(rc, 0);

	rc = qb_log_thread_start();
	ck_assert_int_ne(rc, 0);
	qb_log_fini();
}
END_TEST
#endif

START_TEST(test_extended_information)
{
	int32_t t;
	int32_t rc;
	int extended;

	qb_log_init("test", LOG_USER, LOG_DEBUG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	_ck_assert_int(t, >, QB_LOG_STDOUT);
	qb_log_format_set(t, "%b");
	rc = qb_log_filter_fn_set(NULL);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL, QB_LOG_FILTER_FILE,
			       "*", LOG_TRACE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_CLEAR_ALL, QB_LOG_FILTER_FORMAT,
			       "*", LOG_TRACE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FORMAT,
			       "*", LOG_TRACE);
	ck_assert_int_eq(rc, 0);
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	for (extended = QB_FALSE; extended <= QB_TRUE; ++extended) {
		rc = qb_log_ctl(t, QB_LOG_CONF_EXTENDED, extended);
		ck_assert_int_eq(rc, 0);

		num_msgs = 0;

		memset(test_buf, 0, sizeof(test_buf));
		qb_log(LOG_ERR, "message with no extended information");
		ck_assert_str_eq(test_buf, "message with no extended information");

		memset(test_buf, 0, sizeof(test_buf));
		qb_log(LOG_ERR, "message with empty extended information "QB_XS);
		ck_assert_str_eq(test_buf, "message with empty extended information ");

		memset(test_buf, 0, sizeof(test_buf));
		qb_log(LOG_ERR, QB_XS" message with only extended information");
		ck_assert_str_eq(test_buf, extended?
				 "| message with only extended information" : "");

		memset(test_buf, 0, sizeof(test_buf));
		qb_log(LOG_ERR, "message with extended information "QB_XS" (namely this)");
		ck_assert_str_eq(test_buf, extended?
				 "message with extended information | (namely this)"
				 : "message with extended information ");

		ck_assert_int_eq(num_msgs, (extended? 4 : 3));
	}
	qb_log_fini();
}
END_TEST

static const char *tagtest_stringify_tag(uint32_t tag)
{
	static char buf[32];
	sprintf(buf, "%5" PRIu32, tag);
	return buf;
}

START_TEST(test_zero_tags)
{
	int32_t rc;
	int32_t t;

	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);

	t = qb_log_custom_open(_test_logger, NULL, NULL, NULL);
	rc = qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			       QB_LOG_FILTER_FILE, "*", LOG_INFO);
	ck_assert_int_eq(rc, 0);

	qb_log_format_set(t, "[%g] %b");
	qb_log_tags_stringify_fn_set(tagtest_stringify_tag);
	rc = qb_log_ctl(t, QB_LOG_CONF_ENABLED, QB_TRUE);
	ck_assert_int_eq(rc, 0);

	qb_log_filter_ctl(t, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);

	qb_log_from_external_source("function", "filename", "%s: %d", LOG_DEBUG, 356, 2, "testlog", 2);
	ck_assert_str_eq(test_buf, "[    2] testlog: 2");

	qb_log_from_external_source("function", "filename", "%s: %d", LOG_DEBUG, 356, 0, "testlog", 0);
	ck_assert_str_eq(test_buf, "[    2] testlog: 0");

	qb_log_fini();


}
END_TEST

#ifdef USE_JOURNAL
START_TEST(test_journal)
{
	int rc;
	const char *msg;
	size_t len;
	pid_t log_pid;
	sd_journal *jnl;
	int count = 0;
	const char *msgid="f77379a8490b408bbe5f6940505a777b";

	qb_log_init("check_log", LOG_USER, LOG_DEBUG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_TRUE);
	rc = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_USE_JOURNAL, 1);
	ck_assert_int_eq(rc, 0);
	qb_log2(msgid, LOG_ERR, "Test message 1 from libqb");

	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
	rc = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_USE_JOURNAL, 1);
	ck_assert_int_eq(rc, -EINVAL);
	sleep(1);

	/* Check it reached the journal */
	rc = sd_journal_open(&jnl, 0);
	ck_assert_int_eq(rc, 0);
	rc = sd_journal_seek_tail(jnl);
	ck_assert_int_eq(rc, 0);
	SD_JOURNAL_FOREACH_BACKWARDS(jnl) {
	    rc = sd_journal_get_data(jnl, "_PID", (const void **)&msg, &len);
	    ck_assert_int_eq(rc, 0);
	    sscanf(msg, "_PID=%d", &log_pid);
	    fprintf(stderr, "PID message = '%s' - pid = %d (pid=%d, parent=%d)\n", msg, log_pid, getpid(), getppid());
	    if (log_pid == getpid()) {
	        rc = sd_journal_get_data(jnl, "MESSAGE", (const void **)&msg, &len);
		ck_assert_int_eq(rc, 0);
	        rc = sd_journal_get_data(jnl, "MESSAGE_ID", (const void **)&msg, &len);
		ck_assert_int_eq(rc, 0);
		ck_assert_str_eq(msg+11, msgid);
		break;
	    }
	    if (++count > 20) {
		    break;
            }
        }
	sd_journal_close(jnl);
	ck_assert_int_lt(count, 20);
}
END_TEST
#else
START_TEST(test_syslog)
{
	int rc;

	qb_log_init("flip", LOG_USER, LOG_INFO);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_TRUE);

	qb_log(LOG_ERR, "first as flip");
	ck_assert_int_eq(_syslog_opened, 1);
	ck_assert_str_eq(_syslog_ident, "flip");

	qb_log_ctl2(QB_LOG_SYSLOG, QB_LOG_CONF_IDENT, QB_LOG_CTL2_S("flop"));
	qb_log(LOG_ERR, "second as flop");
	ck_assert_str_eq(_syslog_ident, "flop");

	/* This test only runs if USE_JOURNAL is undefined, so should always fail */
	rc = qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_USE_JOURNAL, 1);
	ck_assert_int_eq(rc, -EOPNOTSUPP);

	qb_log_fini();
}
END_TEST
#endif

static Suite *
log_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("logging");

	add_tcase(s, tc, test_va_serialize);
	add_tcase(s, tc, test_log_stupid_inputs);
	add_tcase(s, tc, test_log_basic);
	add_tcase(s, tc, test_log_format);
	add_tcase(s, tc, test_log_enable);
	add_tcase(s, tc, test_log_threads, 360);
	add_tcase(s, tc, test_log_long_msg);
	add_tcase(s, tc, test_log_filter_fn);
	add_tcase(s, tc, test_threaded_logging);
	add_tcase(s, tc, test_line_length);
	add_tcase(s, tc, test_file_logging);
#ifdef HAVE_PTHREAD_SETSCHEDPARAM
	add_tcase(s, tc, test_threaded_logging_bad_sched_params);
#endif
	add_tcase(s, tc, test_timestamps);
	add_tcase(s, tc, test_extended_information);
	add_tcase(s, tc, test_zero_tags);
/*
 * You can still use syslog and journal in a normal application,
 * but the syslog_override code doesn't work when -lsystemd is present
 */
#ifdef USE_JOURNAL
        add_tcase(s, tc, test_journal);
#else
	add_tcase(s, tc, test_syslog);
#endif

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;

	Suite *s = log_suite();
	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
