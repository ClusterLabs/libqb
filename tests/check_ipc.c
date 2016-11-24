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

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbipcc.h>
#include <qb/qbipcs.h>
#include <qb/qbloop.h>

#ifdef HAVE_FAILURE_INJECTION
#include "_failure_injection.h"
#endif

static char ipc_name[256];

#define DEFAULT_MAX_MSG_SIZE (8192*16)
#ifndef __clang__
static int CALCULATED_DGRAM_MAX_MSG_SIZE = 0;

#define DGRAM_MAX_MSG_SIZE \
	(CALCULATED_DGRAM_MAX_MSG_SIZE == 0 ? \
	CALCULATED_DGRAM_MAX_MSG_SIZE = qb_ipcc_verify_dgram_max_msg_size(DEFAULT_MAX_MSG_SIZE) : \
	CALCULATED_DGRAM_MAX_MSG_SIZE)

#define MAX_MSG_SIZE (ipc_type == QB_IPC_SOCKET ? DGRAM_MAX_MSG_SIZE : DEFAULT_MAX_MSG_SIZE)

#else
/* because of clang's
   'variable length array in structure' extension will never be supported;
   assign default for SHM as we'll skip test that would use run-time
   established value (via qb_ipcc_verify_dgram_max_msg_size), anyway */
static const int MAX_MSG_SIZE = DEFAULT_MAX_MSG_SIZE;
#endif

/* The size the giant msg's data field needs to be to make
 * this the largests msg we can successfully send. */
#define GIANT_MSG_DATA_SIZE MAX_MSG_SIZE - sizeof(struct qb_ipc_response_header) - 8

static int enforce_server_buffer=0;
static qb_ipcc_connection_t *conn;
static enum qb_ipc_type ipc_type;

enum my_msg_ids {
	IPC_MSG_REQ_TX_RX,
	IPC_MSG_RES_TX_RX,
	IPC_MSG_REQ_DISPATCH,
	IPC_MSG_RES_DISPATCH,
	IPC_MSG_REQ_BULK_EVENTS,
	IPC_MSG_RES_BULK_EVENTS,
	IPC_MSG_REQ_STRESS_EVENT,
	IPC_MSG_RES_STRESS_EVENT,
	IPC_MSG_REQ_SERVER_FAIL,
	IPC_MSG_RES_SERVER_FAIL,
	IPC_MSG_REQ_SERVER_DISCONNECT,
	IPC_MSG_RES_SERVER_DISCONNECT,
};

/* Test Cases
 *
 * 1) basic send & recv different message sizes
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
 * 7) service availability
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
static int32_t num_stress_events = 30000;
static int32_t reference_count_test = QB_FALSE;
static int32_t multiple_connections = QB_FALSE;


static int32_t
exit_handler(int32_t rsignal, void *data)
{
	qb_log(LOG_DEBUG, "caught signal %d", rsignal);
	qb_ipcs_destroy(s1);
	exit(0);
}

static void
set_ipc_name(const char *prefix)
{
	/* We have to give the server name a random postfix because
	 * some build systems attempt to generate packages for libqb
	 * in parallel. These unit tests are run during the package
	 * build process. Two builds executing on the same machine
	 * can stomp on each other's unit tests if the ipc server
	 * names aren't unique... This was very confusing to debug */
	snprintf(ipc_name, 256, "%s-%d", prefix, (int32_t)random());
}

