/*
 * Copyright (c) 2010 Red Hat, Inc.
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

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>

static int32_t job_1_run_count = 0;
static int32_t job_2_run_count = 0;
static int32_t job_3_run_count = 0;


static int32_t job_order_1 = 1;
static int32_t job_order_2 = 2;
static int32_t job_order_3 = 3;
static int32_t job_order_4 = 4;
static int32_t job_order_5 = 5;
static int32_t job_order_6 = 6;
static int32_t job_order_7 = 7;
static int32_t job_order_8 = 8;
static int32_t job_order_9 = 9;
static int32_t job_order_10 = 10;
static int32_t job_order_11 = 11;
static int32_t job_order_12 = 12;
static int32_t job_order_13 = 13;


static void job_1(void *data)
{
	job_1_run_count++;
}

static void job_order_check(void *data)
{
	int32_t * order = (int32_t *)data;
	job_1_run_count++;
	ck_assert_int_eq(job_1_run_count, *order);

	if (job_1_run_count == 1) {
		qb_loop_job_add(NULL, QB_LOOP_MED, &job_order_10, job_order_check);
		qb_loop_job_add(NULL, QB_LOOP_MED, &job_order_11, job_order_check);
		qb_loop_job_add(NULL, QB_LOOP_MED, &job_order_12, job_order_check);
		qb_loop_job_add(NULL, QB_LOOP_MED, &job_order_13, job_order_check);
	} else if (job_1_run_count >= 13) {
		qb_loop_stop(NULL);
	}
}

static void job_stop(void *data)
{
	qb_loop_t *l = (qb_loop_t *)data;
	job_3_run_count++;
	qb_loop_stop(l);
}

static void job_2(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_2_run_count++;
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_stop);
	ck_assert_int_eq(res, 0);
}

static void job_1_r(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_1_run_count++;
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_2);
	ck_assert_int_eq(res, 0);
}

static void job_1_add_nuts(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_1_run_count++;
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_1);
	ck_assert_int_eq(res, 0);
	if (job_1_run_count < 500) {
		res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_1_add_nuts);
		ck_assert_int_eq(res, 0);
	} else {
		res = qb_loop_job_add(l, QB_LOOP_LOW, data, job_stop);
		ck_assert_int_eq(res, 0);
	}
	ck_assert_int_eq(res, 0);
}

START_TEST(test_loop_job_input)
{
	int32_t res;
	qb_loop_t *l;

	res = qb_loop_job_add(NULL, QB_LOOP_LOW,  NULL, job_2);
	ck_assert_int_eq(res, -EINVAL);

	l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_job_add(NULL, QB_LOOP_LOW,  NULL, job_2);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, 89,  NULL, job_2);
	ck_assert_int_eq(res, -EINVAL);
	res = qb_loop_job_add(l, QB_LOOP_LOW,  NULL, NULL);
	ck_assert_int_eq(res, -EINVAL);
	qb_loop_destroy(l);
}
END_TEST

START_TEST(test_loop_job_1)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_LOW,  l, job_stop);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	ck_assert_int_eq(job_1_run_count, 1);
	qb_loop_destroy(l);
}
END_TEST

START_TEST(test_loop_job_4)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_job_add(l, QB_LOOP_LOW,  l, job_1_r);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	ck_assert_int_eq(job_1_run_count, 1);
	ck_assert_int_eq(job_2_run_count, 1);
	ck_assert_int_eq(job_3_run_count, 1);
	qb_loop_destroy(l);
}
END_TEST

START_TEST(test_loop_job_nuts)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_job_add(l, QB_LOOP_LOW,  l, job_1_add_nuts);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	ck_assert(job_1_run_count >= 500);
	qb_loop_destroy(l);
}
END_TEST

START_TEST(test_loop_job_order)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	job_1_run_count = 0;

	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_1, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_2, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_3, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_4, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_5, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_6, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_7, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_8, job_order_check);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, &job_order_9, job_order_check);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	qb_loop_destroy(l);
}
END_TEST


static qb_util_stopwatch_t *rl_sw;
#define RATE_LIMIT_RUNTIME_SEC 3

static void job_add_self(void *data)
{
	int32_t res;
	uint64_t elapsed1;
	qb_loop_t *l = (qb_loop_t *)data;

	job_1_run_count++;
	qb_util_stopwatch_stop(rl_sw);
	elapsed1 = qb_util_stopwatch_us_elapsed_get(rl_sw);
	if (elapsed1 > (RATE_LIMIT_RUNTIME_SEC * QB_TIME_US_IN_SEC)) {
		/* run for 3 seconds */
		qb_loop_stop(l);
		return;
	}
	res = qb_loop_job_add(l, QB_LOOP_MED, data, job_add_self);
	ck_assert_int_eq(res, 0);
}

