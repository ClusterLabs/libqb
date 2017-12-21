/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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
#include <qb/qbutil.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>
#include <qb/qbipcs.h>

#ifdef HAVE_GLIB
#include <glib.h>
static GMainLoop *glib_loop;
static qb_array_t *gio_map;
#endif /* HAVE_GLIB */

#define ONE_MEG 1048576

static int32_t use_glib = QB_FALSE;
static int32_t use_events = QB_FALSE;
static qb_loop_t *bms_loop;
static qb_ipcs_service_t *s1;

static int32_t
s1_connection_accept_fn(qb_ipcs_connection_t * c, uid_t uid, gid_t gid)
{
#if 0
	if (uid == 0 && gid == 0) {
		qb_log(LOG_INFO, "Authenticated connection");
		return 1;
	}
	qb_log(LOG_NOTICE, "BAD user!");
	return 0;
#else
	return 0;
#endif
}

static void
s1_connection_created_fn(qb_ipcs_connection_t * c)
{
	struct qb_ipcs_stats srv_stats;

	qb_ipcs_stats_get(s1, &srv_stats, QB_FALSE);
	qb_log(LOG_INFO, "Connection created (active:%d, closed:%d)",
	       srv_stats.active_connections, srv_stats.closed_connections);
}

static void
s1_connection_destroyed_fn(qb_ipcs_connection_t * c)
{
	qb_log(LOG_INFO, "Connection about to be freed");
}

static int32_t
s1_connection_closed_fn(qb_ipcs_connection_t * c)
{
	struct qb_ipcs_connection_stats stats;
	struct qb_ipcs_stats srv_stats;

	qb_ipcs_stats_get(s1, &srv_stats, QB_FALSE);
	qb_ipcs_connection_stats_get(c, &stats, QB_FALSE);
	qb_log(LOG_INFO,
	       "Connection to pid:%d destroyed (active:%d, closed:%d)",
	       stats.client_pid, srv_stats.active_connections,
	       srv_stats.closed_connections);

	qb_log(LOG_DEBUG, " Requests     %"PRIu64"", stats.requests);
	qb_log(LOG_DEBUG, " Responses    %"PRIu64"", stats.responses);
	qb_log(LOG_DEBUG, " Events       %"PRIu64"", stats.events);
	qb_log(LOG_DEBUG, " Send retries %"PRIu64"", stats.send_retries);
	qb_log(LOG_DEBUG, " Recv retries %"PRIu64"", stats.recv_retries);
	qb_log(LOG_DEBUG, " FC state     %d", stats.flow_control_state);
	qb_log(LOG_DEBUG, " FC count     %"PRIu64"", stats.flow_control_count);
	return 0;
}

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[256];
};

static int32_t
s1_msg_process_fn(qb_ipcs_connection_t * c, void *data, size_t size)
{
	struct qb_ipc_request_header *hdr;
	struct my_req *req_pt;
	struct qb_ipc_response_header response;
	ssize_t res;
	struct iovec iov[2];
	char resp[100];
	int32_t sl;
	int32_t send_ten_events = QB_FALSE;

	hdr = (struct qb_ipc_request_header *)data;
	if (hdr->id == (QB_IPC_MSG_USER_START + 1)) {
		return 0;
	}

	req_pt = (struct my_req *)data;
	qb_log(LOG_DEBUG, "msg received (id:%d, size:%d, data:%s)",
	       req_pt->hdr.id, req_pt->hdr.size, req_pt->message);

	if (strcmp(req_pt->message, "kill") == 0) {
		exit(0);
	}
	response.size = sizeof(struct qb_ipc_response_header);
	response.id = 13;
	response.error = 0;

	sl = snprintf(resp, 100, "ACK %zu bytes", size) + 1;
	iov[0].iov_len = sizeof(response);
	iov[0].iov_base = &response;
	iov[1].iov_len = sl;
	iov[1].iov_base = resp;
	response.size += sl;

	send_ten_events = (strcmp(req_pt->message, "events") == 0);

	if (use_events && !send_ten_events) {
		res = qb_ipcs_event_sendv(c, iov, 2);
	} else {
		res = qb_ipcs_response_sendv(c, iov, 2);
	}
	if (res < 0) {
		errno = - res;
		qb_perror(LOG_ERR, "qb_ipcs_response_send");
	}
	if (send_ten_events) {
		int32_t i;
		qb_log(LOG_INFO, "request to send 10 events");
		for (i = 0; i < 10; i++) {
			res = qb_ipcs_event_sendv(c, iov, 2);
			qb_log(LOG_INFO, "sent event %d res:%d", i, res);
		}
	}
	return 0;
}

static void
sigusr1_handler(int32_t num)
{
	qb_log(LOG_DEBUG, "(%d)", num);
	qb_ipcs_destroy(s1);
	exit(0);
}

static void
show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -h             show this help text\n");
	printf("  -m             use shared memory\n");
	printf("  -u             use unix sockets\n");
	printf("  -g             use glib mainloop\n");
	printf("  -e             use events\n");
	printf("\n");
}

