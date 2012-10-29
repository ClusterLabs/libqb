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
#include <sys/wait.h>
#include <signal.h>
#include <check.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbipcc.h>
#include <qb/qbipcs.h>
#include <qb/qbloop.h>

static const char *ipc_name = "ipc_test";
#define MAX_MSG_SIZE (8192*16)
static qb_ipcc_connection_t *conn;
static enum qb_ipc_type ipc_type;

enum my_msg_ids {
	IPC_MSG_REQ_TX_RX,
	IPC_MSG_RES_TX_RX,
	IPC_MSG_REQ_DISPATCH,
	IPC_MSG_RES_DISPATCH,
	IPC_MSG_REQ_BULK_EVENTS,
	IPC_MSG_RES_BULK_EVENTS,
	IPC_MSG_REQ_SERVER_FAIL,
	IPC_MSG_RES_SERVER_FAIL,
	IPC_MSG_REQ_SERVER_DISCONNECT,
	IPC_MSG_RES_SERVER_DISCONNECT,
};

/* Test Cases
 *
 * 1) basic send & recv differnet message sizes
 *
 * 2) send message to start dispatch (confirm receipt)
 *
 * 3) flow control
 *
 * 4) authentication
 *
 * 5) thread safety
 *
 * 6) cleanup
 *
 * 7) service availabilty
 *
 * 8) multiple services
 */
static qb_loop_t *my_loop;
static qb_ipcs_service_t* s1;
static int32_t turn_on_fc = QB_FALSE;
static int32_t fc_enabled = 89;
static int32_t send_event_on_created = QB_FALSE;
static int32_t disconnect_after_created = QB_FALSE;
static int32_t num_bulk_events = 10;

static int32_t
exit_handler(int32_t rsignal, void *data)
{
	qb_log(LOG_DEBUG, "caught signal %d", rsignal);
	qb_ipcs_destroy(s1);
	return -1;
}

static int32_t
s1_msg_process_fn(qb_ipcs_connection_t *c,
		void *data, size_t size)
{
	struct qb_ipc_request_header *req_pt = (struct qb_ipc_request_header *)data;
	struct qb_ipc_response_header response;
	ssize_t res;

	if (req_pt->id == IPC_MSG_REQ_TX_RX) {
		response.size = sizeof(struct qb_ipc_response_header);
		response.id = IPC_MSG_RES_TX_RX;
		response.error = 0;
		res = qb_ipcs_response_send(c, &response, response.size);
		if (res < 0) {
			qb_perror(LOG_INFO, "qb_ipcs_response_send");
		} else if (res != response.size) {
			qb_log(LOG_DEBUG, "qb_ipcs_response_send %zd != %d",
			       res, response.size);
		}
		if (turn_on_fc) {
			qb_ipcs_request_rate_limit(s1, QB_IPCS_RATE_OFF);
		}
	} else if (req_pt->id == IPC_MSG_REQ_DISPATCH) {
		response.size = sizeof(struct qb_ipc_response_header);
		response.id = IPC_MSG_RES_DISPATCH;
		response.error = 0;
		res = qb_ipcs_event_send(c, &response,
					 sizeof(response));
		if (res < 0) {
			qb_perror(LOG_INFO, "qb_ipcs_event_send");
		}
	} else if (req_pt->id == IPC_MSG_REQ_BULK_EVENTS) {
		int32_t m;
		int32_t num;
		struct qb_ipcs_connection_stats_2 *stats;

		response.size = sizeof(struct qb_ipc_response_header);
		response.error = 0;

		stats = qb_ipcs_connection_stats_get_2(c, QB_FALSE);
		num = stats->event_q_length;
		free(stats);

		for (m = 0; m < num_bulk_events; m++) {
			res = qb_ipcs_event_send(c, &response,
						 sizeof(response));
			ck_assert_int_eq(res, sizeof(response));
			response.id++;
		}
		stats = qb_ipcs_connection_stats_get_2(c, QB_FALSE);
		ck_assert_int_eq(stats->event_q_length - num, num_bulk_events);
		free(stats);

		response.id = IPC_MSG_RES_BULK_EVENTS;
		res = qb_ipcs_response_send(c, &response, response.size);
		ck_assert_int_eq(res, sizeof(response));

	} else if (req_pt->id == IPC_MSG_REQ_SERVER_FAIL) {
		exit(0);
	} else if (req_pt->id == IPC_MSG_REQ_SERVER_DISCONNECT) {
		qb_ipcs_disconnect(c);
	}
	return 0;
}