static int32_t
s1_msg_process_fn(qb_ipcs_connection_t *c,
		void *data, size_t size)
{
	struct qb_ipc_request_header *req_pt = (struct qb_ipc_request_header *)data;
	struct qb_ipc_response_header response = { 0, };
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
		uint32_t max_size = MAX_MSG_SIZE;

		response.size = sizeof(struct qb_ipc_response_header);
		response.error = 0;

		stats = qb_ipcs_connection_stats_get_2(c, QB_FALSE);
		num = stats->event_q_length;
		free(stats);

		/* crazy large message */
		res = qb_ipcs_event_send(c, &response, max_size*10);
		ck_assert_int_eq(res, -EMSGSIZE);

		/* send one event before responding */
		res = qb_ipcs_event_send(c, &response, sizeof(response));
		ck_assert_int_eq(res, sizeof(response));
		response.id++;

		/* There should be one more item in the event queue now. */
		stats = qb_ipcs_connection_stats_get_2(c, QB_FALSE);
		ck_assert_int_eq(stats->event_q_length - num, 1);
		free(stats);

		/* send response */
		response.id = IPC_MSG_RES_BULK_EVENTS;
		res = qb_ipcs_response_send(c, &response, response.size);
		ck_assert_int_eq(res, sizeof(response));

		/* send the rest of the events after the response */
		for (m = 1; m < num_bulk_events; m++) {
			res = qb_ipcs_event_send(c, &response, sizeof(response));

			if (res == -EAGAIN || res == -ENOBUFS) {
				/* retry */
				usleep(1000);
				m--;
				continue;
			}
			ck_assert_int_eq(res, sizeof(response));
			response.id++;
		}

	} else if (req_pt->id == IPC_MSG_REQ_STRESS_EVENT) {
		struct {
			struct qb_ipc_response_header hdr __attribute__ ((aligned(8)));
			char data[GIANT_MSG_DATA_SIZE] __attribute__ ((aligned(8)));
			uint32_t sent_msgs __attribute__ ((aligned(8)));
		} __attribute__ ((aligned(8))) giant_event_send;
		int32_t m;

		response.size = sizeof(struct qb_ipc_response_header);
		response.error = 0;

		response.id = IPC_MSG_RES_STRESS_EVENT;
		res = qb_ipcs_response_send(c, &response, response.size);
		ck_assert_int_eq(res, sizeof(response));

		giant_event_send.hdr.error = 0;
		giant_event_send.hdr.id = IPC_MSG_RES_STRESS_EVENT;
		for (m = 0; m < num_stress_events; m++) {
			size_t sent_len = sizeof(struct qb_ipc_response_header);

			if (((m+1) % 1000) == 0) {
				sent_len = sizeof(giant_event_send);
				giant_event_send.sent_msgs = m + 1;
			}
			giant_event_send.hdr.size = sent_len;

			res = qb_ipcs_event_send(c, &giant_event_send, sent_len);
			if (res < 0) {
				if (res == -EAGAIN || res == -ENOBUFS) {
					/* yield to the receive process */
					usleep(1000);
					m--;
					continue;
				} else {
					qb_perror(LOG_DEBUG, "sending stress events");
					ck_assert_int_eq(res, sent_len);
				}
			} else if (((m+1) % 1000) == 0) {
				qb_log(LOG_DEBUG, "SENT: %d stress events sent", m+1);
			}
			giant_event_send.hdr.id++;
		}

	} else if (req_pt->id == IPC_MSG_REQ_SERVER_FAIL) {
		exit(0);
	} else if (req_pt->id == IPC_MSG_REQ_SERVER_DISCONNECT) {
		multiple_connections = QB_FALSE;
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
	if (multiple_connections) {
		return 0;
	}
	qb_enter();
	qb_leave();
	return 0;
}

static void
outq_flush (void *data)
{
	static int i = 0;
	struct cs_ipcs_conn_context *cnx;
	cnx = qb_ipcs_context_get(data);

	qb_log(LOG_DEBUG,"iter %u\n", i);
	i++;
	if (i == 2) {
		qb_ipcs_destroy(s1);
		s1 = NULL;
	}
	/* if the reference counting is not working, this should fail
	 * for i > 1.
	 */
	qb_ipcs_event_send(data, "test", 4);
	assert(memcmp(cnx, "test", 4) == 0);
	if (i < 5) {
		qb_loop_job_add(my_loop, QB_LOOP_HIGH, data, outq_flush);
	} else {
		/* this single unref should clean everything up.
		 */
		qb_ipcs_connection_unref(data);
		qb_log(LOG_INFO, "end of test, stopping loop");
		qb_loop_stop(my_loop);
	}
}


