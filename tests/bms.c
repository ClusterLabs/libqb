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

#include <qb/qbarray.h>
#include <qb/qbdefs.h>
#include <qb/qblog.h>
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
static qb_ipcs_service_t* s1;

static int32_t s1_connection_accept_fn(qb_ipcs_connection_t *c, uid_t uid, gid_t gid)
{
#if 0
	if (uid == 0 && gid == 0) {
		if (verbose) {
			qb_log(LOG_INFO, "%s:%d %s authenticated connection\n",
					__FILE__, __LINE__, __func__);
		}
		return 1;
	}
	qb_log(LOG_INFO, "%s:%d %s() BAD user!\n", __FILE__, __LINE__, __func__);
	return 0;
#else
	return 0;
#endif
}


static void s1_connection_created_fn(qb_ipcs_connection_t *c)
{
	struct qb_ipcs_stats srv_stats;

	qb_ipcs_stats_get(s1, &srv_stats, QB_FALSE);
	qb_log(LOG_NOTICE, "Connection created > active:%d > closed:%d",
	       srv_stats.active_connections,
	       srv_stats.closed_connections);
}

static void s1_connection_destroyed_fn(qb_ipcs_connection_t *c)
{
	qb_log(LOG_INFO, "connection about to be freed\n");
}

static int32_t s1_connection_closed_fn(qb_ipcs_connection_t *c)
{
	struct qb_ipcs_connection_stats stats;
	struct qb_ipcs_stats srv_stats;

	qb_ipcs_stats_get(s1, &srv_stats, QB_FALSE);

	qb_ipcs_connection_stats_get(c, &stats, QB_FALSE);

	qb_log(LOG_INFO, "Connection to pid:%d destroyed > active:%d > closed:%d",
	       stats.client_pid,
	       srv_stats.active_connections,
	       srv_stats.closed_connections);

	qb_log(LOG_INFO, " Requests     %"PRIu64"\n", stats.requests);
	qb_log(LOG_INFO, " Responses    %"PRIu64"\n", stats.responses);
	qb_log(LOG_INFO, " Events       %"PRIu64"\n", stats.events);
	qb_log(LOG_INFO, " Send retries %"PRIu64"\n", stats.send_retries);
	qb_log(LOG_INFO, " Recv retries %"PRIu64"\n", stats.recv_retries);
	qb_log(LOG_INFO, " FC state     %d\n", stats.flow_control_state);
	qb_log(LOG_INFO, " FC count     %"PRIu64"\n\n", stats.flow_control_count);
	return 0;
}

static int32_t s1_msg_process_fn(qb_ipcs_connection_t *c,
		void *data, size_t size)
{
	struct qb_ipc_request_header *req_pt = (struct qb_ipc_request_header *)data;
	struct qb_ipc_response_header response;
	ssize_t res;

	qb_log(LOG_TRACE, "msg:%d, size:%d",
	       req_pt->id, req_pt->size);
	response.size = sizeof(struct qb_ipc_response_header);
	response.id = 13;
	response.error = 0;
	if (blocking) {
		res = qb_ipcs_response_send(c, &response,
				sizeof(response));
		if (res < 0) {
			qb_perror(LOG_ERR, "qb_ipcs_response_send");
			return res;
		}
	}
	if (events) {
		res = qb_ipcs_event_send(c, &response,
				sizeof(response));
		if (res < 0) {
			qb_perror(LOG_ERR, "qb_ipcs_event_send");
			return res;
		}
	}
	return 0;
}

static void sigusr1_handler(int32_t num)
{
	qb_log(LOG_INFO, "%s(%d)\n", __func__, num);
	qb_ipcs_destroy(s1);
	exit(0);
}

