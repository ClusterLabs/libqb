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
#include <syslog.h>
#include <errno.h>

#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qbrb.h>
#include <qb/qbipc_common.h>
#include <qb/qblog.h>

START_TEST(test_ring_buffer1)
{
	char my_buf[512];
	struct qb_ipc_request_header hdr;
	char *str;
	qb_ringbuffer_t *rb;
	int32_t i;
	int32_t b;
	ssize_t actual;
	ssize_t avail;

	rb = qb_rb_open("test1", 200, QB_RB_FLAG_CREATE, 0);
	ck_assert(rb != NULL);

	for (b = 0; b < 3; b++) {
		memcpy(&hdr, my_buf, sizeof(struct qb_ipc_request_header));
		str = my_buf + sizeof(struct qb_ipc_request_header);

		for (i = 0; i < 900; i++) {
			hdr.id = __LINE__ + i;
			hdr.size =
			    sprintf(str, "ID: %d (%s + i(%d)) -- %s-%s!",
				    hdr.id, "actually the line number", i,
				    __func__, __FILE__) + 1;
			hdr.size += sizeof(struct qb_ipc_request_header);
			memcpy(my_buf, &hdr, sizeof(struct qb_ipc_request_header));
			avail = qb_rb_space_free(rb);
			actual = qb_rb_chunk_write(rb, my_buf, hdr.size);
			if (avail < (hdr.size + (3 * sizeof(uint32_t)))) {
				ck_assert_int_eq(actual, -EAGAIN);
			} else {
				ck_assert_int_eq(actual, hdr.size);
			}
		}

		memset(my_buf, 0, sizeof(my_buf));

		memcpy(&hdr, my_buf, sizeof(struct qb_ipc_request_header));
		str = my_buf + sizeof(struct qb_ipc_request_header);

		for (i = 0; i < 15; i++) {
			actual = qb_rb_chunk_read(rb, my_buf, 512, 0);
			if (actual < 0) {
				ck_assert_int_eq(0, qb_rb_chunks_used(rb));
				break;
			}
			memcpy(&hdr, my_buf, sizeof(struct qb_ipc_request_header));
			str[actual - sizeof(struct qb_ipc_request_header)] = '\0';
			ck_assert_int_eq(actual, hdr.size);
		}
	}
	qb_rb_close(rb);
}
END_TEST

/*
 * nice size (int64)
 */
START_TEST(test_ring_buffer2)
{
	qb_ringbuffer_t *t;
	int32_t i;
	int64_t v = 7891034;
	int64_t *new_data;
	ssize_t l;

	t = qb_rb_open("test2", 200 * sizeof(int64_t), QB_RB_FLAG_CREATE, 0);
	ck_assert(t != NULL);
	for (i = 0; i < 200; i++) {
		l = qb_rb_chunk_write(t, &v, sizeof(v));
		ck_assert_int_eq(l, sizeof(v));
	}
	for (i = 0; i < 100; i++) {
		l = qb_rb_chunk_peek(t, (void **)&new_data, 0);
		ck_assert_int_eq(l, sizeof(v));
		ck_assert(v == *new_data);
		qb_rb_chunk_reclaim(t);
	}
	for (i = 0; i < 100; i++) {
		l = qb_rb_chunk_write(t, &v, sizeof(v));
		ck_assert_int_eq(l, sizeof(v));
	}
	for (i = 0; i < 100; i++) {
		l = qb_rb_chunk_peek(t, (void **)&new_data, 0);
		if (l == 0) {
			/* no more to read */
			break;
		}
		ck_assert_int_eq(l, sizeof(v));
		ck_assert(v == *new_data);
		qb_rb_chunk_reclaim(t);
	}
	qb_rb_close(t);
}
END_TEST

/*
 * odd size (10)
 */
START_TEST(test_ring_buffer3)
{
	qb_ringbuffer_t *t;
	int32_t i;
	char v[] = "1234567891";
	char out[32];
	ssize_t l;
	size_t len = strlen(v) + 1;

	t = qb_rb_open("test3", 10, QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
	ck_assert(t != NULL);
	for (i = 0; i < 9000; i++) {
		l = qb_rb_chunk_write(t, v, len);
		ck_assert_int_eq(l, len);
	}
	for (i = 0; i < 2000; i++) {
		l = qb_rb_chunk_read(t, (void *)out, 32, 0);
		if (l < 0) {
			/* no more to read */
			break;
		}
		ck_assert_int_eq(l, len);
		ck_assert_str_eq(v, out);
	}
	qb_rb_close(t);
}
END_TEST

START_TEST(test_ring_buffer4)
{
	qb_ringbuffer_t *t;
	char data[] = "1234567891";
	int32_t i;
	char *new_data;
	ssize_t l;

	t = qb_rb_open("test4", 10, QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
	ck_assert(t != NULL);
	for (i = 0; i < 2000; i++) {
		l = qb_rb_chunk_write(t, data, strlen(data));
		ck_assert_int_eq(l, strlen(data));
		if (i == 0) {
			data[0] = 'b';
		}
	}
	for (i = 0; i < 2000; i++) {
		l = qb_rb_chunk_peek(t, (void **)&new_data, 0);
		if (l < 0) {
			/* no more to read */
			break;
		}
		ck_assert_int_eq(l, strlen(data));
		qb_rb_chunk_reclaim(t);
	}
	qb_rb_close(t);
}
END_TEST

static Suite *rb_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("ringbuffer");

	add_tcase(s, tc, test_ring_buffer1);
	add_tcase(s, tc, test_ring_buffer2);
	add_tcase(s, tc, test_ring_buffer3);
	add_tcase(s, tc, test_ring_buffer4);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = rb_suite();
	SRunner *sr = srunner_create(s);

	qb_log_init("check", LOG_USER, LOG_EMERG);
	atexit(qb_log_fini);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
