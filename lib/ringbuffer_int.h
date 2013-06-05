/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include "os_base.h"

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif /* HAVE_SYS_MMAN_H */
#ifdef HAVE_SYS_SEM_H
#include <sys/sem.h>
#endif
#ifdef HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif
#include "rpl_sem.h"
#include "util_int.h"
#include <qb/qbutil.h>
#include <qb/qbrb.h>

struct qb_ringbuffer_s;

int32_t qb_rb_sem_create(struct qb_ringbuffer_s *rb, uint32_t flags);

typedef int32_t(*qb_rb_notifier_post_fn_t) (void * instance, size_t msg_size);
typedef ssize_t(*qb_rb_notifier_q_len_fn_t) (void * instance);
typedef ssize_t(*qb_rb_notifier_used_fn_t) (void * instance);
typedef int32_t(*qb_rb_notifier_timedwait_fn_t) (void * instance,
					         int32_t ms_timeout);
typedef int32_t(*qb_rb_notifier_reclaim_fn_t) (void * instance, size_t msg_size);
typedef int32_t(*qb_rb_notifier_destroy_fn_t) (void * instance);

struct qb_rb_notifier {
	qb_rb_notifier_post_fn_t post_fn;
	qb_rb_notifier_q_len_fn_t q_len_fn;
	qb_rb_notifier_used_fn_t space_used_fn;
	qb_rb_notifier_timedwait_fn_t timedwait_fn;
	qb_rb_notifier_reclaim_fn_t reclaim_fn;
	qb_rb_notifier_destroy_fn_t destroy_fn;
	void *instance;
};

struct qb_ringbuffer_shared_s {
	volatile uint32_t write_pt;
	volatile uint32_t read_pt;
	uint32_t word_size;
	char hdr_path[PATH_MAX];
	char data_path[PATH_MAX];
	int32_t ref_count;
	rpl_sem_t posix_sem;
	char user_data[1];
} __attribute__ ((aligned(8)));

struct qb_ringbuffer_s {
	uint32_t flags;
	int32_t sem_id;
	struct qb_ringbuffer_shared_s *shared_hdr;
	uint32_t *shared_data;

	struct qb_rb_notifier notifier;
};

void qb_rb_force_close(qb_ringbuffer_t * rb);

qb_ringbuffer_t *qb_rb_open_2(const char *name, size_t size, uint32_t flags,
			      size_t shared_user_data_size,
			      struct qb_rb_notifier *notifier);


#ifndef HAVE_SEMUN
union semun {
	int32_t val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif /* HAVE_SEMUN */

#endif /* _RINGBUFFER_H_ */