static void show_usage(const char *name)
{
	qb_log(LOG_INFO, "usage: \n");
	qb_log(LOG_INFO, "%s <options>\n", name);
	qb_log(LOG_INFO, "\n");
	qb_log(LOG_INFO, "  options:\n");
	qb_log(LOG_INFO, "\n");
	qb_log(LOG_INFO, "  -n             non-blocking ipc (default blocking)\n");
	qb_log(LOG_INFO, "  -e             send events back instead for responses\n");
	qb_log(LOG_INFO, "  -v             verbose\n");
	qb_log(LOG_INFO, "  -h             show this help text\n");
	qb_log(LOG_INFO, "  -m             use shared memory\n");
	qb_log(LOG_INFO, "  -p             use posix message queues\n");
	qb_log(LOG_INFO, "  -s             use sysv message queues\n");
	qb_log(LOG_INFO, "  -u             use unix sockets\n");
	qb_log(LOG_INFO, "  -g             use glib mainloop\n");
	qb_log(LOG_INFO, "\n");
}

#ifdef HAVE_GLIB

struct gio_to_qb_poll {
	gboolean is_used;
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
	adaptor->is_used = TRUE;

	g_io_add_watch(channel, evts, gio_read_socket, adaptor);
	return 0;
}

static int32_t my_g_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
	void *data, qb_ipcs_dispatch_fn_t fn)
{
	return 0;
}

static int32_t my_g_dispatch_del(int32_t fd)
{
	struct gio_to_qb_poll *adaptor;
	if (qb_array_index(gio_map, fd, (void**)&adaptor) == 0) {
		g_io_channel_unref(adaptor->channel);
		adaptor->is_used = FALSE;
	}
	return 0;
}

#endif /* HAVE_GLIB */

static int32_t my_job_add(enum qb_loop_priority p, void *data, qb_loop_job_dispatch_fn fn)
{
	return qb_loop_job_add(bms_loop, p, data, fn);
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
	const char *options = "nevhmpsug";
	int32_t opt;
	int32_t rc;
	enum qb_ipc_type ipc_type = QB_IPC_SHM;
	struct qb_ipcs_service_handlers sh = {
		.connection_accept = s1_connection_accept_fn,
		.connection_created = s1_connection_created_fn,
		.msg_process = s1_msg_process_fn,
		.connection_destroyed = s1_connection_destroyed_fn,
		.connection_closed = s1_connection_closed_fn,
	};
	struct qb_ipcs_poll_handlers ph = {
		.job_add = my_job_add,
		.dispatch_add = my_dispatch_add,
		.dispatch_mod = my_dispatch_mod,
		.dispatch_del = my_dispatch_del,
	};
#ifdef HAVE_GLIB
	struct qb_ipcs_poll_handlers glib_ph = {
		.job_add = NULL, /* FIXME */
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
		case 'u':
			ipc_type = QB_IPC_SOCKET;
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

	qb_log_init("bms", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO + verbose);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	if (!use_glib) {
		bms_loop = qb_loop_create();
		s1 = qb_ipcs_create("bm1", 0, ipc_type, &sh);
		if (s1 == 0) {
			qb_perror(LOG_ERR, "qb_ipcs_create");
			exit(1);
		}
		qb_ipcs_poll_handlers_set(s1, &ph);
		rc = qb_ipcs_run(s1);
		if (rc != 0) {
			errno = -rc;
			qb_perror(LOG_ERR, "qb_ipcs_run");
			exit(1);
		}
		qb_loop_run(bms_loop);
	} else {
#ifdef HAVE_GLIB
		glib_loop = g_main_loop_new(NULL, FALSE);

		gio_map = qb_array_create(64, sizeof(struct gio_to_qb_poll));

		s1 = qb_ipcs_create("bm1", 0, ipc_type, &sh);
		if (s1 == 0) {
			qb_perror(LOG_ERR, "qb_ipcs_create");
			exit(1);
		}
		qb_ipcs_poll_handlers_set(s1, &glib_ph);
		rc = qb_ipcs_run(s1);
		if (rc != 0) {
			errno = -rc;
			qb_perror(LOG_ERR, "qb_ipcs_run");
			exit(1);
		}

		g_main_loop_run(glib_loop);
#else
		qb_log(LOG_ERR, "You don't seem to have glib-devel installed.\n");
#endif
	}
	return EXIT_SUCCESS;
}