static void
s1_connection_destroyed(qb_ipcs_connection_t *c)
{
	if (multiple_connections) {
		return;
	}

	qb_enter();
	if (reference_count_test) {
		struct cs_ipcs_conn_context *cnx;
		cnx = qb_ipcs_context_get(c);
		free(cnx);
	} else {
		qb_loop_stop(my_loop);
	}
	qb_leave();
}

static void
s1_connection_created(qb_ipcs_connection_t *c)
{
	uint32_t max = MAX_MSG_SIZE;
	if (multiple_connections) {
		return;
	}

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
	if (reference_count_test) {
		struct cs_ipcs_conn_context *context;

		qb_ipcs_connection_ref(c);
		qb_loop_job_add(my_loop, QB_LOOP_HIGH, c, outq_flush);

		context = calloc(1, 20);
		memcpy(context, "test", 4);
		qb_ipcs_context_set(c, context);
	}


	ck_assert_int_eq(max, qb_ipcs_connection_get_buffer_size(c));

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
	uint32_t max_size = MAX_MSG_SIZE;

	qb_loop_signal_add(my_loop, QB_LOOP_HIGH, SIGTERM,
			   NULL, exit_handler, &handle);

	my_loop = qb_loop_create();

	s1 = qb_ipcs_create(ipc_name, 4, ipc_type, &sh);
	fail_if(s1 == 0);

	if (enforce_server_buffer) {
		qb_ipcs_enforce_buffer_size(s1, max_size);
	}
	qb_ipcs_poll_handlers_set(s1, &ph);

	res = qb_ipcs_run(s1);
	ck_assert_int_eq(res, 0);

	qb_loop_run(my_loop);
	qb_log(LOG_DEBUG, "loop finished - done ...");
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
		exit(0);
	}
	return pid;
}

static void
request_server_exit(void)
{
	struct qb_ipc_request_header req_header;
	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t res;

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
	 * confirm we get -ENOTCONN or ECONNRESET
	 */
	if (res != -ECONNRESET && res != -ENOTCONN) {
		qb_log(LOG_ERR, "id:%d size:%d", res_header.id, res_header.size);
		ck_assert_int_eq(res, -ENOTCONN);
	}
}

static void
kill_server(pid_t pid)
{
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}

