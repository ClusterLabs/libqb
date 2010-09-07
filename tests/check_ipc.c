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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <check.h>
#include <qb/qbipcc.h>
#include <qb/qbipcs.h>
#include <qb/qbpoll.h>

#define IPC_NAME "ipc_test"
#define MAX_MSG_SIZE (8192*16)
static qb_ipcc_connection_t *conn;
static enum qb_ipc_type ipc_type;

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
static qb_handle_t bms_poll_handle;
static qb_ipcs_service_pt s1;

static void sigterm_handler(int32_t num)
{
	qb_ipcs_destroy(s1);
	qb_poll_stop(bms_poll_handle);
	exit(0);
}

static void s1_msg_process_fn(qb_ipcs_connection_pt c,
		void *data, size_t size)
{
	//struct qb_ipc_request_header *req_pt = (struct qb_ipc_request_header *)data;
	struct qb_ipc_response_header response;
	ssize_t res;

	response.size = sizeof(struct qb_ipc_response_header);
	response.id = 13;
	response.error = 0;
	res = qb_ipcs_response_send(c, &response,
			sizeof(response));
	if (res < 0) {
		perror("qb_ipcs_response_send");
	}
}

static void ipc_log_fn(const char *file_name,
		       int32_t file_line, int32_t severity, const char *msg)
{
	if (severity < LOG_INFO)
		fprintf(stderr, "%s:%d [%d] %s\n", file_name, file_line, severity, msg);
}

static void run_ipc_server(void)
{
	int32_t res;

	struct qb_ipcs_service_handlers sh = {
		.connection_authenticate = NULL,
		.connection_created = NULL,
		.msg_process = s1_msg_process_fn,
		.connection_destroyed = NULL,
	};
	signal(SIGTERM, sigterm_handler);

	bms_poll_handle = qb_poll_create();

	s1 = qb_ipcs_create(IPC_NAME, ipc_type, MAX_MSG_SIZE);
	fail_if(s1 == 0);

	qb_ipcs_service_handlers_set(s1, &sh);

	res = qb_ipcs_run(s1, bms_poll_handle);
	ck_assert_int_eq(res, 0);

	qb_poll_run(bms_poll_handle);
}

static int32_t run_function_in_new_process(void (*run_ipc_server_fn)(void))
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

static int32_t stop_process(pid_t pid)
{
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	return 0;
}


static char buffer[1024 * 1024];
static int32_t bmc_send_nozc(uint32_t size)
{
	struct qb_ipc_request_header *req_header = (struct qb_ipc_request_header *)buffer;
	struct qb_ipc_response_header res_header;
	int32_t res;

	req_header->id = QB_IPC_MSG_USER_START + 3;
	req_header->size = sizeof(struct qb_ipc_request_header) + size;

repeat_send:
	res = qb_ipcc_send(conn, req_header, req_header->size);
	if (res < 0) {
		if (res == -EAGAIN || res == -ENOMEM) {
			goto repeat_send;
		} else if (res == -EINVAL || res == -EINTR) {
			perror("qb_ipcc_send");
			return -1;
		} else {
			errno = -res;
			perror("qb_ipcc_send");
			goto repeat_send;
		}
	}

 repeat_recv:
	res = qb_ipcc_recv(conn,
			&res_header,
			sizeof(struct qb_ipc_response_header));
	if (res == -EAGAIN) {
		goto repeat_recv;
	}
	if (res == -EINTR) {
		return -1;
	}
	ck_assert_int_eq(res, sizeof(struct qb_ipc_response_header));
	ck_assert_int_eq(res_header.id, 13);
	ck_assert_int_eq(res_header.size, sizeof(struct qb_ipc_response_header));
	return 0;
}


static void test_ipc_txrx(void)
{
	int32_t j;
	int32_t c = 0;
	size_t size;
	pid_t pid;

	pid = run_function_in_new_process(run_ipc_server);
	fail_if(pid == -1);

	do {
		conn = qb_ipcc_connect(IPC_NAME);
		if (conn == NULL) {
			sleep(1);
			c++;
		}
	} while (conn == NULL && c < 5);
	fail_if(conn == NULL);

	for (j = 1; j < 19; j++) {
		size = (10 * j * j * j) + sizeof(struct qb_ipc_request_header);
		if (size >= MAX_MSG_SIZE)
			break;
		if (bmc_send_nozc(size) == -1) {
			break;
		}
	}
	qb_ipcc_disconnect(conn);
	stop_process(pid);
}

START_TEST(test_ipc_txrx_shm)
{
	ipc_type = QB_IPC_SHM;
	test_ipc_txrx();
}
END_TEST

START_TEST(test_ipc_txrx_pmq)
{
	ipc_type = QB_IPC_POSIX_MQ;
	test_ipc_txrx();
}
END_TEST

START_TEST(test_ipc_txrx_smq)
{
	ipc_type = QB_IPC_SYSV_MQ;
	test_ipc_txrx();
}
END_TEST

static Suite *ipc_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("ipc");

	tc = tcase_create("ipc_txrx_shm");
	tcase_add_test(tc, test_ipc_txrx_shm);
	suite_add_tcase(s, tc);

	tc = tcase_create("ipc_txrx_posix_mq");
	tcase_add_test(tc, test_ipc_txrx_pmq);
	tcase_set_timeout(tc, 10);
	suite_add_tcase(s, tc);

//	tc = tcase_create("ipc_txrx_sysv_mq");
//	tcase_add_test(tc, test_ipc_txrx_smq);
//	suite_add_tcase(s, tc);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = ipc_suite();
	SRunner *sr = srunner_create(s);

	qb_util_set_log_function(ipc_log_fn);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