START_TEST(test_job_rate_limit)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	rl_sw = qb_util_stopwatch_create();
	ck_assert(rl_sw != NULL);

	qb_util_stopwatch_start(rl_sw);

	res = qb_loop_job_add(l, QB_LOOP_MED,  l, job_add_self);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	/*
	 * the test is to confirm that a single job does not run away
	 * and cause cpu spin. We are going to say that a spin is more than
	 * one job per 50ms if there is only one job pending in the loop.
	 */
	_ck_assert_int(job_1_run_count, <, (RATE_LIMIT_RUNTIME_SEC * (QB_TIME_MS_IN_SEC/50)) + 10);
	qb_loop_destroy(l);
	qb_util_stopwatch_free(rl_sw);
}
END_TEST

static void job_stop_and_del_1(void *data)
{
	int32_t res;
	qb_loop_t *l = (qb_loop_t *)data;
	job_3_run_count++;
	res = qb_loop_job_del(l, QB_LOOP_MED, l, job_1);
	ck_assert_int_eq(res, 0);
	qb_loop_stop(l);
}

START_TEST(test_job_add_del)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_job_add(l, QB_LOOP_MED, l, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_del(l, QB_LOOP_MED, l, job_1);
	ck_assert_int_eq(res, 0);

	job_1_run_count = 0;
	job_3_run_count = 0;

	res = qb_loop_job_add(l, QB_LOOP_MED, l, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, l, job_stop_and_del_1);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);
	ck_assert_int_eq(job_1_run_count, 0);
	ck_assert_int_eq(job_3_run_count, 1);

	qb_loop_destroy(l);
}
END_TEST


static Suite *loop_job_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("loop_job");

	add_tcase(s, tc, test_loop_job_input);
	add_tcase(s, tc, test_loop_job_1);
	add_tcase(s, tc, test_loop_job_4);
	add_tcase(s, tc, test_loop_job_nuts, 5);
	add_tcase(s, tc, test_job_rate_limit, 5);
	add_tcase(s, tc, test_job_add_del);
	add_tcase(s, tc, test_loop_job_order);

	return s;
}

/*
 * -----------------------------------------------------------------------
 *  Timers
 */
static qb_loop_timer_handle test_th;
static qb_loop_timer_handle test_th2;

static void check_time_left(void *data)
{
	qb_loop_t *l = (qb_loop_t *)data;

	/* NOTE: We are checking the 'stop_loop' timer here, not our own */
	uint64_t abs_time = qb_loop_timer_expire_time_get(l, test_th);
	uint64_t rel_time = qb_loop_timer_expire_time_remaining(l, test_th);

	ck_assert(abs_time > 0ULL);
	ck_assert(rel_time > 0ULL);
	ck_assert(abs_time >  rel_time);
	ck_assert(rel_time <= 60*QB_TIME_NS_IN_MSEC);
}


START_TEST(test_loop_timer_input)
{
	int32_t res;
	qb_loop_t *l;

	res = qb_loop_timer_add(NULL, QB_LOOP_LOW, 5*QB_TIME_NS_IN_MSEC, NULL, job_2, &test_th);
	ck_assert_int_eq(res, -EINVAL);

	l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_timer_add(NULL, QB_LOOP_LOW, 5*QB_TIME_NS_IN_MSEC, NULL, job_2, &test_th);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 5*QB_TIME_NS_IN_MSEC, l, NULL, &test_th);
	ck_assert_int_eq(res, -EINVAL);
	qb_loop_destroy(l);
}
END_TEST

static void one_shot_tmo(void * data)
{
	static int32_t been_here = QB_FALSE;
	ck_assert_int_eq(been_here, QB_FALSE);
	been_here = QB_TRUE;
}