static int32_t
verify_graceful_stop(pid_t pid)
{
	int wait_rc = 0;
	int status = 0;
	int rc = 0;
	int tries;

	/* We need the server to be able to exit by itself */
	for (tries = 10;  tries >= 0; tries--) {
		sleep(1);
		wait_rc = waitpid(pid, &status, WNOHANG);
		if (wait_rc > 0) {
			break;
		}
	}

	ck_assert_int_eq(wait_rc, pid);
	rc = WIFEXITED(status);
	if (rc) {
		rc = WEXITSTATUS(status);
		ck_assert_int_eq(rc, 0);
	} else {
		fail_if(rc == 0);
	}
	
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
	uint32_t max_size = MAX_MSG_SIZE;

	request.hdr.id = req_id;
	request.hdr.size = sizeof(struct qb_ipc_request_header) + size;

	/* check that we can't send a message that is too big
	 * and we get the right return code.
	 */
	res = qb_ipcc_send(conn, &request, max_size*2);
	ck_assert_int_eq(res, -EMSGSIZE);

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

static void
test_ipc_txrx_timeout(void)
{
	struct qb_ipc_request_header req_header;
	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t res;
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	/* The dispatch response will only come over
	 * the event channel, we want to verify the receive times
	 * out when an event is returned with no response */
	req_header.id = IPC_MSG_REQ_DISPATCH;
	req_header.size = sizeof(struct qb_ipc_request_header);

	iov[0].iov_len = req_header.size;
	iov[0].iov_base = &req_header;

	res = qb_ipcc_sendv_recv(conn, iov, 1,
				 &res_header,
				 sizeof(struct qb_ipc_response_header), 5000);

	ck_assert_int_eq(res, -ETIMEDOUT);

	request_server_exit();
	verify_graceful_stop(pid);

	/*
	 * wait a bit for the server to die.
	 */
	sleep(1);

	/*
	 * this needs to free up the shared mem
	 */
	qb_ipcc_disconnect(conn);
}

static int32_t recv_timeout = -1;
static void
test_ipc_txrx(void)
{
	int32_t j;
	int32_t c = 0;
	size_t size;
	pid_t pid;
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
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
		if (size >= max_size)
			break;
		if (send_and_check(IPC_MSG_REQ_TX_RX, size,
				   recv_timeout, QB_TRUE) < 0) {
			break;
		}
	}
	if (turn_on_fc) {
		/* can't signal server to shutdown if flow control is on */
		ck_assert_int_eq(fc_enabled, QB_TRUE);
		qb_ipcc_disconnect(conn);
		/* TODO - figure out why this sleep is necessary */
		sleep(1);
		kill_server(pid);
	} else {
		request_server_exit();
		qb_ipcc_disconnect(conn);
		verify_graceful_stop(pid);
	}
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
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
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

	request_server_exit();
	verify_graceful_stop(pid);

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
	set_ipc_name(__func__);
	recv_timeout = 5000;
	test_ipc_exit();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_exit_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	recv_timeout = 1000;
	test_ipc_exit();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_shm_timeout)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_txrx_timeout();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_us_timeout)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_txrx_timeout();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_shm_tmo)
{
	qb_enter();
	turn_on_fc = QB_FALSE;
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
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
	set_ipc_name(__func__);
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
	set_ipc_name(__func__);
	test_ipc_txrx();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_us_block)
{
	qb_enter();
	turn_on_fc = QB_FALSE;
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
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
	set_ipc_name(__func__);
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
	set_ipc_name(__func__);
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
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
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
		if (size >= max_size)
			break;
		if (send_and_check(IPC_MSG_REQ_DISPATCH, size,
				   recv_timeout, QB_TRUE) < 0) {
			break;
		}
	}

	request_server_exit();
	qb_ipcc_disconnect(conn);
	verify_graceful_stop(pid);
}

START_TEST(test_ipc_dispatch_us)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_dispatch();
	qb_leave();
}
END_TEST

static int32_t events_received;

static int32_t
count_stress_events(int32_t fd, int32_t revents, void *data)
{
	struct {
		struct qb_ipc_response_header hdr __attribute__ ((aligned(8)));
		char data[GIANT_MSG_DATA_SIZE] __attribute__ ((aligned(8)));
		uint32_t sent_msgs __attribute__ ((aligned(8)));
	} __attribute__ ((aligned(8))) giant_event_recv;
	qb_loop_t *cl = (qb_loop_t*)data;
	int32_t res;

	res = qb_ipcc_event_recv(conn, &giant_event_recv,
				 sizeof(giant_event_recv),
				 -1);
	if (res > 0) {
		events_received++;

		if ((events_received % 1000) == 0) {
			qb_log(LOG_DEBUG, "RECV: %d stress events processed", events_received);
			if (res != sizeof(giant_event_recv)) {
				qb_log(LOG_DEBUG, "Unexpected recv size, expected %d got %d",
					res, sizeof(giant_event_recv));

				ck_assert_int_eq(res, sizeof(giant_event_recv));
			} else if (giant_event_recv.sent_msgs != events_received) {
				qb_log(LOG_DEBUG, "Server event mismatch. Server thinks we got %d msgs, but we only received %d",
					giant_event_recv.sent_msgs, events_received);
				/* This indicates that data corruption is occurring. Since the events
				 * received is placed at the end of the giant msg, it is possible
				 * that buffers were not allocated correctly resulting in us
				 * reading/writing to uninitialized memeory at some point. */
				ck_assert_int_eq(giant_event_recv.sent_msgs, events_received);
			}
		}
	} else if (res != -EAGAIN) {
		qb_perror(LOG_DEBUG, "count_stress_events");
		qb_loop_stop(cl);
		return -1;
	}

	if (events_received >= num_stress_events) {
		qb_loop_stop(cl);
		return -1;
	}
	return 0;
}

