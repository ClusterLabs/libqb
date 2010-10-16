/*
 * Copyright (c) 2006-2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake <sdake@redhat.com>
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

#include <pthread.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <stdarg.h>
#include <sched.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>
#include <qb/qbipcs.h>

int32_t blocking = QB_TRUE;
int32_t events = QB_FALSE;
int32_t verbose = 0;

static qb_loop_t *bms_loop;
static qb_ipcs_service_pt s1;

static int32_t s1_connection_accept_fn(qb_ipcs_connection_t *c, uid_t uid, gid_t gid)
{
#if 0
	if (uid == 0 && gid == 0) {
		if (verbose) {
			printf("%s:%d %s authenticated connection\n",
					__FILE__, __LINE__, __func__);
		}
		return 1;
	}
	printf("%s:%d %s() BAD user!\n", __FILE__, __LINE__, __func__);
	return 0;
#else
	return 0;
#endif
}


static void s1_connection_created_fn(qb_ipcs_connection_t *c)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
}
static void s1_connection_destroyed_fn(qb_ipcs_connection_t *c)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
}

static int32_t s1_msg_process_fn(qb_ipcs_connection_t *c,
		void *data, size_t size)
{
	struct qb_ipc_request_header *req_pt = (struct qb_ipc_request_header *)data;
	struct qb_ipc_response_header response;
	ssize_t res;

	if (verbose > 2) {
		printf("%s:%d %s > msg:%d, size:%d\n",
			__FILE__, __LINE__, __func__,
			req_pt->id, req_pt->size);
	}
	response.size = sizeof(struct qb_ipc_response_header);
	response.id = 13;
	response.error = 0;
	if (blocking) {
		res = qb_ipcs_response_send(c, &response,
				sizeof(response));
		if (res < 0) {
			perror("qb_ipcs_response_send");
		}
	}
	if (events) {
		res = qb_ipcs_event_send(c, &response,
				sizeof(response));
		if (res < 0) {
			perror("qb_ipcs_event_send");
		}
	}
	return 0;
}

static void ipc_log_fn(const char *file_name,
		       int32_t file_line, int32_t severity, const char *msg)
{
	fprintf(stderr, "%s:%d [%d] %s\n", file_name, file_line, severity, msg);
}

static void sigusr1_handler(int32_t num)
{
	printf("%s(%d)\n", __func__, num);
	qb_ipcs_destroy(s1);
	exit(0);
}

static void show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -n             non-blocking ipc (default blocking)\n");
	printf("  -e             send events back instead for responses\n");
	printf("  -v             verbose\n");
	printf("  -h             show this help text\n");
	printf("  -m             use shared memory\n");
	printf("  -p             use posix message queues\n");
	printf("  -s             use sysv message queues\n");
	printf("  -u             use unix sockets\n");
	printf("\n");
}

static int32_t my_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t evts,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_add(bms_loop, p, fd, evts, data, fn);
}

static int32_t my_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_mod(bms_loop, p, fd, evts, data, fn);
}

static int32_t my_dispatch_del(int32_t fd)
{
	return qb_loop_poll_del(bms_loop, fd);
}

int32_t main(int32_t argc, char *argv[])
{
	const char *options = "nevhmpsu";
	int32_t opt;
	enum qb_ipc_type ipc_type = QB_IPC_SHM;
	struct qb_ipcs_service_handlers sh = {
		.connection_accept = s1_connection_accept_fn,
		.connection_created = s1_connection_created_fn,
		.msg_process = s1_msg_process_fn,
		.connection_destroyed = s1_connection_destroyed_fn,
	};
	struct qb_ipcs_poll_handlers ph = {
		.dispatch_add = my_dispatch_add,
		.dispatch_mod = my_dispatch_mod,
		.dispatch_del = my_dispatch_del,
	};

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'm':
			ipc_type = QB_IPC_SHM;
			break;
		case 's':
			ipc_type = QB_IPC_SYSV_MQ;
			break;
		case 'u':
			ipc_type = QB_IPC_SOCKET;
			break;
		case 'p':
			ipc_type = QB_IPC_POSIX_MQ;
			break;
		case 'n':	/* non-blocking */
			blocking = QB_FALSE;
			break;
		case 'e':	/* events */
			events = QB_TRUE;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}
	signal(SIGINT, sigusr1_handler);
	signal(SIGILL, sigusr1_handler);
	signal(SIGTERM, sigusr1_handler);

	qb_util_set_log_function(ipc_log_fn);

	bms_loop = qb_loop_create();

	s1 = qb_ipcs_create("bm1", 0, ipc_type, &sh);
	if (s1 == 0) {
		perror("qb_ipcs_create");
		exit(1);
	}
	qb_ipcs_poll_handlers_set(s1, &ph);

	qb_ipcs_run(s1);
	qb_loop_run(bms_loop);

	return EXIT_SUCCESS;
}