static qb_loop_timer_handle reset_th;
static int32_t reset_timer_step = 0;
static void reset_one_shot_tmo(void*data)
{
	int32_t res;
	qb_loop_t *l = data;
	if (reset_timer_step == 0) {
		res = qb_loop_timer_del(l, reset_th);
		ck_assert_int_eq(res, -EINVAL);
		res = qb_loop_timer_is_running(l, reset_th);
		ck_assert_int_eq(res, QB_FALSE);
		res = qb_loop_timer_add(l, QB_LOOP_LOW, 8*QB_TIME_NS_IN_MSEC, l, reset_one_shot_tmo, &reset_th);
		ck_assert_int_eq(res, 0);
	}

	reset_timer_step++;
}

START_TEST(test_loop_timer_basic)
{
	int32_t res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 5*QB_TIME_NS_IN_MSEC, l, one_shot_tmo, &test_th);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_is_running(l, test_th);
	ck_assert_int_eq(res, QB_TRUE);

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 7*QB_TIME_NS_IN_MSEC, l, reset_one_shot_tmo, &reset_th);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_add(l, QB_LOOP_HIGH, 20*QB_TIME_NS_IN_MSEC, l, check_time_left, &test_th2);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 60*QB_TIME_NS_IN_MSEC, l, job_stop, &test_th);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);

	ck_assert_int_eq(reset_timer_step, 2);

	qb_loop_destroy(l);
}
END_TEST

static void *loop_timer_thread(void *arg)
{
	int res;
	qb_loop_t *l = (qb_loop_t *)arg;
	qb_loop_timer_handle test_tht;

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 5*QB_TIME_NS_IN_MSEC, l, one_shot_tmo, &test_tht);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_is_running(l, test_th);
	ck_assert_int_eq(res, QB_TRUE);

	sleep(5);

	return (void *)0;
}

/* This test will probably never fail (unless something
   really bad happens) but is useful for running under
   helgrind to find threading issues */
START_TEST(test_loop_timer_threads)
{
	int32_t res;
	pthread_t thr;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	res = pthread_create(&thr, NULL, loop_timer_thread, l);

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 7*QB_TIME_NS_IN_MSEC, l, reset_one_shot_tmo, &reset_th);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_add(l, QB_LOOP_HIGH, 20*QB_TIME_NS_IN_MSEC, l, check_time_left, &test_th2);
	ck_assert_int_eq(res, 0);

	res = qb_loop_timer_add(l, QB_LOOP_LOW, 60*QB_TIME_NS_IN_MSEC, l, job_stop, &test_th);
	ck_assert_int_eq(res, 0);

	qb_loop_run(l);

	ck_assert_int_eq(reset_timer_step, 2);

	pthread_join(thr, NULL);
	qb_loop_destroy(l);
}
END_TEST




struct qb_stop_watch {
	uint64_t start;
	uint64_t end;
	qb_loop_t *l;
	uint64_t ns_timer;
	int64_t total;
	int32_t count;
	int32_t killer;
	qb_loop_timer_handle th;
};

static void stop_watch_tmo(void*data)
{
	struct qb_stop_watch *sw = (struct qb_stop_watch *)data;
	float per;
	int64_t diff;

	sw->end = qb_util_nano_current_get();

	diff = sw->end - sw->start;
	if (diff < sw->ns_timer) {
		printf("timer expired early! by %"PRIi64"\n", (int64_t)(sw->ns_timer - diff));
	}
	ck_assert(diff >= sw->ns_timer);
	sw->total += diff;
	sw->total -= sw->ns_timer;
	sw->start = sw->end;

	sw->count++;
	if (sw->count < 50) {
		qb_loop_timer_add(sw->l, QB_LOOP_LOW, sw->ns_timer, data, stop_watch_tmo, &sw->th);
	} else {
		per = ((sw->total * 100) / sw->count) / (float)sw->ns_timer;
		printf("average error for %"PRIu64" ns timer is %"PRIi64" (ns) (%f)\n",
		       sw->ns_timer,
		       (int64_t)(sw->total/sw->count), per);
		if (sw->killer) {
			qb_loop_stop(sw->l);
		}
	}
}

static void start_timer(qb_loop_t *l, struct qb_stop_watch *sw, uint64_t timeout, int32_t killer)
{
	int32_t res;

	sw->l = l;
	sw->count = 0;
	sw->total = 0;
	sw->killer = killer;
	sw->ns_timer = timeout;
	sw->start = qb_util_nano_current_get();
	res = qb_loop_timer_add(sw->l, QB_LOOP_LOW, sw->ns_timer, sw, stop_watch_tmo, &sw->th);
	ck_assert_int_eq(res, 0);
}


