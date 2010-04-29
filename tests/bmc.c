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

#include <sys/types.h>
#include <sys/socket.h>
#include <qb/qbipcc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define ITERATIONS 10000

static struct timeval tv1, tv2, tv_elapsed;

#define timersub(a, b, result)					\
do {								\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {				\
		--(result)->tv_sec;				\
		(result)->tv_usec += 1000000;			\
	}							\
} while (0)

FILE *mbs_fp;

FILE *ops_fp;

static void bm_start (void)
{
        gettimeofday (&tv1, NULL);
}

static void bm_finish (const char *operation, int size)
{
	float ops_per_sec;
	float mbs_per_sec;

        gettimeofday (&tv2, NULL);
        timersub (&tv2, &tv1, &tv_elapsed);

	ops_per_sec = 
                ((float)ITERATIONS) / (((float)tv_elapsed.tv_sec) + (((float)tv_elapsed.tv_usec) / 1000000.0));

	mbs_per_sec = 
                ((((float)ITERATIONS) * size) / (((float)tv_elapsed.tv_sec) + (((float)tv_elapsed.tv_usec) / 1000000.0))) / (1024.0*1024.0);


	fprintf (ops_fp, "%d %9.3f\n", size, ops_per_sec);
	fflush (ops_fp);
	fprintf (mbs_fp, "%d %9.3f\n", size, mbs_per_sec);
	fflush (mbs_fp);

	printf ("write size %d OPs/sec %9.3f ", size, ops_per_sec);
	printf ("MB/sec %9.3f\n", mbs_per_sec);
}

qb_hdb_handle_t bmc_ipc_handle;

static void bmc_connect (void)
{
	unsigned int res;

	res = qb_ipcc_service_connect ("qb_ipcs_bm",
		0,
		8192*128,
		8192*128,
		8192*128,
		&bmc_ipc_handle);
}

static char buffer[1024*1024];
static void bmc_send_nozc (unsigned int size)
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
		bmc_ipc_handle,
		iov,
		2,
		&res_header,
		sizeof (qb_ipc_response_header_t));
	if (res != 0) {
		goto repeat_send;
	}
}

qb_ipc_request_header_t *global_zcb_buffer;

int main (void)
{
	int i, j;

	bmc_connect();

	ops_fp = fopen ("opsec", "w");
	mbs_fp = fopen ("mbsec", "w");

	for (j = 1; j < 499; j++) {
		bm_start();
		for (i = 0; i < ITERATIONS; i++) {
			bmc_send_nozc (1000 * j);
		}
		bm_finish("send_nozc", 1000 * j);
	}
	return EXIT_SUCCESS;
}