static int32_t
my_job_add(enum qb_loop_priority p,
			  void *data,
			  qb_loop_job_dispatch_fn fn)
{
	return qb_loop_job_add(my_loop, p, data, fn);
}

static int32_t
my_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_add(my_loop, p, fd, events, data, fn);
}

static int32_t
my_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t events,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_mod(my_loop, p, fd, events, data, fn);
}

static int32_t
my_dispatch_del(int32_t fd)
{
	return qb_loop_poll_del(my_loop, fd);
}

static int32_t
s1_connection_closed(qb_ipcs_connection_t *c)
{
	qb_enter();
	qb_leave();
	return 0;
}

static void
s1_connection_destroyed(qb_ipcs_connection_t *c)
{
	qb_enter();
	qb_loop_stop(my_loop);
	qb_leave();
}

static void
s1_connection_created(qb_ipcs_connection_t *c)
{
	if (send_event_on_created) {
		struct qb_ipc_response_header response;
		int32_t res;

		response.size = sizeof(struct qb_ipc_response_header);
		response.id = IPC_MSG_RES_DISPATCH;
		response.error = 0;
		res = qb_ipcs_event_send(c, &response,
					 sizeof(response));
		ck_assert_int_eq(res, response.size);
	}
}

static void
run_ipc_server(void)
{
	int32_t res;
	qb_loop_signal_handle handle;

	struct qb_ipcs_service_handlers sh = {
		.connection_accept = NULL,
		.connection_created = s1_connection_created,
		.msg_process = s1_msg_process_fn,
		.connection_destroyed = s1_connection_destroyed,
		.connection_closed = s1_connection_closed,
	};

	struct qb_ipcs_poll_handlers ph = {
		.job_add = my_job_add,
		.dispatch_add = my_dispatch_add,
		.dispatch_mod = my_dispatch_mod,
		.dispatch_del = my_dispatch_del,
	};

	qb_loop_signal_add(my_loop, QB_LOOP_HIGH, SIGSTOP,
			   NULL, exit_handler, &handle);
	qb_loop_signal_add(my_loop, QB_LOOP_HIGH, SIGTERM,
			   NULL, exit_handler, &handle);

	my_loop = qb_loop_create();

	s1 = qb_ipcs_create(ipc_name, 4, ipc_type, &sh);
	fail_if(s1 == 0);

	qb_ipcs_poll_handlers_set(s1, &ph);

	res = qb_ipcs_run(s1);
	ck_assert_int_eq(res, 0);

	qb_loop_run(my_loop);
}

static int32_t
run_function_in_new_process(void (*run_ipc_server_fn)(void))
{
	pid_t pid = fork ();

	if (pid == -1) {
		fprintf (stderr, "Can't fork\n");
		return -1;
	}

	if (pid == 0) {
		run_ipc_server_fn();
		return 0;
	}
	return pid;
}

static int32_t
stop_process(pid_t pid)
{
	/* wait a bit for the server to shutdown by it's self */
	usleep(100000);
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	return 0;
}

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[1024 * 1024];
};