START_TEST(test_loop_timer_precision)
{
	int32_t i;
	uint64_t tmo;
	struct qb_stop_watch sw[11];
	qb_loop_t *l = qb_loop_create();

	ck_assert(l != NULL);

	for (i = 0; i < 10; i++) {
		tmo = ((1 + i * 9) * QB_TIME_NS_IN_MSEC) + 500000;
		start_timer(l, &sw[i], tmo, QB_FALSE);
	}
	start_timer(l, &sw[i], 100 * QB_TIME_NS_IN_MSEC, QB_TRUE);

	qb_loop_run(l);
	qb_loop_destroy(l);
}
END_TEST

static int expire_leak_counter = 0;
#define EXPIRE_NUM_RUNS 10
static int expire_leak_runs = 0;

static void empty_func_tmo(void*data)
{
	expire_leak_counter++;
}

static void stop_func_tmo(void*data)
{
	qb_loop_t *l = (qb_loop_t *)data;
	qb_log(LOG_DEBUG, "expire_leak_counter:%d",  expire_leak_counter);
	qb_loop_stop(l);
}

static void next_func_tmo(void*data)
{
	qb_loop_t *l = (qb_loop_t *)data;
	int32_t i;
	uint64_t tmo;
	uint64_t max_tmo = 0;
	qb_loop_timer_handle th;

	qb_log(LOG_DEBUG, "expire_leak_counter:%d",  expire_leak_counter);
	for (i = 0; i < 300; i++) {
		tmo = ((1 + i) * QB_TIME_NS_IN_MSEC) + 500000;
		qb_loop_timer_add(l, QB_LOOP_LOW, tmo, NULL, empty_func_tmo, &th);
		qb_loop_timer_add(l, QB_LOOP_MED, tmo, NULL, empty_func_tmo, &th);
		qb_loop_timer_add(l, QB_LOOP_HIGH, tmo, NULL, empty_func_tmo, &th);
		max_tmo = QB_MAX(max_tmo, tmo);
	}
	expire_leak_runs++;
	if (expire_leak_runs == EXPIRE_NUM_RUNS) {
		qb_loop_timer_add(l, QB_LOOP_LOW, max_tmo, l, stop_func_tmo, &th);
	} else {
		qb_loop_timer_add(l, QB_LOOP_LOW, max_tmo, l, next_func_tmo, &th);
	}
}

/*
 * make sure that file descriptors don't get leaked with no qb_loop_timer_del()
 */
START_TEST(test_loop_timer_expire_leak)
{
	int32_t i;
	uint64_t tmo;
	uint64_t max_tmo = 0;
	qb_loop_timer_handle th;
	qb_loop_t *l = qb_loop_create();

	ck_assert(l != NULL);

	expire_leak_counter = 0;
	for (i = 0; i < 300; i++) {
		tmo = ((1 + i) * QB_TIME_NS_IN_MSEC) + 500000;
		qb_loop_timer_add(l, QB_LOOP_LOW, tmo, NULL, empty_func_tmo, &th);
		qb_loop_timer_add(l, QB_LOOP_MED, tmo, NULL, empty_func_tmo, &th);
		qb_loop_timer_add(l, QB_LOOP_HIGH, tmo, NULL, empty_func_tmo, &th);
		max_tmo = QB_MAX(max_tmo, tmo);
	}
	qb_loop_timer_add(l, QB_LOOP_LOW, max_tmo, l, next_func_tmo, &th);
	expire_leak_runs = 1;

	qb_loop_run(l);

	ck_assert_int_eq(expire_leak_counter, 300*3* EXPIRE_NUM_RUNS);
	qb_loop_destroy(l);
}
END_TEST

static int received_signum = 0;
static int received_sigs = 0;

static int32_t
sig_handler(int32_t rsignal, void *data)
{
	qb_loop_t *l = (qb_loop_t *)data;
	qb_log(LOG_DEBUG, "caught signal %d", rsignal);
	received_signum = rsignal;
	received_sigs++;
	qb_loop_job_add(l, QB_LOOP_LOW, NULL, job_stop);
	return 0;
}

