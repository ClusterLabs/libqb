/*
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <qb/qbipcc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

#define ITERATIONS 10000000

struct bm_ctx {
	qb_hdb_handle_t bmc_ipc_handle;
	struct timeval tv1;
	struct timeval tv2;
	struct timeval tv_elapsed;
	float mbs;
	int multi;
	unsigned int counter;
};

#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)

static void bm_start (struct bm_ctx *ctx)
{
        gettimeofday (&ctx->tv1, NULL);
}
static void bm_finish (struct bm_ctx *ctx, const char *operation, int size)
{
	float ops_per_sec;
	float mbs_per_sec;

        gettimeofday (&ctx->tv2, NULL);
        timersub (&ctx->tv2, &ctx->tv1, &ctx->tv_elapsed);

	ops_per_sec = 
                ((float)ctx->counter) / (((float)ctx->tv_elapsed.tv_sec) + (((float)ctx->tv_elapsed.tv_usec) / 1000000.0));

	mbs_per_sec = 
                ((((float)ctx->counter) * size) / (((float)ctx->tv_elapsed.tv_sec) + (((float)ctx->tv_elapsed.tv_usec) / 1000000.0))) / (1024.0*1024.0);


	ctx->mbs = ops_per_sec;
}

static void bmc_connect (struct bm_ctx *ctx)
{
	unsigned int res;

	res = qb_ipcc_service_connect ("qb_ipcs_bm",
		0,
		8192*128,
		8192*128,
		8192*128,
		&ctx->bmc_ipc_handle);
}

static void bmc_disconnect (struct bm_ctx *ctx)
{
	qb_ipcc_service_disconnect (ctx->bmc_ipc_handle);
}

static char buffer[1024*1024];
static void bmc_send_nozc (struct bm_ctx *ctx, unsigned int size)
{
	struct iovec iov[2];
	qb_ipc_request_header_t req_header;
	qb_ipc_response_header_t res_header;
	int res;

	req_header.id = 0;
	req_header.size = sizeof (qb_ipc_request_header_t) + size;
	
	iov[0].iov_base = &req_header;
	iov[0].iov_len = sizeof (qb_ipc_request_header_t);
	iov[1].iov_base = buffer;
	iov[1].iov_len = size;

repeat_send:
	res = qb_ipcc_msg_send_reply_receive (
		ctx->bmc_ipc_handle,
		iov,
		2,
		&res_header,
		sizeof (qb_ipc_response_header_t));
	if (res != 0) {
		goto repeat_send;
	}
}

unsigned int alarm_notice = 0;
static void sigalrm_handler (int num)
{
	alarm_notice = 1;
}

static void *benchmark (void *ctx) {
	struct bm_ctx *bm_ctx = (struct bm_ctx *)ctx;

	bmc_connect(bm_ctx);

	bm_start(bm_ctx);
	for (;;) {
		bm_ctx->counter++;
		bmc_send_nozc (bm_ctx, 1000 * bm_ctx->multi);
		if (alarm_notice) {
			bm_finish (bm_ctx, "send_nozc", 1000 * bm_ctx->multi);
			bmc_disconnect (bm_ctx);
			return (NULL);
		}
	}
}

#define THREADS 4

int main (void)
{
	struct bm_ctx bm_ctx[THREADS];
	pthread_t threads[THREADS];
	pthread_attr_t thread_attr[THREADS];
	int i, j;
	float total_mbs;
	void *retval;


        signal (SIGALRM, sigalrm_handler);
	for (j = 0 ; j < 500; j++) { 
	alarm_notice = 0;
	alarm (3);
	for (i = 0; i < THREADS; i++) {
		bm_ctx[i].multi = j + 100;
		bm_ctx[i].counter = 0;
		pthread_attr_init (&thread_attr[i]);

		pthread_attr_setdetachstate (&thread_attr[i], PTHREAD_CREATE_JOINABLE);
		pthread_create (&threads[i], &thread_attr[i], benchmark, &bm_ctx[i]);
	}
	for (i = 0; i < THREADS; i++) {
		pthread_join (threads[i], &retval);
	}
	total_mbs = 0;
	for (i = 0; i < THREADS; i++) {
		total_mbs = total_mbs + bm_ctx[i].mbs;
	}
	printf ("%d ", 1000 * bm_ctx[0].multi);
	printf ("%9.3f\n", total_mbs);
	}
	return EXIT_SUCCESS;
}
