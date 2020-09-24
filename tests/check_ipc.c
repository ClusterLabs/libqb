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
#include <sys/un.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>

#ifdef HAVE_GLIB
#include <glib.h>
#endif

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbipcc.h>
#include <qb/qbipcs.h>
#include <qb/qbloop.h>

#ifdef HAVE_FAILURE_INJECTION
#include "_failure_injection.h"
#endif

#define NUM_STRESS_CONNECTIONS 5000

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

static int enforce_server_buffer;
static qb_ipcc_connection_t *conn;
static enum qb_ipc_type ipc_type;
static enum qb_loop_priority global_loop_prio = QB_LOOP_MED;
static bool global_use_glib;
static int global_pipefd[2];

enum my_msg_ids {
	IPC_MSG_REQ_TX_RX,
	IPC_MSG_RES_TX_RX,
	IPC_MSG_REQ_DISPATCH,
	IPC_MSG_RES_DISPATCH,
	IPC_MSG_REQ_BULK_EVENTS,
	IPC_MSG_RES_BULK_EVENTS,
	IPC_MSG_REQ_STRESS_EVENT,
	IPC_MSG_RES_STRESS_EVENT,
	IPC_MSG_REQ_SELF_FEED,
	IPC_MSG_RES_SELF_FEED,
	IPC_MSG_REQ_SERVER_FAIL,
	IPC_MSG_RES_SERVER_FAIL,
	IPC_MSG_REQ_SERVER_DISCONNECT,
	IPC_MSG_RES_SERVER_DISCONNECT,
};


/* these 2 functions from pacemaker code */
static enum qb_ipcs_rate_limit
conv_libqb_prio2ratelimit(enum qb_loop_priority prio)
{
	/* this is an inversion of what libqb's qb_ipcs_request_rate_limit does */
	enum qb_ipcs_rate_limit ret = QB_IPCS_RATE_NORMAL;
	switch (prio) {
	case QB_LOOP_LOW:
		ret = QB_IPCS_RATE_SLOW;
		break;
	case QB_LOOP_HIGH:
		ret = QB_IPCS_RATE_FAST;
		break;
	default:
		qb_log(LOG_DEBUG, "Invalid libqb's loop priority %d,"
		       " assuming QB_LOOP_MED", prio);
		/* fall-through */
	case QB_LOOP_MED:
		break;
	}
	return ret;
}
#ifdef HAVE_GLIB
static gint
conv_prio_libqb2glib(enum qb_loop_priority prio)
{
	gint ret = G_PRIORITY_DEFAULT;
	switch (prio) {
	case QB_LOOP_LOW:
		ret = G_PRIORITY_LOW;
		break;
	case QB_LOOP_HIGH:
		ret = G_PRIORITY_HIGH;
		break;
	default:
		qb_log(LOG_DEBUG, "Invalid libqb's loop priority %d,"
		       " assuming QB_LOOP_MED", prio);
		/* fall-through */
	case QB_LOOP_MED:
		break;
	}
	return ret;
}

/* these 3 glue functions inspired from pacemaker, too */
static gboolean
gio_source_prepare(GSource *source, gint *timeout)
{
	qb_enter();
	*timeout = 500;
	return FALSE;
}
static gboolean
gio_source_check(GSource *source)
{
	qb_enter();
	return TRUE;
}
static gboolean
gio_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
	gboolean ret = G_SOURCE_CONTINUE;
	qb_enter();
	if (callback) {
		ret = callback(user_data);
	}
	return ret;
}
static GSourceFuncs gio_source_funcs = {
    .prepare = gio_source_prepare,
    .check = gio_source_check,
    .dispatch = gio_source_dispatch,
};

#endif


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
 *
 * 9) setting perms on the sockets
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
static int32_t set_perms_on_socket = QB_FALSE;


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
	FILE *f;
	char process_name[256];

	/* The process-unique part of the IPC name has already been decided
	 * and stored in the file ipc-test-name.
	 */
	f = fopen("ipc-test-name", "r");
	if (f) {
		fgets(process_name, sizeof(process_name), f);
		fclose(f);
		snprintf(ipc_name, sizeof(ipc_name), "%.44s%s", prefix, process_name);
	} else {
		/* This is the old code, use only as a fallback */
		static char t_sec[3] = "";
		if (t_sec[0] == '\0') {
			const char *const found = strrchr(__TIME__, ':');
			strncpy(t_sec, found ? found + 1 : "-", sizeof(t_sec) - 1);
			t_sec[sizeof(t_sec) - 1] = '\0';
		}

		snprintf(ipc_name, sizeof(ipc_name), "%.44s%s%lX%.4x", prefix, t_sec,
			 (unsigned long)getpid(), (unsigned) ((long) time(NULL) % (0x10000)));
	}
}

