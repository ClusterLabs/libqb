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

#include <qb/qbutil.h>
#include <qb/qbpoll.h>
#include <qb/qbipcs.h>

int blocking = 1;
int verbose = 0;

static qb_hdb_handle_t bms_poll_handle;

struct lib_handler {
	void (*lib_handler_fn) (void *conn, const void *msg);
};

struct service_engine {
	unsigned int private_data_size;
	int (*lib_init_fn) (void *conn);
	int (*lib_exit_fn) (void *conn);
	struct lib_handler *lib_engine;
};

static void bms_benchmark_one_fn(void *conn, const void *msg)
{
	qb_ipc_response_header_t res;

	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	res.size = sizeof(qb_ipc_response_header_t);
	res.id = 0;
	res.error = 0;
	if (blocking == 1) {
		qb_ipcs_response_send(conn, &res, sizeof(res));
	}
}

int ii = 0;
static void bms_benchmark_two_fn(void *conn, const void *msg)
{
	const qb_ipc_request_header_t *req = msg;
	const char *req_buf = (char *)msg + sizeof(qb_ipc_request_header_t);
	qb_ipc_response_header_t res_done;

	qb_ipc_response_header_t res;
	struct iovec iovec[2];
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}

	res.size =
	    req->size - sizeof(qb_ipc_request_header_t) +
	    sizeof(qb_ipc_response_header_t);
	res.error = 0;
	iovec[0].iov_base = &res;
	iovec[0].iov_len = sizeof(res);
	iovec[1].iov_base = (void *)req_buf;
	iovec[1].iov_len = req->size - sizeof(qb_ipc_request_header_t);

	qb_ipcs_dispatch_iov_send(conn, iovec, 2);
	res_done.size = sizeof(qb_ipc_response_header_t);
	res_done.error = 0;
	qb_ipcs_response_send(conn, &res_done, sizeof(res_done));
}

static int bms_lib_init_fn(void *conn)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	return (0);
}

static int bms_lib_exit_fn(void *conn)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	return (0);
}

static struct lib_handler bms_lib_engine_one[] = {
	{			/* entry 0 */
	 .lib_handler_fn = bms_benchmark_one_fn,
	 },
	{			/* entry 1 */
	 .lib_handler_fn = bms_benchmark_two_fn,
	 }
};

static struct service_engine services[1] = {
	{
	 .private_data_size = 0,
	 .lib_init_fn = bms_lib_init_fn,
	 .lib_exit_fn = bms_lib_exit_fn,
	 .lib_engine = bms_lib_engine_one,
	 }
};

static void bms_serialize_lock(void)
{
}

static void bms_serialize_unlock(void)
{
}

/*
 * Provides the glue from bms to the IPC Service
 */
static int bms_private_data_size_get(unsigned int service)
{
	return (services[service].private_data_size);
}

static qb_ipcs_init_fn_lvalue bms_init_fn_get(unsigned int service)
{
	return (services[service].lib_init_fn);
}

static qb_ipcs_exit_fn_lvalue bms_exit_fn_get(unsigned int service)
{
	return (services[service].lib_exit_fn);
}

static qb_ipcs_handler_fn_lvalue bms_handler_fn_get(unsigned int service,
						    unsigned int id)
{
	return (services[service].lib_engine[id].lib_handler_fn);
}

static int bms_security_valid(int euid, int egid)
{
	if (euid == 0 || egid == 0) {
		return (1);
	}
	printf("%s:%d %s NOT VALID!\n", __FILE__, __LINE__, __func__);
	return (0);
}

static int bms_service_available(unsigned int service)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	return (service < 1);
}

static int bms_sending_allowed(unsigned int service,
			       unsigned int id,
			       const void *msg,
			       void *sending_allowed_private_data)
{
	return (1);
}

static void bms_sending_allowed_release(void *sending_allowed_private_data)
{
}

static void ipc_log_fn(const char *file_name,
		       int32_t file_line, int32_t severity, const char *msg)
{
	fprintf(stderr, "%s:%d [%d] %s\n", file_name, file_line, severity, msg);
}

static void ipc_fatal_error(const char *error_msg)
{
	printf("FATAL Error: %s\n", error_msg);
	exit(1);
}

static int bms_poll_handler_accept(qb_hdb_handle_t handle,
				   int fd, int revent, void *context)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	return (qb_ipcs_handler_accept(fd, revent, context));
}

static int bms_poll_handler_dispatch(qb_hdb_handle_t handle,
				     int fd, int revent, void *context)
{
	return (qb_ipcs_handler_dispatch(fd, revent, context));
}

static void bms_poll_accept_add(int fd)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	qb_poll_dispatch_add(bms_poll_handle, fd, POLLIN | POLLNVAL, 0,
			     bms_poll_handler_accept);
}

static void bms_poll_dispatch_add(int fd, void *context)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	qb_poll_dispatch_add(bms_poll_handle, fd, POLLIN | POLLNVAL, context,
			     bms_poll_handler_dispatch);
}

static void bms_poll_dispatch_modify(int fd, int events)
{
	if (verbose) {
		printf("%s:%d %s\n", __FILE__, __LINE__, __func__);
	}
	qb_poll_dispatch_modify(bms_poll_handle, fd, events,
				bms_poll_handler_dispatch);
}

struct sched_param sched_param = {
	.sched_priority = 99,
};

struct qb_ipcs_init_state ipc_init_state = {
	.socket_name = "qb_ipcs_bm",
	.sched_policy = 0,
	.sched_param = NULL,
	.malloc = malloc,
	.free = free,
	.fatal_error = ipc_fatal_error,
	.security_valid = bms_security_valid,
	.service_available = bms_service_available,
	.private_data_size_get = bms_private_data_size_get,
	.serialize_lock = bms_serialize_lock,
	.serialize_unlock = bms_serialize_unlock,
	.sending_allowed = bms_sending_allowed,
	.sending_allowed_release = bms_sending_allowed_release,
	.poll_accept_add = bms_poll_accept_add,
	.poll_dispatch_add = bms_poll_dispatch_add,
	.poll_dispatch_modify = bms_poll_dispatch_modify,
	.init_fn_get = bms_init_fn_get,
	.exit_fn_get = bms_exit_fn_get,
	.handler_fn_get = bms_handler_fn_get
};

static void sigusr1_handler(int num)
{
	printf("%s(%d)\n", __func__, num);
	qb_ipcs_ipc_exit();
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
	printf("  -v             verbose\n");
	printf("  -h             show this help text\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	const char *options = "nvh";
	int opt;

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'n':	/* non-blocking */
			blocking = 0;
			break;
		case 'v':
			verbose = 1;
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

	bms_poll_handle = qb_poll_create();

	qb_ipcs_ipc_init(&ipc_init_state);

	qb_poll_run(bms_poll_handle);

	return EXIT_SUCCESS;
}
