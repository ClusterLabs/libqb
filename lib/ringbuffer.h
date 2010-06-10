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

#include <config.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "os_base.h"

#include <limits.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <pthread.h>
#include <semaphore.h>

#include "util_int.h"
#include <qb/qbutil.h>
#include <qb/qbrb.h>

struct qb_ringbuffer_shared_s {
	volatile uint32_t write_pt;
	volatile uint32_t read_pt;
	uint32_t size;
	uint32_t count;
	char hdr_path[PATH_MAX];
	char data_path[PATH_MAX];
	int32_t ref_count;
#if _POSIX_THREAD_PROCESS_SHARED > 0
	sem_t posix_sem;
	pthread_spinlock_t spinlock;
#endif
};

struct qb_ringbuffer_s {
	uint32_t flags;
#if _POSIX_THREAD_PROCESS_SHARED < 1
	int32_t lock_id;
	int32_t sem_id;
#endif
	struct qb_ringbuffer_shared_s *shared_hdr;
	uint32_t *shared_data;
};

#if defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};
#endif				/* _SEM_SEMUN_UNDEFINED */

#define RB_NS_IN_MSEC  1000000ULL

int32_t my_lock_it_create (qb_ringbuffer_t * rb, uint32_t flags);
int32_t my_lock_it (qb_ringbuffer_t * rb);
int32_t my_unlock_it (qb_ringbuffer_t * rb);
int32_t my_lock_it_destroy (qb_ringbuffer_t * rb);

int32_t my_sem_create (qb_ringbuffer_t * rb, uint32_t flags);
int32_t my_sem_post (qb_ringbuffer_t * rb);
int32_t my_sem_timedwait (qb_ringbuffer_t * rb, int32_t ms_timeout);
int32_t my_sem_destroy (qb_ringbuffer_t * rb);

#endif				/* _RINGBUFFER_H_ */
