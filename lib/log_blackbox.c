/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#include <qb/qbrb.h>
#include "util_int.h"
#include "log_int.h"
#include "ringbuffer_int.h"

#define BB_MIN_ENTRY_SIZE (4 * sizeof(uint32_t) +\
			   sizeof(uint8_t) +\
			   2 * sizeof(char) + sizeof(time_t))


static void
_blackbox_reload(int32_t target)
{
	struct qb_log_target *t = qb_log_target_get(target);

	if (t->instance == NULL) {
		return;
	}
	qb_rb_close(t->instance);
	t->instance = qb_rb_open(t->filename, t->size,
				 QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
}

/* <u32> file lineno
 * <u32> tags
 * <u8> priority
 * <u32> function name length
 * <string> function name
 * <u32> buffer length
 * <string> buffer
 */
static void
_blackbox_vlogger(int32_t target,
		  struct qb_log_callsite *cs, struct timespec *timestamp, va_list ap)
{
	size_t max_size;
	size_t actual_size;
	uint32_t fn_size;
	char *chunk;
	char *msg_len_pt;
	uint32_t msg_len;
	struct qb_log_target *t = qb_log_target_get(target);

	if (t->instance == NULL) {
		return;
	}

	fn_size = strlen(cs->function) + 1;

	actual_size = 4 * sizeof(uint32_t) + sizeof(uint8_t) + fn_size + sizeof(struct timespec);
	max_size = actual_size + t->max_line_length;

	chunk = qb_rb_chunk_alloc(t->instance, max_size);

	if (chunk == NULL) {
		/* something bad has happened. abort blackbox logging */
		qb_util_perror(LOG_ERR, "Blackbox allocation error, aborting blackbox log %s", t->filename);
		qb_rb_close(qb_rb_lastref_and_ret(
			(struct qb_ringbuffer_s **) &t->instance
		));
		return;
	}

	/* line number */
	memcpy(chunk, &cs->lineno, sizeof(uint32_t));
	chunk += sizeof(uint32_t);

	/* tags */
	memcpy(chunk, &cs->tags, sizeof(uint32_t));
	chunk += sizeof(uint32_t);

	/* log level/priority */
	memcpy(chunk, &cs->priority, sizeof(uint8_t));
	chunk += sizeof(uint8_t);

	/* function name */
	memcpy(chunk, &fn_size, sizeof(uint32_t));
	chunk += sizeof(uint32_t);
	memcpy(chunk, cs->function, fn_size);
	chunk += fn_size;

	/* timestamp */
	memcpy(chunk, timestamp, sizeof(struct timespec));
	chunk += sizeof(struct timespec);

	/* log message length */
	msg_len_pt = chunk;
	chunk += sizeof(uint32_t);

	/* log message */
	msg_len = qb_vsnprintf_serialize(chunk, max_size, cs->format, ap);
	if (msg_len >= max_size) {
	    chunk = msg_len_pt + sizeof(uint32_t); /* Reset */

	    /* Leave this at QB_LOG_MAX_LEN so as not to overflow the blackbox */
	    msg_len = qb_vsnprintf_serialize(chunk, QB_LOG_MAX_LEN,
		"Log message too long to be stored in the blackbox.  "\
		"Maximum is QB_LOG_MAX_LEN" , ap);
	    actual_size += msg_len;
	}

	actual_size += msg_len;

	/* now that we know the length, write it
	 */
	memcpy(msg_len_pt, &msg_len, sizeof(uint32_t));

	(void)qb_rb_chunk_commit(t->instance, actual_size);
}

static void
_blackbox_close(int32_t target)
{
	struct qb_log_target *t = qb_log_target_get(target);

	qb_rb_close(qb_rb_lastref_and_ret(
		(struct qb_ringbuffer_s **) &t->instance
	));
}

int32_t
qb_log_blackbox_open(struct qb_log_target *t)
{
	if (t->size < 1024) {
		return -EINVAL;
	}
	snprintf(t->filename, PATH_MAX, "%s-%d-blackbox", t->name, getpid());

	t->instance = qb_rb_open(t->filename, t->size,
				 QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
	if (t->instance == NULL) {
		return -errno;
	}

	t->logger = NULL;
	t->vlogger = _blackbox_vlogger;
	t->reload = _blackbox_reload;
	t->close = _blackbox_close;
	return 0;
}

/*
 * This is designed to look as much like the ringbuffer header
 * as possible so that we can distinguish an old RB dump
 * from a new one with this header.
 */

struct _blackbox_file_header {
	uint32_t word_size;
	uint32_t read_pt;
	uint32_t write_pt;
	uint32_t version;
	uint32_t hash;
} __attribute__((packed));

/* Values we expect for a 'new' header */
#define QB_BLACKBOX_HEADER_WORDSIZE 0
#define QB_BLACKBOX_HEADER_READPT   0xCCBBCCBB
#define QB_BLACKBOX_HEADER_WRITEPT  0xBBCCBBCC
#define QB_BLACKBOX_HEADER_VERSION  2
#define QB_BLACKBOX_HEADER_HASH     0

ssize_t
qb_log_blackbox_write_to_file(const char *filename)
{
	ssize_t written_size = 0;
	struct qb_log_target *t;
	struct _blackbox_file_header header;
	int fd = open(filename, O_CREAT | O_RDWR, 0700);

	if (fd < 0) {
		return -errno;
	}

	/* Write header, so we know this is a 'new' format blackbox */
	header.word_size = QB_BLACKBOX_HEADER_WORDSIZE;
	header.read_pt   = QB_BLACKBOX_HEADER_READPT;
	header.write_pt  = QB_BLACKBOX_HEADER_WRITEPT;
	header.version   = QB_BLACKBOX_HEADER_VERSION;
	header.hash      = QB_BLACKBOX_HEADER_HASH;
	written_size = write(fd, &header, sizeof(header));
	if (written_size < sizeof(header)) {
		close(fd);
		return written_size;
	}

	t = qb_log_target_get(QB_LOG_BLACKBOX);
	if (t->instance) {
		written_size += qb_rb_write_to_file(t->instance, fd);
	} else {
		written_size = -ENOENT;
	}
	close(fd);

	return written_size;
}

int
qb_log_blackbox_print_from_file(const char *bb_filename)
{
	qb_ringbuffer_t *instance;
	ssize_t bytes_read;
	int max_size = 2 * QB_LOG_MAX_LEN;
	char *chunk;
	int fd;
	int err = 0;
	int saved_errno;
	struct _blackbox_file_header header;
	int have_timespecs = 0;
	char time_buf[64];

	fd = open(bb_filename, 0);
	if (fd < 0) {
		saved_errno = errno;
		qb_util_perror(LOG_ERR, "qb_log_blackbox_print_from_file");
		return -saved_errno;
	}

	/* Read the header. If it looks like one of ours then
	   we know we have hi-res timestamps */
	err = read(fd, &header, sizeof(header));
	if (err < sizeof(header)) {
		saved_errno = errno;
		close(fd);
		return -saved_errno;
	}

	if (header.word_size == QB_BLACKBOX_HEADER_WORDSIZE &&
	    header.read_pt == QB_BLACKBOX_HEADER_READPT &&
	    header.write_pt == QB_BLACKBOX_HEADER_WRITEPT &&
	    header.version == QB_BLACKBOX_HEADER_VERSION &&
	    header.hash == QB_BLACKBOX_HEADER_HASH) {
		have_timespecs = 1;
	} else {
		(void)lseek(fd, 0, SEEK_SET);
	}


	instance = qb_rb_create_from_file(fd, 0);
	close(fd);
	if (instance == NULL) {
		return -EIO;
	}
	chunk = malloc(max_size);

	do {
		char *ptr;
		uint32_t lineno;
		uint32_t tags;
		uint8_t priority;
		uint32_t fn_size;
		char *function;
		uint32_t len;
		struct timespec timestamp;
		time_t time_sec;
		uint32_t msg_len;
		struct tm *tm;
		char message[QB_LOG_MAX_LEN];

		bytes_read = qb_rb_chunk_read(instance, chunk, max_size, 0);

		if (bytes_read >= 0 && bytes_read < BB_MIN_ENTRY_SIZE) {
			printf("ERROR Corrupt file: blackbox header too small.\n");
			err = -1;
			goto cleanup;
		} else if (bytes_read < 0) {
			errno = -bytes_read;
			perror("ERROR: qb_rb_chunk_read failed");
			err = -EIO;
			goto cleanup;
		}
		ptr = chunk;

		/* lineno */
		memcpy(&lineno, ptr, sizeof(uint32_t));
		ptr += sizeof(uint32_t);

		/* tags */
		memcpy(&tags, ptr, sizeof(uint32_t));
		ptr += sizeof(uint32_t);

		/* priority */
		memcpy(&priority, ptr, sizeof(uint8_t));
		ptr += sizeof(uint8_t);

		/* function size & name */
		memcpy(&fn_size, ptr, sizeof(uint32_t));
		if ((fn_size + BB_MIN_ENTRY_SIZE) > bytes_read) {
#ifndef S_SPLINT_S
			printf("ERROR Corrupt file: fn_size way too big %" PRIu32 "\n", fn_size);
			err = -EIO;
#endif /* S_SPLINT_S */
			goto cleanup;
		}
		if (fn_size <= 0) {
#ifndef S_SPLINT_S
			printf("ERROR Corrupt file: fn_size negative %" PRIu32 "\n", fn_size);
			err = -EIO;
#endif /* S_SPLINT_S */
			goto cleanup;
		}
		ptr += sizeof(uint32_t);

		function = ptr;
		ptr += fn_size;

		/* timestamp size & content */
		if (have_timespecs) {
			memcpy(&timestamp, ptr, sizeof(struct timespec));
			ptr += sizeof(struct timespec);
			time_sec = timestamp.tv_sec;
		} else {
			memcpy(&time_sec, ptr, sizeof(time_t));
			ptr += sizeof(time_t);
			timestamp.tv_nsec = 0LL;
		}

		tm = localtime(&time_sec);
		if (tm) {
			int slen = strftime(time_buf,
					    sizeof(time_buf), "%b %d %T",
					    tm);
			snprintf(time_buf+slen, sizeof(time_buf - slen), ".%03llu", timestamp.tv_nsec/QB_TIME_NS_IN_MSEC);
		} else {
			snprintf(time_buf, sizeof(time_buf), "%ld",
				 (long int)time_sec);
		}
		/* message length */
		memcpy(&msg_len, ptr, sizeof(uint32_t));
		if (msg_len > QB_LOG_MAX_LEN || msg_len <= 0) {
#ifndef S_SPLINT_S
			printf("ERROR Corrupt file: msg_len out of bounds %" PRIu32 "\n", msg_len);
			err = -EIO;
#endif /* S_SPLINT_S */
			goto cleanup;
		}

		ptr += sizeof(uint32_t);

		/* message content */
		len = qb_vsnprintf_deserialize(message, QB_LOG_MAX_LEN, ptr);
		assert(len > 0);
		message[len] = '\0';
		len--;
		while (len > 0 && (message[len] == '\n' || message[len] == '\0')) {
			message[len] = '\0';
			len--;
		}

		printf("%-7s %s %s(%u):%u: %s\n",
		       qb_log_priority2str(priority),
		       time_buf, function, lineno, tags, message);

	} while (bytes_read > BB_MIN_ENTRY_SIZE);

cleanup:
	qb_rb_close(instance);
	free(chunk);
	return err;
}
