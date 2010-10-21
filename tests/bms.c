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
#include "os_base.h"
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>
#include <qb/qbipcs.h>
#ifdef HAVE_GLIB
#include <glib.h>
#endif

int32_t blocking = QB_TRUE;
int32_t events = QB_FALSE;
int32_t use_glib = QB_FALSE;
int32_t verbose = 0;

static qb_loop_t *bms_loop;
#ifdef HAVE_GLIB
static GMainLoop *glib_loop;
static qb_array_t *gio_map;
#endif
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
	struct qb_ipcs_stats srv_stats;

	qb_ipcs_stats_get(s1, &srv_stats, QB_FALSE);
	printf("\n Connection created\n > active:%d\n > closed:%d\n\n",
	       srv_stats.active_connections,
	       srv_stats.closed_connections);
}

static void s1_connection_destroyed_fn(qb_ipcs_connection_t *c)
{
	struct qb_ipcs_connection_stats stats;
	struct qb_ipcs_stats srv_stats;

	qb_ipcs_stats_get(s1, &srv_stats, QB_FALSE);

	qb_ipcs_connection_stats_get(c, &stats, QB_FALSE);

	printf("\n Connection to pid:%d destroyed\n > active:%d\n > closed:%d\n\n",
	       stats.client_pid,
	       srv_stats.active_connections,
	       srv_stats.closed_connections);

	printf(" Requests     %"PRIu64"\n", stats.requests);
	printf(" Responses    %"PRIu64"\n", stats.responses);
	printf(" Events       %"PRIu64"\n", stats.events);
	printf(" Send retries %"PRIu64"\n", stats.send_retries);
	printf(" Recv retries %"PRIu64"\n", stats.recv_retries);
	printf(" FC state     %d\n", stats.flow_control_state);
	printf(" FC count     %"PRIu64"\n\n", stats.flow_control_count);

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
	printf("  -g             use glib mainloop\n");
	printf("\n");
}

#ifdef HAVE_GLIB

struct gio_to_qb_poll {
	int32_t is_used;
	GIOChannel *channel;
	int32_t events;
	void * data;
	qb_ipcs_dispatch_fn_t fn;
	enum qb_loop_priority p;
};


static gboolean
gio_read_socket (GIOChannel *gio, GIOCondition condition, gpointer data)
{
	struct gio_to_qb_poll *adaptor = (struct gio_to_qb_poll *)data;
	gint fd = g_io_channel_unix_get_fd(gio);

	return (adaptor->fn(fd, condition, adaptor->data) == 0);
}

static int32_t my_g_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t evts,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	struct gio_to_qb_poll *adaptor;
	GIOChannel *channel;
	int32_t res = 0;

	res = qb_array_grow(gio_map, fd + 1);
	if (res < 0) {
		return res;
	}
	res = qb_array_index(gio_map, fd, (void**)&adaptor);
	if (res < 0) {
		return res;
	}
	if (adaptor->is_used) {
		return -EEXIST;
	}

	channel = g_io_channel_unix_new(fd);
	if (!channel) {
		return -ENOMEM;
	}

	adaptor->channel = channel;
	adaptor->fn = fn;
	adaptor->events = evts;
	adaptor->data = data;
	adaptor->p = p;
	adaptor->is_used = QB_TRUE;

	g_io_add_watch(channel, evts, gio_read_socket, adaptor);
	return 0;
}

static int32_t my_g_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	//return qb_loop_poll_mod(bms_loop, p, fd, evts, data, fn);
	return 0;
}

static int32_t my_g_dispatch_del(int32_t fd)
{
	struct gio_to_qb_poll *adaptor;
	if (qb_array_index(gio_map, fd, (void**)&adaptor) == 0) {
		g_io_channel_unref(adaptor->channel);
		adaptor->is_used = QB_FALSE;
	}
	return 0;
}

#endif /* HAVE_GLIB */

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
	const char *options = "nevhmpsug";
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
#ifdef HAVE_GLIB
	struct qb_ipcs_poll_handlers glib_ph = {
		.dispatch_add = my_g_dispatch_add,
		.dispatch_mod = my_g_dispatch_mod,
		.dispatch_del = my_g_dispatch_del,
	};
#endif /* HAVE_GLIB */

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
		case 'g':
			use_glib = QB_TRUE;
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

	if (!use_glib) {
		bms_loop = qb_loop_create();
		s1 = qb_ipcs_create("bm1", 0, ipc_type, &sh);
		if (s1 == 0) {
			perror("qb_ipcs_create");
			exit(1);
		}
		qb_ipcs_poll_handlers_set(s1, &ph);
		qb_ipcs_run(s1);
		qb_loop_run(bms_loop);
	} else {
#ifdef HAVE_GLIB
		glib_loop = g_main_loop_new(NULL, FALSE);

		gio_map = qb_array_create(64, sizeof(struct gio_to_qb_poll));

		s1 = qb_ipcs_create("bm1", 0, ipc_type, &sh);
		if (s1 == 0) {
			perror("qb_ipcs_create");
			exit(1);
		}
		qb_ipcs_poll_handlers_set(s1, &glib_ph);
		qb_ipcs_run(s1);

		g_main_loop_run(glib_loop);
#else
		printf("You don't seem to have glib-devel installed.\n");
#endif
	}
	return EXIT_SUCCESS;
}