static int32_t
count_bulk_events(int32_t fd, int32_t revents, void *data)
{
	qb_loop_t *cl = (qb_loop_t*)data;
	struct qb_ipc_response_header res_header;
	int32_t res;

	res = qb_ipcc_event_recv(conn, &res_header,
				 sizeof(struct qb_ipc_response_header),
				 -1);
	if (res > 0) {
		events_received++;
	}

	if (events_received >= num_bulk_events) {
		qb_loop_stop(cl);
		return -1;
	}
	return 0;
}

static void
test_ipc_stress_connections(void)
{
	int32_t c = 0;
	int32_t j = 0;
	uint32_t max_size = MAX_MSG_SIZE;
	int32_t connections = 0;
	pid_t pid;

	multiple_connections = QB_TRUE;

	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	for (connections = 1; connections < 70000; connections++) {
		if (conn) {
			qb_ipcc_disconnect(conn);
			conn = NULL;
		}
		do {
			conn = qb_ipcc_connect(ipc_name, max_size);
			if (conn == NULL) {
				j = waitpid(pid, NULL, WNOHANG);
				ck_assert_int_eq(j, 0);
				sleep(1);
				c++;
			}
		} while (conn == NULL && c < 5);
		fail_if(conn == NULL);
		
		if (((connections+1) % 1000) == 0) {
			qb_log(LOG_INFO, "%d ipc connections made", connections+1);
		}
	}
	multiple_connections = QB_FALSE;

	request_server_exit();
	verify_graceful_stop(pid);
	qb_ipcc_disconnect(conn);

	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_CLEAR_ALL,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
}

static void
test_ipc_bulk_events(void)
{
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	int32_t res;
	qb_loop_t *cl;
	int32_t fd;
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
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

	request_server_exit();
	qb_ipcc_disconnect(conn);
	verify_graceful_stop(pid);
}

static void
test_ipc_stress_test(void)
{
	struct {
		struct qb_ipc_request_header hdr __attribute__ ((aligned(8)));
		char data[GIANT_MSG_DATA_SIZE] __attribute__ ((aligned(8)));
		uint32_t sent_msgs __attribute__ ((aligned(8)));
	} __attribute__ ((aligned(8))) giant_req;

	struct qb_ipc_response_header res_header;
	struct iovec iov[1];
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	int32_t res;
	qb_loop_t *cl;
	int32_t fd;
	uint32_t max_size = MAX_MSG_SIZE;
	/* This looks strange, but it serves an important purpose.
	 * This test forces the server to enforce the MAX_MSG_SIZE
	 * limit from the server side, which overrides the client's
	 * buffer limit.  To verify this functionality is working
	 * we set the client limit lower than what the server
	 * is enforcing. */
	int32_t client_buf_size = max_size - 1024;
	int32_t real_buf_size;

	enforce_server_buffer = 1;
	pid = run_function_in_new_process(run_ipc_server);
	enforce_server_buffer = 0;
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, client_buf_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	real_buf_size = qb_ipcc_get_buffer_size(conn);
	ck_assert_int_eq(real_buf_size, max_size);

	qb_log(LOG_DEBUG, "Testing %d iterations of EVENT msg passing.", num_stress_events);

	events_received = 0;
	cl = qb_loop_create();
	res = qb_ipcc_fd_get(conn, &fd);
	ck_assert_int_eq(res, 0);
	res = qb_loop_poll_add(cl, QB_LOOP_MED,
			 fd, POLLIN,
			 cl, count_stress_events);
	ck_assert_int_eq(res, 0);

	res = send_and_check(IPC_MSG_REQ_STRESS_EVENT, 0, recv_timeout, QB_TRUE);

	qb_loop_run(cl);
	ck_assert_int_eq(events_received, num_stress_events);

	giant_req.hdr.id = IPC_MSG_REQ_SERVER_FAIL;
	giant_req.hdr.size = sizeof(giant_req);

	if (giant_req.hdr.size <= client_buf_size) {
		ck_assert_int_eq(1, 0);
	}

	iov[0].iov_len = giant_req.hdr.size;
	iov[0].iov_base = &giant_req;
	res = qb_ipcc_sendv_recv(conn, iov, 1,
				 &res_header,
				 sizeof(struct qb_ipc_response_header), -1);
	if (res != -ECONNRESET && res != -ENOTCONN) {
		qb_log(LOG_ERR, "id:%d size:%d", res_header.id, res_header.size);
		ck_assert_int_eq(res, -ENOTCONN);
	}

	qb_ipcc_disconnect(conn);
	verify_graceful_stop(pid);
}

