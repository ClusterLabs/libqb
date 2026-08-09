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
#include <unistd.h>

#include "os_base.h"
#include "check_common.h"

#include <qb/qbdefs.h>
#include <qb/qbrb.h>
#include <qb/qbipc_common.h>
#include <qb/qblog.h>

#if defined(HAVE_FAILURE_INJECTION) && defined(HAVE_MLOCK)
#include "_failure_injection.h"
#include "../lib/ringbuffer_int.h"
#endif

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

#if defined(HAVE_FAILURE_INJECTION) && defined(HAVE_MLOCK)

static void
make_mlock_test_path(char *directory, size_t directory_size,
		     char *name, size_t name_size)
{
	const char *tmpdir = getenv("TMPDIR");
	int result;

	if ((tmpdir == NULL) || (tmpdir[0] == '\0')) {
		tmpdir = "/tmp";
	}
	result = snprintf(directory, directory_size,
			  "%s/qb-rb-mlock-XXXXXX", tmpdir);
	ck_assert_int_ge(result, 0);
	ck_assert((size_t)result < directory_size);
	ck_assert(mkdtemp(directory) != NULL);

	result = snprintf(name, name_size, "%s/ring", directory);
	ck_assert_int_ge(result, 0);
	ck_assert((size_t)result < name_size);
}

static void
assert_mlock_test_files_absent(const char *name)
{
	char path[PATH_MAX];
	int result;

	result = snprintf(path, sizeof(path), "%s-header", name);
	ck_assert_int_ge(result, 0);
	ck_assert((size_t)result < sizeof(path));
	ck_assert_int_eq(access(path, F_OK), -1);
	ck_assert_int_eq(errno, ENOENT);

	result = snprintf(path, sizeof(path), "%s-data", name);
	ck_assert_int_ge(result, 0);
	ck_assert((size_t)result < sizeof(path));
	ck_assert_int_eq(access(path, F_OK), -1);
	ck_assert_int_eq(errno, ENOENT);
}

START_TEST(test_ring_buffer_mlocks_mappings)
{
	char directory[PATH_MAX];
	char name[PATH_MAX];
	qb_ringbuffer_t *rb;
	const void *header_address;
	const void *data_address;
	size_t header_length;
	size_t data_length;

	make_mlock_test_path(directory, sizeof(directory), name, sizeof(name));
	_fi_mlock_reset();
	_fi_mlock_fake_success = QB_TRUE;

	rb = qb_rb_open(name, 1, QB_RB_FLAG_CREATE | QB_RB_FLAG_MLOCK,
			4);
	ck_assert(rb != NULL);
	header_address = rb->shared_hdr;
	header_length = sizeof(struct qb_ringbuffer_shared_s) + 4;
	data_address = rb->shared_data;
	data_length = rb->shared_hdr->word_size * sizeof(uint32_t);
	qb_rb_close(rb);

	assert_mlock_test_files_absent(name);
	ck_assert_int_eq(rmdir(directory), 0);
	ck_assert_int_eq(_fi_mlock_called, 2);
	ck_assert(_fi_mlock_calls[0].address == header_address);
	ck_assert(_fi_mlock_calls[0].length == header_length);
	ck_assert(_fi_mlock_calls[1].address == data_address);
	ck_assert(_fi_mlock_calls[1].length == data_length);
}
END_TEST