static struct my_req request;
static int32_t
send_and_check(int32_t req_id, uint32_t size,
	       int32_t ms_timeout, int32_t expect_perfection)
{
	struct qb_ipc_response_header res_header;
	int32_t res;
	int32_t try_times = 0;

	request.hdr.id = req_id;
	request.hdr.size = sizeof(struct qb_ipc_request_header) + size;

repeat_send:

	res = qb_ipcc_send(conn, &request, request.hdr.size);
	try_times++;
	if (res < 0) {
		if (res == -EAGAIN && try_times < 10) {
			goto repeat_send;
		} else {
			if (res == -EAGAIN && try_times >= 10) {
				fc_enabled = QB_TRUE;
			}
			errno = -res;
			qb_perror(LOG_INFO, "qb_ipcc_send");
			return res;
		}
	}

	if (req_id == IPC_MSG_REQ_DISPATCH) {
		res = qb_ipcc_event_recv(conn, &res_header,
					 sizeof(struct qb_ipc_response_header),
					 ms_timeout);
	} else {
		res = qb_ipcc_recv(conn, &res_header,
				   sizeof(struct qb_ipc_response_header),
				   ms_timeout);
	}
	if (res == -EINTR) {
		return -1;
	}
	if (res == -EAGAIN || res == -ETIMEDOUT) {
		fc_enabled = QB_TRUE;
		qb_perror(LOG_DEBUG, "qb_ipcc_recv");
		return res;
	}
	if (expect_perfection) {
		ck_assert_int_eq(res, sizeof(struct qb_ipc_response_header));
		ck_assert_int_eq(res_header.id, req_id + 1);
		ck_assert_int_eq(res_header.size, sizeof(struct qb_ipc_response_header));
	}
	return res;
}

static int32_t recv_timeout = -1;
static void
test_ipc_txrx(void)
{
	int32_t j;
	int32_t c = 0;
	size_t size;
	pid_t pid;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	size = QB_MIN(sizeof(struct qb_ipc_request_header), 64);
	for (j = 1; j < 19; j++) {
		size *= 2;
		if (size >= MAX_MSG_SIZE)
			break;
		if (send_and_check(IPC_MSG_REQ_TX_RX, size,
				   recv_timeout, QB_TRUE) < 0) {
			break;
		}
	}
	if (turn_on_fc) {
		ck_assert_int_eq(fc_enabled, QB_TRUE);
	}
	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

static void
test_ipc_exit(void)
{
	struct qb_ipc_request_header req_header;
	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t res;
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	req_header.id = IPC_MSG_REQ_TX_RX;
	req_header.size = sizeof(struct qb_ipc_request_header);

	iov[0].iov_len = req_header.size;
	iov[0].iov_base = &req_header;

	res = qb_ipcc_sendv_recv(conn, iov, 1,
				 &res_header,
				 sizeof(struct qb_ipc_response_header), -1);
	ck_assert_int_eq(res, sizeof(struct qb_ipc_response_header));

	/* kill the server */
	stop_process(pid);

	/*
	 * wait a bit for the server to die.
	 */
	sleep(1);

	/*
	 * this needs to free up the shared mem
	 */
	qb_ipcc_disconnect(conn);
}

START_TEST(test_ipc_exit_us)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	recv_timeout = 5000;
	test_ipc_exit();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_exit_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	recv_timeout = 1000;
	test_ipc_exit();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_shm_tmo)
{
	qb_enter();
	turn_on_fc = QB_FALSE;
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	recv_timeout = 1000;
	test_ipc_txrx();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_shm_block)
{
	qb_enter();
	turn_on_fc = QB_FALSE;
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	recv_timeout = -1;
	test_ipc_txrx();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_fc_shm)
{
	qb_enter();
	turn_on_fc = QB_TRUE;
	ipc_type = QB_IPC_SHM;
	recv_timeout = 500;
	ipc_name = __func__;
	test_ipc_txrx();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_us_block)
{
	qb_enter();
	turn_on_fc = QB_FALSE;
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	recv_timeout = -1;
	test_ipc_txrx();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_us_tmo)
{
	qb_enter();
	turn_on_fc = QB_FALSE;
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	recv_timeout = 1000;
	test_ipc_txrx();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_fc_us)
{
	qb_enter();
	turn_on_fc = QB_TRUE;
	ipc_type = QB_IPC_SOCKET;
	recv_timeout = 500;
	ipc_name = __func__;
	test_ipc_txrx();
	qb_leave();
}
END_TEST

struct my_res {
	struct qb_ipc_response_header hdr;
	char message[1024 * 1024];
};