#ifndef __clang__ /* see variable length array in structure' at the top */
START_TEST(test_ipc_stress_test_us)
{
	qb_enter();
	send_event_on_created = QB_FALSE;
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_stress_test();
	qb_leave();
}
END_TEST
#endif

START_TEST(test_ipc_stress_connections_us)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_stress_connections();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_bulk_events_us)
{
	qb_enter();
	send_event_on_created = QB_FALSE;
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
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
	uint32_t max_size = MAX_MSG_SIZE;

	num_bulk_events = 1;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
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

	request_server_exit();
	qb_ipcc_disconnect(conn);
	verify_graceful_stop(pid);
}

START_TEST(test_ipc_event_on_created_us)
{
	qb_enter();
	send_event_on_created = QB_TRUE;
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
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
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
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
	 * confirm we get -ENOTCONN or -ECONNRESET
	 */
	if (res != -ECONNRESET && res != -ENOTCONN) {
		qb_log(LOG_ERR, "id:%d size:%d", res_header.id, res_header.size);
		ck_assert_int_eq(res, -ENOTCONN);
	}
	ck_assert_int_eq(QB_FALSE, qb_ipcc_is_connected(conn));

	qb_ipcc_disconnect(conn);
	kill_server(pid);
}

START_TEST(test_ipc_disconnect_after_created_us)
{
	qb_enter();
	disconnect_after_created = QB_TRUE;
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_disconnect_after_created();
	qb_leave();
}
END_TEST

static void
test_ipc_server_fail(void)
{
	int32_t j;
	int32_t c = 0;
	pid_t pid;
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	request_server_exit();
	if (_fi_unlink_inject_failure == QB_TRUE) {
		_fi_truncate_called = _fi_openat_called = 0;
	}
	ck_assert_int_eq(QB_FALSE, qb_ipcc_is_connected(conn));
	qb_ipcc_disconnect(conn);
	if (_fi_unlink_inject_failure == QB_TRUE) {
		ck_assert_int_ne(_fi_truncate_called + _fi_openat_called, 0);
	}
	verify_graceful_stop(pid);
}

START_TEST(test_ipc_server_fail_soc)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_server_fail();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_dispatch_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_dispatch();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_stress_test_shm)
{
	qb_enter();
	send_event_on_created = QB_FALSE;
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_stress_test();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_stress_connections_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_stress_connections();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_bulk_events_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_bulk_events();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_event_on_created_shm)
{
	qb_enter();
	send_event_on_created = QB_TRUE;
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_event_on_created();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_server_fail_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_server_fail();
	qb_leave();
}
END_TEST

#ifdef HAVE_FAILURE_INJECTION
START_TEST(test_ipcc_truncate_when_unlink_fails_shm)
{
	qb_enter();
	_fi_unlink_inject_failure = QB_TRUE;
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_server_fail();
	_fi_unlink_inject_failure = QB_FALSE;
	qb_leave();
}
END_TEST
#endif