START_TEST(test_loop_sig_handling)
{
	qb_loop_signal_handle handle;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	qb_loop_signal_add(l, QB_LOOP_HIGH, SIGINT,
			   l, sig_handler, &handle);
	qb_loop_signal_add(l, QB_LOOP_HIGH, SIGTERM,
			   l, sig_handler, &handle);
	qb_loop_signal_add(l, QB_LOOP_HIGH, SIGQUIT,
			   l, sig_handler, &handle);
	kill(getpid(), SIGINT);
	qb_loop_run(l);
	ck_assert_int_eq(received_signum, SIGINT);
	kill(getpid(), SIGQUIT);
	qb_loop_run(l);
	ck_assert_int_eq(received_signum, SIGQUIT);

	qb_loop_destroy(l);
}
END_TEST

/* Globals for this test only */
static int our_signal_called = 0;
static qb_loop_t *this_l;
static void handle_nonqb_signal(int num)
{
	our_signal_called = 1;
	qb_loop_job_add(this_l, QB_LOOP_LOW, NULL, job_stop);
}

START_TEST(test_loop_dont_override_other_signals)
{
	qb_loop_signal_handle handle;

	this_l = qb_loop_create();
	ck_assert(this_l != NULL);

	signal(SIGUSR1, handle_nonqb_signal);

	qb_loop_signal_add(this_l, QB_LOOP_HIGH, SIGINT,
			   this_l, sig_handler, &handle);
	kill(getpid(), SIGUSR1);
	qb_loop_run(this_l);

	ck_assert_int_eq(our_signal_called, 1);

	qb_loop_destroy(this_l);
}
END_TEST


START_TEST(test_loop_sig_only_get_one)
{
	int res;
	qb_loop_signal_handle handle;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	/* make sure we only get one call to the handler
	 * don't assume we are going to exit the loop.
	 */
	received_sigs = 0;
	qb_loop_signal_add(l, QB_LOOP_LOW, SIGINT,
			   l, sig_handler, &handle);

	res = qb_loop_job_add(l, QB_LOOP_MED, NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_MED, NULL, job_1);
	ck_assert_int_eq(res, 0);

	kill(getpid(), SIGINT);
	qb_loop_run(l);

	ck_assert_int_eq(received_signum, SIGINT);
	ck_assert_int_eq(received_sigs, 1);

	qb_loop_destroy(l);
}
END_TEST

static qb_loop_signal_handle sig_hdl;

static void
job_rm_sig_handler(void *data)
{
	int res;
	qb_loop_t *l = (qb_loop_t *)data;
	res = qb_loop_signal_del(l, sig_hdl);
	ck_assert_int_eq(res, 0);
	res = qb_loop_job_add(l, QB_LOOP_LOW, NULL, job_stop);
	ck_assert_int_eq(res, 0);
}

START_TEST(test_loop_sig_delete)
{
	int res;
	qb_loop_t *l = qb_loop_create();
	ck_assert(l != NULL);

	/* make sure we can remove a signal job from the job queue.
	 */
	received_sigs = 0;
	received_signum = 0;
	res = qb_loop_signal_add(l, QB_LOOP_MED, SIGINT,
				 l, sig_handler, &sig_hdl);
	ck_assert_int_eq(res, 0);

	res = qb_loop_job_add(l, QB_LOOP_HIGH, NULL,
			      job_rm_sig_handler);
	ck_assert_int_eq(res, 0);

	kill(getpid(), SIGINT);
	qb_loop_run(l);

	ck_assert_int_eq(received_sigs, 0);
	ck_assert_int_eq(received_signum, 0);

	qb_loop_destroy(l);
}
END_TEST

static Suite *
loop_timer_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("loop_timers");

	add_tcase(s, tc, test_loop_timer_input);
	add_tcase(s, tc, test_loop_timer_basic, 30);
	add_tcase(s, tc, test_loop_timer_precision, 30);
	add_tcase(s, tc, test_loop_timer_expire_leak, 30);
	add_tcase(s, tc, test_loop_timer_threads, 30);

	return s;
}

static Suite *
loop_signal_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("loop_signal_suite");

	add_tcase(s, tc, test_loop_sig_handling, 10);
	add_tcase(s, tc, test_loop_sig_only_get_one);
	add_tcase(s, tc, test_loop_sig_delete);
	add_tcase(s, tc, test_loop_dont_override_other_signals);

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;
	SRunner *sr = srunner_create(loop_job_suite());
	srunner_add_suite (sr, loop_timer_suite());
	srunner_add_suite (sr, loop_signal_suite());

	qb_log_init("check", LOG_USER, LOG_EMERG);
	atexit(qb_log_fini);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
