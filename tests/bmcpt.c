/*
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbipcc.h>

#define ITERATIONS 10000000
#define THREADS 4

struct bm_ctx {
	qb_ipcc_connection_t *conn;
	qb_util_stopwatch_t *sw;
	float mbs;
	float secs;
	int32_t multi;
	uint32_t counter;
};

static void bm_start(struct bm_ctx *ctx)
{
	qb_util_stopwatch_start(ctx->sw);
}

static void bm_finish(struct bm_ctx *ctx, const char *operation, int32_t size)
{
	qb_util_stopwatch_stop(ctx->sw);
	ctx->secs = qb_util_stopwatch_sec_elapsed_get(ctx->sw);

	ctx->mbs =
	    ((((float)ctx->counter) * size) / ctx->secs) / (1024.0 *
							    1024.0);
}

static void bmc_connect(struct bm_ctx *ctx)
{
	ctx->sw = qb_util_stopwatch_create();
	ctx->conn = qb_ipcc_connect("bm1", QB_MAX(1000 * (100 + THREADS),
						  1024*1024));
	if (ctx->conn == NULL) {
		perror("qb_ipcc_connect");
		exit(-1);
	}
}

static void bmc_disconnect(struct bm_ctx *ctx)
{
	qb_ipcc_disconnect(ctx->conn);
	qb_util_stopwatch_free(ctx->sw);
}

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[1024 * 1024];
};

static struct my_req request;

static int32_t bmc_send_nozc(struct bm_ctx *ctx, uint32_t size)
{
	struct qb_ipc_response_header res_header;
	int32_t res;

	request.hdr.id = QB_IPC_MSG_USER_START + 3;
	request.hdr.size = sizeof(struct qb_ipc_request_header) + size;

repeat_send:
	res = qb_ipcc_send(ctx->conn, &request, request.hdr.size);
	if (res < 0) {
		if (res == -EAGAIN) {
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

	res = qb_ipcc_recv(ctx->conn, &res_header,
			   sizeof(struct qb_ipc_response_header), -1);
	if (res == -EINTR) {
		return -1;
	}
	if (res < 0) {
		perror("qb_ipcc_recv");
	}
	assert(res == sizeof(struct qb_ipc_response_header));
	assert(res_header.id == 13);
	assert(res_header.size == sizeof(struct qb_ipc_response_header));
	return 0;
}

uint32_t alarm_notice = 0;
static void sigalrm_handler(int32_t num)
{
	alarm_notice = 1;
}

static void *benchmark(void *ctx)
{
	struct bm_ctx *bm_ctx = (struct bm_ctx *)ctx;
	int32_t res;

	bmc_connect(bm_ctx);

	bm_start(bm_ctx);
	for (;;) {
		bm_ctx->counter++;
		res = bmc_send_nozc(bm_ctx, 1000 * bm_ctx->multi);
		if (alarm_notice || res == -1) {
			bm_finish(bm_ctx, "send_nozc", 1000 * bm_ctx->multi);
			bmc_disconnect(bm_ctx);
			return (NULL);
		}
	}
}


int32_t main(void)
{
	struct bm_ctx bm_ctx[THREADS];
	pthread_t threads[THREADS];
	pthread_attr_t thread_attr[THREADS];
	int32_t i, j;
	float total_mbs;
	void *retval;

	for (i = 0; i < THREADS; i++) {
		bm_ctx[i].mbs = 0;
	}
	signal(SIGALRM, sigalrm_handler);
	for (j = 0; j < 500; j++) {
		alarm_notice = 0;
		alarm(3);
		for (i = 0; i < THREADS; i++) {
			bm_ctx[i].multi = j + 100;
			bm_ctx[i].counter = 0;
			pthread_attr_init(&thread_attr[i]);

			pthread_attr_setdetachstate(&thread_attr[i],
						    PTHREAD_CREATE_JOINABLE);
			pthread_create(&threads[i], &thread_attr[i], benchmark,
				       &bm_ctx[i]);
		}
		for (i = 0; i < THREADS; i++) {
			pthread_join(threads[i], &retval);
		}
		total_mbs = 0;
		for (i = 0; i < THREADS; i++) {
			total_mbs = total_mbs + bm_ctx[i].mbs;
		}
		printf("%d ", 1000 * bm_ctx[0].multi);
		printf("%9.3f\n", total_mbs);
	}
	return EXIT_SUCCESS;
}