static void
test_ipc_service_ref_count(void)
{
	int32_t c = 0;
	int32_t j = 0;
	pid_t pid;
	uint32_t max_size = MAX_MSG_SIZE;

	reference_count_test = QB_TRUE;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);
	sleep(1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	sleep(5);

	kill_server(pid);
}


START_TEST(test_ipc_service_ref_count_shm)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_service_ref_count();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_service_ref_count_us)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_service_ref_count();
	qb_leave();
}
END_TEST

static void test_max_dgram_size(void)
{
	/* most implementations will not let you set a dgram buffer 
	 * of 1 million bytes. This test verifies that the we can detect
	 * the max dgram buffersize regardless, and that the value we detect
	 * is consistent. */
	int32_t init;
	int32_t i;

	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_REMOVE,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);

	init = qb_ipcc_verify_dgram_max_msg_size(1000000);
	fail_if(init <= 0);
	for (i = 0; i < 100; i++) {
		int try = qb_ipcc_verify_dgram_max_msg_size(1000000);
		ck_assert_int_eq(init, try);
	}

	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
}

START_TEST(test_ipc_max_dgram_size)
{
	qb_enter();
	test_max_dgram_size();
	qb_leave();
}
END_TEST

static Suite *
make_shm_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("shm");

	add_tcase(s, tc, test_ipc_txrx_shm_timeout, 30);
	add_tcase(s, tc, test_ipc_server_fail_shm, 8);
	add_tcase(s, tc, test_ipc_txrx_shm_block, 8);
	add_tcase(s, tc, test_ipc_txrx_shm_tmo, 8);
	add_tcase(s, tc, test_ipc_fc_shm, 8);
	add_tcase(s, tc, test_ipc_dispatch_shm, 16);
	add_tcase(s, tc, test_ipc_stress_test_shm, 16);
	add_tcase(s, tc, test_ipc_bulk_events_shm, 16);
	add_tcase(s, tc, test_ipc_exit_shm, 8);
	add_tcase(s, tc, test_ipc_event_on_created_shm, 10);
	add_tcase(s, tc, test_ipc_service_ref_count_shm, 10);
	add_tcase(s, tc, test_ipc_stress_connections_shm, 3600);

#ifdef HAVE_FAILURE_INJECTION
	add_tcase(s, tc, test_ipcc_truncate_when_unlink_fails_shm, 8);
#endif

	return s;
}

static Suite *
make_soc_suite(void)
{
	Suite *s = suite_create("socket");
	TCase *tc;

	add_tcase(s, tc, test_ipc_txrx_us_timeout, 30);
	add_tcase(s, tc, test_ipc_max_dgram_size, 30);
	add_tcase(s, tc, test_ipc_server_fail_soc, 8);
	add_tcase(s, tc, test_ipc_txrx_us_block, 8);
	add_tcase(s, tc, test_ipc_txrx_us_tmo, 8);
	add_tcase(s, tc, test_ipc_fc_us, 8);
	add_tcase(s, tc, test_ipc_exit_us, 8);
	add_tcase(s, tc, test_ipc_dispatch_us, 16);
#ifndef __clang__ /* see variable length array in structure' at the top */
	add_tcase(s, tc, test_ipc_stress_test_us, 60);
#endif
	add_tcase(s, tc, test_ipc_bulk_events_us, 16);
	add_tcase(s, tc, test_ipc_event_on_created_us, 10);
	add_tcase(s, tc, test_ipc_disconnect_after_created_us, 10);
	add_tcase(s, tc, test_ipc_service_ref_count_us, 10);
	add_tcase(s, tc, test_ipc_stress_connections_us, 3600);

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;
	SRunner *sr;
	Suite *s;
	int32_t do_shm_tests = QB_TRUE;

	set_ipc_name("ipc_test");
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