static int
pipe_writer(int fd, int revents, void *data) {
	qb_enter();
	static const char buf[8] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h' };

	ssize_t wbytes = 0, wbytes_sum = 0;

	//for (size_t i = 0; i < SIZE_MAX; i++) {
	for (size_t i = 0; i < 4096; i++) {
		wbytes_sum += wbytes;
		if ((wbytes = write(fd, buf, sizeof(buf))) == -1) {
			if (errno != EAGAIN) {
				perror("write");
				exit(-1);
			}
			break;
		}
	}
	if (wbytes_sum > 0) {
		qb_log(LOG_DEBUG, "written %zd bytes", wbytes_sum);
	}
	qb_leave();
	return 1;
}

static int
pipe_reader(int fd, int revents, void *data) {
	qb_enter();
	ssize_t rbytes, rbytes_sum = 0;
	size_t cnt = SIZE_MAX;
	char buf[4096] = { '\0' };
	while ((rbytes = read(fd, buf, sizeof(buf))) > 0 && rbytes < cnt) {
		cnt -= rbytes;
		rbytes_sum += rbytes;
	}
	if (rbytes_sum > 0) {
		ck_assert(buf[0] != '\0');  /* avoid dead store elimination */
		qb_log(LOG_DEBUG, "read %zd bytes", rbytes_sum);
		sleep(1);
	}
	qb_leave();
	return 1;
}

#if HAVE_GLIB
static gboolean
gio_pipe_reader(void *data) {
	return (pipe_reader(*((int *) data), 0, NULL) > 0);
}
static gboolean
gio_pipe_writer(void *data) {
	return (pipe_writer(*((int *) data), 0, NULL) > 0);
}
#endif

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

	} else if (req_pt->id == IPC_MSG_REQ_SELF_FEED) {
		if (pipe(global_pipefd) != 0) {
			perror("pipefd");
			ck_assert(0);
		}
		fcntl(global_pipefd[0], F_SETFL, O_NONBLOCK);
		fcntl(global_pipefd[1], F_SETFL, O_NONBLOCK);
		if (global_use_glib) {
#ifdef HAVE_GLIB
			GSource *source_r, *source_w;
			source_r = g_source_new(&gio_source_funcs, sizeof(GSource));
			source_w = g_source_new(&gio_source_funcs, sizeof(GSource));
			ck_assert(source_r != NULL && source_w != NULL);
			g_source_set_priority(source_r, conv_prio_libqb2glib(QB_LOOP_HIGH));
			g_source_set_priority(source_w, conv_prio_libqb2glib(QB_LOOP_HIGH));
			g_source_set_can_recurse(source_r, FALSE);
			g_source_set_can_recurse(source_w, FALSE);
			g_source_set_callback(source_r, gio_pipe_reader, &global_pipefd[0], NULL);
			g_source_set_callback(source_w, gio_pipe_writer, &global_pipefd[1], NULL);
			g_source_add_unix_fd(source_r, global_pipefd[0], G_IO_IN);
			g_source_add_unix_fd(source_w, global_pipefd[1], G_IO_OUT);
			g_source_attach(source_r, NULL);
			g_source_attach(source_w, NULL);
#else
			ck_assert(0);
#endif
		} else {
			qb_loop_poll_add(my_loop, QB_LOOP_HIGH, global_pipefd[1],
			                 POLLOUT|POLLERR, NULL, pipe_writer);
			qb_loop_poll_add(my_loop, QB_LOOP_HIGH, global_pipefd[0],
			                 POLLIN|POLLERR, NULL, pipe_reader);
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


/* taken from examples/ipcserver.c, with s/my_g/gio/ */
#ifdef HAVE_GLIB

#include <qb/qbarray.h>

static qb_array_t *gio_map;
static GMainLoop *glib_loop;

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

	qb_enter();

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
gio_dispatch_update(enum qb_loop_priority p, int32_t fd, int32_t evts,
                    void *data, qb_ipcs_dispatch_fn_t fn, gboolean is_new)
{
	struct gio_to_qb_poll *adaptor;
	GIOChannel *channel;
	int32_t res = 0;

	qb_enter();

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

	adaptor->source = g_io_add_watch_full(channel, conv_prio_libqb2glib(p),
	                                      evts, gio_read_socket, adaptor,
	                                      gio_poll_destroy);

	/* we are handing the channel off to be managed by mainloop now.
	 * remove our reference. */
	g_io_channel_unref(channel);

	return 0;
}

static int32_t
gio_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t evts,
                 void *data, qb_ipcs_dispatch_fn_t fn)
{
	return gio_dispatch_update(p, fd, evts, data, fn, TRUE);
}

static int32_t
gio_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
                 void *data, qb_ipcs_dispatch_fn_t fn)
{
	return gio_dispatch_update(p, fd, evts, data, fn, FALSE);
}