START_TEST(test_ring_buffer_restore_mlocks_mappings)
{
	char directory[PATH_MAX];
	char name[PATH_MAX];
	qb_ringbuffer_t *rb;
	qb_ringbuffer_t *restored;
	FILE *file;
	int fd;

	make_mlock_test_path(directory, sizeof(directory), name, sizeof(name));
	rb = qb_rb_open(name, 1, QB_RB_FLAG_CREATE, 0);
	ck_assert(rb != NULL);

	file = tmpfile();
	ck_assert(file != NULL);
	fd = fileno(file);
	ck_assert_int_ge(fd, 0);
	ck_assert_int_gt(qb_rb_write_to_file(rb, fd), 0);
	qb_rb_close(rb);
	ck_assert_int_eq(lseek(fd, 0, SEEK_SET), 0);

	_fi_mlock_reset();
	_fi_mlock_fake_success = QB_TRUE;
	restored = qb_rb_create_from_file(fd, QB_RB_FLAG_MLOCK);
	ck_assert(restored != NULL);
	qb_rb_close(restored);

	ck_assert_int_eq(fclose(file), 0);
	ck_assert_int_eq(rmdir(directory), 0);
	ck_assert_int_eq(_fi_mlock_called, 2);
	ck_assert(_fi_mlock_calls[0].address != NULL);
	ck_assert(_fi_mlock_calls[1].address != NULL);
}
END_TEST

START_TEST(test_ring_buffer_header_mlock_failure_cleans_up)
{
	char directory[PATH_MAX];
	char name[PATH_MAX];
	qb_ringbuffer_t *rb;
	int open_errno;

	make_mlock_test_path(directory, sizeof(directory), name, sizeof(name));
	_fi_mlock_reset();
	_fi_mlock_fake_success = QB_TRUE;
	_fi_mlock_fail_at = 1;

	rb = qb_rb_open(name, 1, QB_RB_FLAG_CREATE | QB_RB_FLAG_MLOCK,
			4);
	open_errno = errno;
	if (rb != NULL) {
		qb_rb_close(rb);
	}

	assert_mlock_test_files_absent(name);
	ck_assert_int_eq(rmdir(directory), 0);
	ck_assert(rb == NULL);
	ck_assert_int_eq(open_errno, ENOMEM);
	ck_assert_int_eq(_fi_mlock_called, 1);
	ck_assert(_fi_mlock_calls[0].address != NULL);
	ck_assert(_fi_mlock_calls[0].length ==
		  sizeof(struct qb_ringbuffer_shared_s) + 4);
}
END_TEST

START_TEST(test_ring_buffer_data_mlock_failure_cleans_up)
{
	char directory[PATH_MAX];
	char name[PATH_MAX];
	qb_ringbuffer_t *rb;
	long page_size = sysconf(_SC_PAGESIZE);
	int open_errno;

	ck_assert_int_gt(page_size, 0);
	make_mlock_test_path(directory, sizeof(directory), name, sizeof(name));
	_fi_mlock_reset();
	_fi_mlock_fake_success = QB_TRUE;
	_fi_mlock_fail_at = 2;

	rb = qb_rb_open(name, 1, QB_RB_FLAG_CREATE | QB_RB_FLAG_MLOCK,
			4);
	open_errno = errno;
	if (rb != NULL) {
		qb_rb_close(rb);
	}

	assert_mlock_test_files_absent(name);
	ck_assert_int_eq(rmdir(directory), 0);
	ck_assert(rb == NULL);
	ck_assert_int_eq(open_errno, ENOMEM);
	ck_assert_int_eq(_fi_mlock_called, 2);
	ck_assert(_fi_mlock_calls[1].address != NULL);
	ck_assert(_fi_mlock_calls[1].length == (size_t)page_size);
}
END_TEST

#endif /* HAVE_FAILURE_INJECTION && HAVE_MLOCK */

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

	add_tcase(s, tc, test_ring_buffer1, 0);
	add_tcase(s, tc, test_ring_buffer2, 0);
	add_tcase(s, tc, test_ring_buffer3, 0);
	add_tcase(s, tc, test_ring_buffer4, 0);
#if defined(HAVE_FAILURE_INJECTION) && defined(HAVE_MLOCK)
	add_tcase(s, tc, test_ring_buffer_mlocks_mappings, 0);
	add_tcase(s, tc, test_ring_buffer_restore_mlocks_mappings, 0);
	add_tcase(s, tc, test_ring_buffer_header_mlock_failure_cleans_up, 0);
	add_tcase(s, tc, test_ring_buffer_data_mlock_failure_cleans_up, 0);
#endif

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
