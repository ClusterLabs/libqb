/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include <sys/poll.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>

static struct qb_loop *l;
static qb_loop_timer_handle th;

static void job_3_9(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_1_2(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_2_4(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_3_5(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_3_6(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_1_1(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_3_7(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_2_3(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_2_8(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }
static void job_1_9(void *data) { qb_log(LOG_INFO, "%s\n", __func__); }

static void more_important_jobs(void *data)
{
	qb_enter();
	qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1_2);
	qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1_9);
}

static int32_t handle_reconf_signal(int32_t sig, void *data)
{
	qb_log(LOG_INFO, "signal %d", sig);
	return 0;
}

static int32_t handle_exit_signal(int32_t sig, void *data)
{
	qb_log(LOG_INFO, "exiting (signal %d)... bye", sig);
	qb_loop_stop(l);
	return -1;
}

static void more_jobs(void *data)
{
	qb_log(LOG_INFO, "%s\n", __func__);
	qb_loop_timer_add(l, QB_LOOP_HIGH, 3109*QB_TIME_NS_IN_MSEC, NULL, job_1_1, &th);
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_7);
	qb_loop_timer_add(l, QB_LOOP_LOW, 1000*QB_TIME_NS_IN_MSEC, NULL, more_important_jobs, &th);
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_7);
	qb_loop_timer_add(l, QB_LOOP_LOW, 2341*QB_TIME_NS_IN_MSEC, NULL, job_3_7, &th);
	qb_loop_timer_add(l, QB_LOOP_LOW, 900, NULL, job_3_6, &th);
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_5);
	qb_loop_timer_add(l, QB_LOOP_MED, 4000*QB_TIME_NS_IN_MSEC, NULL, more_jobs, &th);
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_9);
	qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1_9);
	qb_loop_job_add(l, QB_LOOP_MED,  NULL, job_2_3);
}

static int32_t read_stdin(int32_t fd, int32_t revents, void *data)
{
	char buf[100];
	ssize_t len = read(fd, buf, 100);
	buf[len-1] = '\0';
	qb_log(LOG_INFO, "typed > \"%s\"\n", buf);
	if (strcmp(buf, "more") == 0) {
		more_jobs(NULL);
	}
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_9);
	return 0;
}

int main(int argc, char * argv[])
{
	qb_loop_signal_handle sh;

	qb_log_init("loop", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_DEBUG);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	l = qb_loop_create();

	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_9);
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_2_4);
	qb_loop_job_add(l, QB_LOOP_HIGH, NULL, job_1_2);
	qb_loop_job_add(l, QB_LOOP_MED,  NULL, job_3_7);
/*
 * 	qb_loop_timer_add(l, QB_LOOP_HIGH, 40*QB_TIME_NS_IN_MSEC, NULL, more_jobs, &th);
 */
	qb_loop_job_add(l, QB_LOOP_MED,  NULL, job_2_8);
	qb_loop_job_add(l, QB_LOOP_LOW,  NULL, job_3_6);

	qb_loop_poll_add(l, QB_LOOP_LOW, 0, POLLIN | POLLPRI | POLLNVAL,
			     NULL, read_stdin);

	qb_loop_signal_add(l, QB_LOOP_MED, SIGINT, NULL, handle_exit_signal, &sh);
	qb_loop_signal_add(l, QB_LOOP_MED, SIGSEGV, NULL, handle_exit_signal, &sh);
	qb_loop_signal_add(l, QB_LOOP_MED, SIGHUP, NULL, handle_reconf_signal, &sh);

	qb_loop_run(l);
	return 0;
}