static void
test_ipc_dispatch(void)
{
	int32_t j;
	int32_t c = 0;
	pid_t pid;
	int32_t size;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	size = QB_MIN(sizeof(struct qb_ipc_request_header), 64);
	for (j = 1; j < 19; j++) {
		size *= 2;
		if (size >= MAX_MSG_SIZE)
			break;
		if (send_and_check(IPC_MSG_REQ_DISPATCH, size,
				   recv_timeout, QB_TRUE) < 0) {
			break;
		}
	}

	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

START_TEST(test_ipc_disp_us)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	test_ipc_dispatch();
	qb_leave();
}
END_TEST

static int32_t events_received;


static int32_t
count_bulk_events(int32_t fd, int32_t revents, void *data)
{
	qb_loop_t *cl = (qb_loop_t*)data;

	events_received++;

	if (events_received >= num_bulk_events) {
		qb_loop_stop(cl);
		return -1;
	}
	return 0;
}

static void
test_ipc_bulk_events(void)
{
	struct qb_ipc_request_header req_header;
	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	int32_t res;
	qb_loop_t *cl;
	int32_t fd;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	events_received = 0;
	cl = qb_loop_create();
	res = qb_ipcc_fd_get(conn, &fd);
	ck_assert_int_eq(res, 0);
	res = qb_loop_poll_add(cl, QB_LOOP_MED,
			 fd, POLLIN,
			 cl, count_bulk_events);
	ck_assert_int_eq(res, 0);

	res = send_and_check(IPC_MSG_REQ_BULK_EVENTS,
			     0,
			     recv_timeout, QB_TRUE);
	ck_assert_int_eq(res, sizeof(struct qb_ipc_response_header));

	qb_loop_run(cl);
	ck_assert_int_eq(events_received, num_bulk_events);

	req_header.id = IPC_MSG_REQ_SERVER_FAIL;
	req_header.size = sizeof(struct qb_ipc_request_header);

	iov[0].iov_len = req_header.size;
	iov[0].iov_base = &req_header;
	res = qb_ipcc_sendv_recv(conn, iov, 1,
				 &res_header,
				 sizeof(struct qb_ipc_response_header), -1);
	if (res != -ECONNRESET && res != -ENOTCONN) {
		qb_log(LOG_ERR, "id:%d size:%d", res_header.id, res_header.size);
		ck_assert_int_eq(res, -ENOTCONN);
	}

	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

START_TEST(test_ipc_bulk_events_us)
{
	qb_enter();
	send_event_on_created = QB_FALSE;
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	test_ipc_bulk_events();
	qb_leave();
}
END_TEST

static void
test_ipc_event_on_created(void)
{
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	int32_t res;
	qb_loop_t *cl;
	int32_t fd;

	num_bulk_events = 1;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	events_received = 0;
	cl = qb_loop_create();
	res = qb_ipcc_fd_get(conn, &fd);
	ck_assert_int_eq(res, 0);
	res = qb_loop_poll_add(cl, QB_LOOP_MED,
			 fd, POLLIN,
			 cl, count_bulk_events);
	ck_assert_int_eq(res, 0);

	qb_loop_run(cl);
	ck_assert_int_eq(events_received, num_bulk_events);

	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

START_TEST(test_ipc_event_on_created_us)
{
	qb_enter();
	send_event_on_created = QB_TRUE;
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	test_ipc_event_on_created();
	qb_leave();
}
END_TEST

static void
test_ipc_disconnect_after_created(void)
{
	struct qb_ipc_request_header req_header;
	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	int32_t res;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	ck_assert_int_eq(QB_TRUE, qb_ipcc_is_connected(conn));

	req_header.id = IPC_MSG_REQ_SERVER_DISCONNECT;
	req_header.size = sizeof(struct qb_ipc_request_header);

	iov[0].iov_len = req_header.size;
	iov[0].iov_base = &req_header;

	res = qb_ipcc_sendv_recv(conn, iov, 1,
				 &res_header,
				 sizeof(struct qb_ipc_response_header), -1);
	/*
	 * confirm we get -ENOTCONN
	 */
	ck_assert_int_eq(res, -ENOTCONN);
	ck_assert_int_eq(QB_FALSE, qb_ipcc_is_connected(conn));

	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

START_TEST(test_ipc_disconnect_after_created_us)
{
	qb_enter();
	disconnect_after_created = QB_TRUE;
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	test_ipc_disconnect_after_created();
	qb_leave();
}
END_TEST

static void
test_ipc_server_fail(void)
{
	struct qb_ipc_request_header req_header;
	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t res;
	int32_t j;
	int32_t c = 0;
	pid_t pid;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, MAX_MSG_SIZE);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	/*
	 * tell the server to exit
	 */
	req_header.id = IPC_MSG_REQ_SERVER_FAIL;
	req_header.size = sizeof(struct qb_ipc_request_header);

	iov[0].iov_len = req_header.size;
	iov[0].iov_base = &req_header;

	ck_assert_int_eq(QB_TRUE, qb_ipcc_is_connected(conn));

	res = qb_ipcc_sendv_recv(conn, iov, 1,
				 &res_header,
				 sizeof(struct qb_ipc_response_header), -1);
	/*
	 * confirm we get -ENOTCONN
	 */
	ck_assert_int_eq(res, -ENOTCONN);
	ck_assert_int_eq(QB_FALSE, qb_ipcc_is_connected(conn));

	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

START_TEST(test_ipc_server_fail_soc)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	ipc_name = __func__;
	test_ipc_server_fail();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_disp_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	test_ipc_dispatch();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_bulk_events_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	test_ipc_bulk_events();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_event_on_created_shm)
{
	qb_enter();
	send_event_on_created = QB_TRUE;
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	test_ipc_event_on_created();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_server_fail_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	ipc_name = __func__;
	test_ipc_server_fail();
	qb_leave();
}
END_TEST


static Suite *
make_shm_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("shm");

	tc = tcase_create("ipc_server_fail_shm");
	tcase_add_test(tc, test_ipc_server_fail_shm);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_txrx_shm_block");
	tcase_add_test(tc, test_ipc_txrx_shm_block);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_txrx_shm_tmo");
	tcase_add_test(tc, test_ipc_txrx_shm_tmo);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_fc_shm");
	tcase_add_test(tc, test_ipc_fc_shm);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_dispatch_shm");
	tcase_add_test(tc, test_ipc_disp_shm);
	tcase_set_timeout(tc, 16);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_bulk_events_shm");
	tcase_add_test(tc, test_ipc_bulk_events_shm);
	tcase_set_timeout(tc, 16);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_exit_shm");
	tcase_add_test(tc, test_ipc_exit_shm);
	tcase_set_timeout(tc, 3);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_event_on_created_shm");
	tcase_add_test(tc, test_ipc_event_on_created_shm);
	suite_add_tcase(s, tc);

	return s;
}

static Suite *
make_soc_suite(void)
{
	Suite *s = suite_create("socket");
	TCase *tc;

	tc = tcase_create("ipc_server_fail_soc");
	tcase_add_test(tc, test_ipc_server_fail_soc);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_txrx_us_block");
	tcase_add_test(tc, test_ipc_txrx_us_block);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_txrx_us_tmo");
	tcase_add_test(tc, test_ipc_txrx_us_tmo);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_fc_us");
	tcase_add_test(tc, test_ipc_fc_us);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_exit_us");
	tcase_add_test(tc, test_ipc_exit_us);
	tcase_set_timeout(tc, 6);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_dispatch_us");
	tcase_add_test(tc, test_ipc_disp_us);
	tcase_set_timeout(tc, 16);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_bulk_events_us");
	tcase_add_test(tc, test_ipc_bulk_events_us);
	tcase_set_timeout(tc, 16);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_event_on_created_us");
	tcase_add_test(tc, test_ipc_event_on_created_us);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_disconnect_after_created_us");
	tcase_add_test(tc, test_ipc_disconnect_after_created_us);
	suite_add_tcase(s, tc);

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;
	SRunner *sr;
	Suite *s;
	int32_t do_shm_tests = QB_TRUE;

#ifdef DISABLE_IPC_SHM
	do_shm_tests = QB_FALSE;
#endif /* DISABLE_IPC_SHM */

	s = make_soc_suite();
	sr = srunner_create(s);

	if (do_shm_tests) {
		srunner_add_suite(sr, make_shm_suite());
	}

	qb_log_init("check", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log_format_set(QB_LOG_STDERR, "lib/%f|%l| %b");

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