#ifdef HAVE_GLIB
struct gio_to_qb_poll {
	int32_t is_used;
	int32_t events;
	int32_t source;
	int32_t fd;
	void *data;
	qb_ipcs_dispatch_fn_t fn;
	enum qb_loop_priority p;
};

static gboolean
gio_read_socket(GIOChannel * gio, GIOCondition condition, gpointer data)
{
	struct gio_to_qb_poll *adaptor = (struct gio_to_qb_poll *)data;
	gint fd = g_io_channel_unix_get_fd(gio);

	return (adaptor->fn(fd, condition, adaptor->data) == 0);
}

static void
gio_poll_destroy(gpointer data)
{
	struct gio_to_qb_poll *adaptor = (struct gio_to_qb_poll *)data;

	adaptor->is_used--;
	if (adaptor->is_used == 0) {
		qb_log(LOG_DEBUG, "fd %d adaptor destroyed\n", adaptor->fd);
		adaptor->fd = 0;
		adaptor->source = 0;
	}
}

static int32_t
my_g_dispatch_update(enum qb_loop_priority p, int32_t fd, int32_t evts,
		  void *data, qb_ipcs_dispatch_fn_t fn, gboolean is_new)
{
	struct gio_to_qb_poll *adaptor;
	GIOChannel *channel;
	int32_t res = 0;

	res = qb_array_index(gio_map, fd, (void **)&adaptor);
	if (res < 0) {
		return res;
	}
	if (adaptor->is_used && adaptor->source) {
		if (is_new) {
			return -EEXIST;
		}
		g_source_remove(adaptor->source);
		adaptor->source = 0;
	}

	channel = g_io_channel_unix_new(fd);
	if (!channel) {
		return -ENOMEM;
	}

	adaptor->fn = fn;
	adaptor->events = evts;
	adaptor->data = data;
	adaptor->p = p;
	adaptor->is_used++;
	adaptor->fd = fd;

	adaptor->source = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, evts, gio_read_socket, adaptor, gio_poll_destroy);

	/* we are handing the channel off to be managed by mainloop now.
	 * remove our reference. */
	g_io_channel_unref(channel);

	return 0;
}

static int32_t
my_g_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t evts,
		  void *data, qb_ipcs_dispatch_fn_t fn)
{
	return my_g_dispatch_update(p, fd, evts, data, fn, TRUE);
}

static int32_t
my_g_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
		  void *data, qb_ipcs_dispatch_fn_t fn)
{
	return my_g_dispatch_update(p, fd, evts, data, fn, FALSE);
}

static int32_t
my_g_dispatch_del(int32_t fd)
{
	struct gio_to_qb_poll *adaptor;
	if (qb_array_index(gio_map, fd, (void **)&adaptor) == 0) {
		g_source_remove(adaptor->source);
		adaptor->source = 0;
	}
	return 0;
}
#endif /* HAVE_GLIB */

static int32_t
my_job_add(enum qb_loop_priority p, void *data, qb_loop_job_dispatch_fn fn)
{
	return qb_loop_job_add(bms_loop, p, data, fn);
}

static int32_t
my_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t evts,
		void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_add(bms_loop, p, fd, evts, data, fn);
}

static int32_t
my_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
		void *data, qb_ipcs_dispatch_fn_t fn)
{
	return qb_loop_poll_mod(bms_loop, p, fd, evts, data, fn);
}

static int32_t
my_dispatch_del(int32_t fd)
{
	return qb_loop_poll_del(bms_loop, fd);
}

int32_t
main(int32_t argc, char *argv[])
{
	const char *options = "mpseugh";
	int32_t opt;
	int32_t rc;
	enum qb_ipc_type ipc_type = QB_IPC_NATIVE;
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
		case 'g':
			use_glib = QB_TRUE;
			break;
		case 'e':
			use_events = QB_TRUE;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}
	signal(SIGINT, sigusr1_handler);

	qb_log_init("ipcserver", LOG_USER, LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_format_set(QB_LOG_STDERR, "%f:%l [%p] %b");
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	s1 = qb_ipcs_create("ipcserver", 0, ipc_type, &sh);
	if (s1 == 0) {
		qb_perror(LOG_ERR, "qb_ipcs_create");
		exit(1);
	}
	/* This forces the clients to use a minimum buffer size */
	qb_ipcs_enforce_buffer_size(s1, ONE_MEG);

	if (!use_glib) {
		bms_loop = qb_loop_create();
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
		gio_map = qb_array_create_2(16, sizeof(struct gio_to_qb_poll), 1);
		qb_ipcs_poll_handlers_set(s1, &glib_ph);
		rc = qb_ipcs_run(s1);
		if (rc != 0) {
			errno = -rc;
			qb_perror(LOG_ERR, "qb_ipcs_run");
			exit(1);
		}
		g_main_loop_run(glib_loop);
#else
		qb_log(LOG_ERR,
		       "You don't seem to have glib-devel installed.\n");
#endif
	}
	qb_log_fini();
	return EXIT_SUCCESS;
}
