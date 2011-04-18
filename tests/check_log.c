/*
 * Copyright (c) 2011 Red Hat, Inc.
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
#include <check.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

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
			       QB_LOG_FILTER_FILE, "bla", 45);
	ck_assert_int_eq(rc, -EINVAL);
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

#define TEST_BUF_SIZE 1024
static char test_buf[TEST_BUF_SIZE];
static int32_t test_priority;
static int32_t num_msgs;

/*
 * to test that we get what we expect.
 */
void syslog(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(test_buf, TEST_BUF_SIZE, format, ap);
	va_end(ap);
	test_priority = priority;
	num_msgs++;
}

static void log_it_please(void)
{
	qb_log(LOG_TRACE, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_DEBUG, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_INFO, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_NOTICE, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_WARNING, "A:%d B:%d C:%d", 1, 2, 3);
	qb_log(LOG_ERR, "A:%d B:%d C:%d", 1, 2, 3);
}

START_TEST(test_log_basic)
{
	qb_log_init("test", LOG_USER, LOG_EMERG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_EMERG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FORMAT, "Angus", LOG_WARNING);
	qb_log_format_set(QB_LOG_SYSLOG, "%b");
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_TRUE);

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
	 * test filtering by function
	 */
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION, "log_it_please", LOG_WARNING);

	num_msgs = 0;
	qb_log(LOG_ERR, "try if you: log_it_please()");
	log_it_please();
	ck_assert_int_eq(num_msgs, 2);

	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_REMOVE,
			  QB_LOG_FILTER_FUNCTION, "log_it_please", LOG_WARNING);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION, __func__, LOG_DEBUG);

	num_msgs = 0;
	log_it_please();
	ck_assert_int_eq(num_msgs, 0);
	qb_log(LOG_DEBUG, "try if you: log_it_please()");
	ck_assert_int_eq(num_msgs, 1);
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
	qb_log_init("test", LOG_USER, LOG_DEBUG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_format_set(QB_LOG_SYSLOG, "%p %f %b");

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
	qb_log_format_set(QB_LOG_SYSLOG, "%g %b");

	qb_logt(LOG_INFO, 0, "Angus");
	ck_assert_str_eq(test_buf, "ANY Angus");
	qb_logt(LOG_INFO, 1, "Angus");
	ck_assert_str_eq(test_buf, "ONE Angus");
	qb_logt(LOG_INFO, 5, "Angus");
	ck_assert_str_eq(test_buf, "ANY Angus");
	qb_logt(LOG_INFO, 8, "Angus");
	ck_assert_str_eq(test_buf, "ATE Angus");
}
END_TEST

START_TEST(test_log_enable)
{
	qb_log_init("test", LOG_USER, LOG_DEBUG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_format_set(QB_LOG_SYSLOG, "%b");

	/* enabled by default */
	qb_log(LOG_DEBUG, "Hello");
	ck_assert_str_eq(test_buf, "Hello");

	num_msgs = 0;
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log(LOG_DEBUG, "Goodbye");
	ck_assert_int_eq(num_msgs, 0);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log(LOG_DEBUG, "Hello again");
	ck_assert_int_eq(num_msgs, 1);
	ck_assert_str_eq(test_buf, "Hello again");
}
END_TEST

START_TEST(test_log_bump)
{
	qb_log_init("test", LOG_USER, LOG_DEBUG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_format_set(QB_LOG_SYSLOG, "%b");

	qb_log(LOG_DEBUG, "Hello");
	ck_assert_int_eq(test_priority, LOG_DEBUG);
	qb_log(LOG_INFO, "Hello");
	ck_assert_int_eq(test_priority, LOG_INFO);
	qb_log(LOG_CRIT, "Hello");
	ck_assert_int_eq(test_priority, LOG_CRIT);

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, -1);
	qb_log(LOG_DEBUG, "Hello");
	ck_assert_int_eq(test_priority, LOG_INFO);

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, -2);
	qb_log(LOG_DEBUG, "Hello");
	ck_assert_int_eq(test_priority, LOG_NOTICE);

	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, 0);
	qb_log(LOG_DEBUG, "Hello");
	ck_assert_int_eq(test_priority, LOG_DEBUG);
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
	qb_log_init("test", LOG_USER, LOG_DEBUG);
	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_format_set(QB_LOG_SYSLOG, "%b");

	qb_log(LOG_ERR, "QMF Agent Initialized: broker=localhost:49000 interval=5 storeFile=.cloudpolicyengine-data-cpe name=cloudpolicyengine.org:cpe:04eabb39-89cf-47dd-9d14-7e77d864be07 QMF Agent Initialized: broker=localhost:49000 interval=5 storeFile=.cloudpolicyengine-data-cpe name=cloudpolicyengine.org:cpe:04eabb39-89cf-47dd-9d14-7e77d864be07 QMF Agent Initialized: broker=localhost:49000 interval=5 storeFile=.cloudpolicyengine-data-cpe name=cloudpolicyengine.org:cpe:04eabb39-89cf-47dd-9d14-7e77d864be07");
}
END_TEST

static Suite *log_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("logging");

	tc = tcase_create("limits");
	tcase_add_test(tc, test_log_stupid_inputs);
	suite_add_tcase(s, tc);

	tc = tcase_create("basic");
	tcase_add_test(tc, test_log_basic);
	suite_add_tcase(s, tc);

	tc = tcase_create("format");
	tcase_add_test(tc, test_log_format);
	suite_add_tcase(s, tc);

	tc = tcase_create("enable");
	tcase_add_test(tc, test_log_enable);
	suite_add_tcase(s, tc);

	tc = tcase_create("bump");
	tcase_add_test(tc, test_log_bump);
	suite_add_tcase(s, tc);

	tc = tcase_create("threads");
	tcase_add_test(tc, test_log_threads);
	tcase_set_timeout(tc, 30);
	suite_add_tcase(s, tc);

	tc = tcase_create("long_msg");
	tcase_add_test(tc, test_log_long_msg);
	suite_add_tcase(s, tc);

	return s;
}

static void libqb_log_fn(const char *file_name,
			 int32_t file_line, int32_t severity, const char *msg)
{
	printf("libqb: %s:%d %s\n", file_name, file_line, msg);
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = log_suite();
	SRunner *sr = srunner_create(s);

	qb_util_set_log_function(libqb_log_fn);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