static int32_t
gio_dispatch_del(int32_t fd)
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
s1_connection_closed(qb_ipcs_connection_t *c)
{
	if (multiple_connections) {
		return 0;
	}
	/* Stop the connection being freed when we call qb_ipcs_disconnect()
	   in the callback */
	if (disconnect_after_created == QB_TRUE) {
		disconnect_after_created = QB_FALSE;
		return 1;
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

static int32_t
s1_connection_accept(qb_ipcs_connection_t *c, uid_t uid, gid_t gid)
{
	if (set_perms_on_socket) {
		qb_ipcs_connection_auth_set(c, 555, 741, S_IRWXU|S_IRWXG|S_IROTH|S_IWOTH);
	}
	return 0;
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

static volatile sig_atomic_t usr1_bit;

static void usr1_bit_setter(int signal) {
    if (signal == SIGUSR1) {
        usr1_bit = 1;
    }
}

#define READY_SIGNALLER(name, data_arg)  void (name)(void *data_arg)
typedef READY_SIGNALLER(ready_signaller_fn, );

static
READY_SIGNALLER(usr1_signaller, parent_target)
{
	kill(*((pid_t *) parent_target), SIGUSR1);
}

#define NEW_PROCESS_RUNNER(name, ready_signaller_arg, signaller_data_arg, data_arg) \
	void (name)(ready_signaller_fn ready_signaller_arg, \
	      void *signaller_data_arg, void *data_arg)
typedef NEW_PROCESS_RUNNER(new_process_runner_fn, , , );

static
NEW_PROCESS_RUNNER(run_ipc_server, ready_signaller, signaller_data, data)
{
	int32_t res;
	qb_loop_signal_handle handle;

	struct qb_ipcs_service_handlers sh = {
		.connection_accept = s1_connection_accept,
		.connection_created = s1_connection_created,
		.msg_process = s1_msg_process_fn,
		.connection_destroyed = s1_connection_destroyed,
		.connection_closed = s1_connection_closed,
	};

	struct qb_ipcs_poll_handlers ph;
	uint32_t max_size = MAX_MSG_SIZE;

	my_loop = qb_loop_create();
	qb_loop_signal_add(my_loop, QB_LOOP_HIGH, SIGTERM,
	                   NULL, exit_handler, &handle);


	s1 = qb_ipcs_create(ipc_name, 4, ipc_type, &sh);
	ck_assert(s1 != 0);

	if (global_loop_prio != QB_LOOP_MED) {
		qb_ipcs_request_rate_limit(s1,
		                           conv_libqb_prio2ratelimit(global_loop_prio));
	}
	if (global_use_glib) {
#ifdef HAVE_GLIB
		ph = (struct qb_ipcs_poll_handlers) {
			.job_add = NULL,
			.dispatch_add = gio_dispatch_add,
			.dispatch_mod = gio_dispatch_mod,
			.dispatch_del = gio_dispatch_del,
		};
		glib_loop = g_main_loop_new(NULL, FALSE);
		gio_map = qb_array_create_2(16, sizeof(struct gio_to_qb_poll), 1);
		ck_assert(gio_map != NULL);
#else
		ck_assert(0);
#endif
	} else {
		ph = (struct qb_ipcs_poll_handlers) {
			.job_add = my_job_add,
			.dispatch_add = my_dispatch_add,
			.dispatch_mod = my_dispatch_mod,
			.dispatch_del = my_dispatch_del,
		};
	}

	if (enforce_server_buffer) {
		qb_ipcs_enforce_buffer_size(s1, max_size);
	}
	qb_ipcs_poll_handlers_set(s1, &ph);

	res = qb_ipcs_run(s1);
	ck_assert_int_eq(res, 0);

	if (ready_signaller != NULL) {
		ready_signaller(signaller_data);
	}

	if (global_use_glib) {
#ifdef HAVE_GLIB
		g_main_loop_run(glib_loop);
#endif
	} else {
		qb_loop_run(my_loop);
	}
	qb_log(LOG_DEBUG, "loop finished - done ...");
}

static pid_t
run_function_in_new_process(const char *role,
                            new_process_runner_fn new_process_runner,
                            void *data)
{
	char formatbuf[1024];
	pid_t parent_target, pid1, pid2;
	struct sigaction orig_sa, purpose_sa;
	sigset_t orig_mask, purpose_mask, purpose_clear_mask;

	sigemptyset(&purpose_mask);
	sigaddset(&purpose_mask, SIGUSR1);

	sigprocmask(SIG_BLOCK, &purpose_mask, &orig_mask);
	purpose_clear_mask = orig_mask;
	sigdelset(&purpose_clear_mask, SIGUSR1);

	purpose_sa.sa_handler = usr1_bit_setter;
	purpose_sa.sa_mask = purpose_mask;
	purpose_sa.sa_flags = SA_RESTART;

	/* Double-fork so the servers can be reaped in a timely manner */
	parent_target = getpid();
	pid1 = fork();
	if (pid1 == 0) {
		pid2 = fork();
		if (pid2 == -1) {
			fprintf (stderr, "Can't fork twice\n");
			exit(0);
		}
		if (pid2 == 0) {
			sigprocmask(SIG_SETMASK, &orig_mask, NULL);

			if (role == NULL) {
				qb_log_format_set(QB_LOG_STDERR, "lib/%f|%l[%P] %b");
			} else {
				snprintf(formatbuf, sizeof(formatbuf),
				         "lib/%%f|%%l|%s[%%P] %%b", role);
				qb_log_format_set(QB_LOG_STDERR, formatbuf);
			}

			new_process_runner(usr1_signaller, &parent_target, data);
			exit(0);
		} else {
			waitpid(pid2, NULL, 0);
			exit(0);
		}
	}

	usr1_bit = 0;
	/* XXX assume never fails */
	sigaction(SIGUSR1, &purpose_sa, &orig_sa);

	do {
		/* XXX assume never fails with EFAULT */
		sigsuspend(&purpose_clear_mask);
	} while (usr1_bit != 1);
	usr1_bit = 0;
	sigprocmask(SIG_SETMASK, &orig_mask, NULL);
	/* give children a slight/non-strict scheduling advantage */
	sched_yield();

	return pid1;
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
		ck_assert(rc != 0);
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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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
test_ipc_getauth(void)
{
	int32_t j;
	int32_t c = 0;
	pid_t pid;
	pid_t spid;
	uid_t suid;
	gid_t sgid;
	int res;
	uint32_t max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

	res = qb_ipcc_auth_get(NULL, NULL, NULL, NULL);
	ck_assert(res == -EINVAL);

	res = qb_ipcc_auth_get(conn, &spid, &suid, &sgid);
	ck_assert(res == 0);
#ifndef HAVE_GETPEEREID
	/* GETPEEREID doesn't return a PID */
	ck_assert(spid != 0);
#endif
	ck_assert(suid == getuid());
	ck_assert(sgid == getgid());

	request_server_exit();
	qb_ipcc_disconnect(conn);
	verify_graceful_stop(pid);
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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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


START_TEST(test_ipc_txrx_shm_getauth)
{
	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);
	test_ipc_getauth();
	qb_leave();
}
END_TEST

START_TEST(test_ipc_txrx_us_getauth)
{
	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_getauth();
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

struct dispatch_data {
	pid_t server_pid;
	enum my_msg_ids msg_type;
	uint32_t repetitions;
};

static inline
NEW_PROCESS_RUNNER(client_dispatch, ready_signaller, signaller_data, data)
{
	uint32_t max_size = MAX_MSG_SIZE;
	int32_t size;
	int32_t c = 0;
	int32_t j;
	pid_t server_pid = ((struct dispatch_data *) data)->server_pid;
	enum my_msg_ids msg_type = ((struct dispatch_data *) data)->msg_type;

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(server_pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

	if (ready_signaller != NULL) {
		ready_signaller(signaller_data);
	}

	size = QB_MIN(sizeof(struct qb_ipc_request_header), 64);

	for (uint32_t r = ((struct dispatch_data *) data)->repetitions;
			r > 0; r--) {
		for (j = 1; j < 19; j++) {
			size *= 2;
			if (size >= max_size)
				break;
			if (send_and_check(msg_type, size,
					   recv_timeout, QB_TRUE) < 0) {
				break;
			}
		}
	}
}

static void
test_ipc_dispatch(void)
{
	pid_t pid;
	struct dispatch_data data;

	pid = run_function_in_new_process(NULL, run_ipc_server, NULL);
	ck_assert(pid != -1);
	data = (struct dispatch_data){.server_pid = pid,
	                              .msg_type = IPC_MSG_REQ_DISPATCH,
	                              .repetitions = 1};

	client_dispatch(NULL, NULL, (void *) &data);

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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	for (connections = 1; connections < NUM_STRESS_CONNECTIONS; connections++) {
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
		ck_assert(conn != NULL);

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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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
	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	enforce_server_buffer = 0;
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, client_buf_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);
	test_ipc_bulk_events();
	qb_leave();
}
END_TEST

static
READY_SIGNALLER(connected_signaller, _)
{
	request_server_exit();
}

START_TEST(test_ipc_dispatch_us_native_prio_dlock)
{
	pid_t server_pid, alphaclient_pid;
	struct dispatch_data data;

	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);

	/* this is to demonstrate that native event loop can deal even
	   with "extreme" priority disproportions */
	global_loop_prio = QB_LOOP_LOW;
	multiple_connections = QB_TRUE;
	recv_timeout = -1;

	server_pid = run_function_in_new_process("server", run_ipc_server,
	                                         NULL);
	ck_assert(server_pid != -1);
	data = (struct dispatch_data){.server_pid = server_pid,
	                              .msg_type = IPC_MSG_REQ_SELF_FEED,
	                              .repetitions = 1};
	alphaclient_pid = run_function_in_new_process("alphaclient",
	                                              client_dispatch,
	                                              (void *) &data);
	ck_assert(alphaclient_pid != -1);

	//sleep(1);
	sched_yield();

	data.repetitions = 0;
	client_dispatch(connected_signaller, NULL, (void *) &data);
	verify_graceful_stop(server_pid);

	multiple_connections = QB_FALSE;
	qb_leave();
}
END_TEST

#if HAVE_GLIB
START_TEST(test_ipc_dispatch_us_glib_prio_dlock)
{
	pid_t server_pid, alphaclient_pid;
	struct dispatch_data data;

	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);

	global_use_glib = QB_TRUE;
	/* this is to make the test pass at all, since GLib is strict
	   on priorities -- QB_LOOP_MED or lower would fail for sure */
	global_loop_prio = QB_LOOP_HIGH;
	multiple_connections = QB_TRUE;
	recv_timeout = -1;

	server_pid = run_function_in_new_process("server", run_ipc_server,
	                                         NULL);
	ck_assert(server_pid != -1);
	data = (struct dispatch_data){.server_pid = server_pid,
	                              .msg_type = IPC_MSG_REQ_SELF_FEED,
	                              .repetitions = 1};
	alphaclient_pid = run_function_in_new_process("alphaclient",
	                                              client_dispatch,
	                                              (void *) &data);
	ck_assert(alphaclient_pid != -1);

	//sleep(1);
	sched_yield();

	data.repetitions = 0;
	client_dispatch(connected_signaller, NULL, (void *) &data);
	verify_graceful_stop(server_pid);

	multiple_connections = QB_FALSE;
	global_loop_prio = QB_LOOP_MED;
	global_use_glib = QB_FALSE;
	qb_leave();
}
END_TEST
#endif

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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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

// Check perms uses illegal access to libqb internals
// DO NOT try this at home.
#include "../lib/ipc_int.h"
#include "../lib/ringbuffer_int.h"
START_TEST(test_ipc_server_perms)
{
	pid_t pid;
	struct stat st;
	int j;
	uint32_t max_size;
	int res;
	int c = 0;

	// Can only test this if we are root
	if (getuid() != 0) {
		return;
	}

	ipc_type = QB_IPC_SHM;
	set_perms_on_socket = QB_TRUE;
	max_size = MAX_MSG_SIZE;

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

	/* Check perms - uses illegal access to libqb internals */

	/* BSD uses /var/run for sockets so we can't alter the perms on the
	   directory */
#ifdef __linux__
	char sockdir[PATH_MAX];
	strcpy(sockdir, conn->request.u.shm.rb->shared_hdr->hdr_path);
	*strrchr(sockdir, '/') = 0;

	res = stat(sockdir, &st);

	ck_assert_int_eq(res, 0);
	ck_assert(st.st_mode & S_IRWXG);
	ck_assert_int_eq(st.st_uid, 555);
	ck_assert_int_eq(st.st_gid, 741);
#endif

	res = stat(conn->request.u.shm.rb->shared_hdr->hdr_path, &st);
	ck_assert_int_eq(res, 0);
	ck_assert_int_eq(st.st_uid, 555);
	ck_assert_int_eq(st.st_gid, 741);

	qb_ipcc_disconnect(conn);
	verify_graceful_stop(pid);
}
END_TEST

START_TEST(test_ipc_dispatch_shm_native_prio_dlock)
{
	pid_t server_pid, alphaclient_pid;
	struct dispatch_data data;

	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);

	/* this is to demonstrate that native event loop can deal even
	   with "extreme" priority disproportions */
	global_loop_prio = QB_LOOP_LOW;
	multiple_connections = QB_TRUE;
	recv_timeout = -1;

	server_pid = run_function_in_new_process("server", run_ipc_server,
	                                         NULL);
	ck_assert(server_pid != -1);
	data = (struct dispatch_data){.server_pid = server_pid,
	                              .msg_type = IPC_MSG_REQ_SELF_FEED,
	                              .repetitions = 1};
	alphaclient_pid = run_function_in_new_process("alphaclient",
	                                              client_dispatch,
	                                              (void *) &data);
	ck_assert(alphaclient_pid != -1);

	//sleep(1);
	sched_yield();

	data.repetitions = 0;
	client_dispatch(connected_signaller, NULL, (void *) &data);
	verify_graceful_stop(server_pid);

	multiple_connections = QB_FALSE;
	qb_leave();
}
END_TEST

#if HAVE_GLIB
START_TEST(test_ipc_dispatch_shm_glib_prio_dlock)
{
	pid_t server_pid, alphaclient_pid;
	struct dispatch_data data;

	qb_enter();
	ipc_type = QB_IPC_SOCKET;
	set_ipc_name(__func__);

	global_use_glib = QB_TRUE;
	/* this is to make the test pass at all, since GLib is strict
	   on priorities -- QB_LOOP_MED or lower would fail for sure */
	global_loop_prio = QB_LOOP_HIGH;
	multiple_connections = QB_TRUE;
	recv_timeout = -1;

	server_pid = run_function_in_new_process("server", run_ipc_server,
	                                         NULL);
	ck_assert(server_pid != -1);
	data = (struct dispatch_data){.server_pid = server_pid,
	                              .msg_type = IPC_MSG_REQ_SELF_FEED,
	                              .repetitions = 1};
	alphaclient_pid = run_function_in_new_process("alphaclient",
	                                              client_dispatch,
	                                              (void *) &data);
	ck_assert(alphaclient_pid != -1);

	//sleep(1);
	sched_yield();

	data.repetitions = 0;
	client_dispatch(connected_signaller, NULL, (void *) &data);
	verify_graceful_stop(server_pid);

	multiple_connections = QB_FALSE;
	global_loop_prio = QB_LOOP_MED;
	global_use_glib = QB_FALSE;
	qb_leave();
}
END_TEST
#endif

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
	char sock_file[PATH_MAX];
	struct sockaddr_un socka;

	qb_enter();
	ipc_type = QB_IPC_SHM;
	set_ipc_name(__func__);

	sprintf(sock_file, "%s/%s", SOCKETDIR, ipc_name);
	sock_file[sizeof(socka.sun_path)] = '\0';

	/* If there's an old socket left from a previous run this test will fail
	   unexpectedly, so try to remove it first */
	unlink(sock_file);

	_fi_unlink_inject_failure = QB_TRUE;
	test_ipc_server_fail();
	_fi_unlink_inject_failure = QB_FALSE;
	unlink(sock_file);
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

	pid = run_function_in_new_process("server", run_ipc_server, NULL);
	ck_assert(pid != -1);

	do {
		conn = qb_ipcc_connect(ipc_name, max_size);
		if (conn == NULL) {
			j = waitpid(pid, NULL, WNOHANG);
			ck_assert_int_eq(j, 0);
			(void)poll(NULL, 0, 400);
			c++;
		}
	} while (conn == NULL && c < 5);
	ck_assert(conn != NULL);

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

#if 0
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
	ck_assert(init > 0);
	for (i = 0; i < 100; i++) {
		int try = qb_ipcc_verify_dgram_max_msg_size(1000000);
#if 0
		ck_assert_int_eq(init, try);
#else
		/* extra troubleshooting, report also on i and errno variables;
		   related: https://github.com/ClusterLabs/libqb/issues/234 */
		if (init != try) {
#ifdef ci_dump_shm_usage
			system("df -h | grep -e /shm >/tmp/_shm_usage");
#endif
			ck_abort_msg("Assertion 'init==try' failed:"
				     " init==%#x, try==%#x, i=%d, errno=%d",
				     init, try, i, errno);
		}
#endif
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
#endif

static Suite *
make_shm_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("shm");

	add_tcase(s, tc, test_ipc_txrx_shm_getauth, 7);
	add_tcase(s, tc, test_ipc_txrx_shm_timeout, 28);
	add_tcase(s, tc, test_ipc_server_fail_shm, 7);
	add_tcase(s, tc, test_ipc_txrx_shm_block, 7);
	add_tcase(s, tc, test_ipc_txrx_shm_tmo, 7);
	add_tcase(s, tc, test_ipc_fc_shm, 7);
	add_tcase(s, tc, test_ipc_dispatch_shm, 15);
	add_tcase(s, tc, test_ipc_stress_test_shm, 15);
	add_tcase(s, tc, test_ipc_bulk_events_shm, 15);
	add_tcase(s, tc, test_ipc_exit_shm, 6);
	add_tcase(s, tc, test_ipc_event_on_created_shm, 9);
	add_tcase(s, tc, test_ipc_service_ref_count_shm, 9);
	add_tcase(s, tc, test_ipc_server_perms, 7);
	add_tcase(s, tc, test_ipc_stress_connections_shm, 3600 /* ? */);
	add_tcase(s, tc, test_ipc_dispatch_shm_native_prio_dlock, 15);
#if HAVE_GLIB
	add_tcase(s, tc, test_ipc_dispatch_shm_glib_prio_dlock, 15);
#endif
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

	add_tcase(s, tc, test_ipc_txrx_us_getauth, 7);
	add_tcase(s, tc, test_ipc_txrx_us_timeout, 28);
/* Commented out for the moment as space in /dev/shm on the CI machines
   causes random failures */
/*	add_tcase(s, tc, test_ipc_max_dgram_size, 30); */
	add_tcase(s, tc, test_ipc_server_fail_soc, 7);
	add_tcase(s, tc, test_ipc_txrx_us_block, 7);
	add_tcase(s, tc, test_ipc_txrx_us_tmo, 7);
	add_tcase(s, tc, test_ipc_fc_us, 7);
	add_tcase(s, tc, test_ipc_exit_us, 6);
	add_tcase(s, tc, test_ipc_dispatch_us, 15);
#ifndef __clang__ /* see variable length array in structure' at the top */
	add_tcase(s, tc, test_ipc_stress_test_us, 58);
#endif
	add_tcase(s, tc, test_ipc_bulk_events_us, 15);
	add_tcase(s, tc, test_ipc_event_on_created_us, 9);
	add_tcase(s, tc, test_ipc_disconnect_after_created_us, 9);
	add_tcase(s, tc, test_ipc_service_ref_count_us, 9);
	add_tcase(s, tc, test_ipc_stress_connections_us, 3600 /* ? */);
	add_tcase(s, tc, test_ipc_dispatch_us_native_prio_dlock, 15);
#if HAVE_GLIB
	add_tcase(s, tc, test_ipc_dispatch_us_glib_prio_dlock, 15);
#endif

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
	atexit(qb_log_fini);
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
