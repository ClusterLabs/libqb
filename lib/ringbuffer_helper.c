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
#include "ringbuffer_int.h"
#include <qb/qbdefs.h>

static int32_t
my_posix_sem_timedwait(qb_ringbuffer_t * rb, int32_t ms_timeout)
{
	struct timespec ts_timeout;
	int32_t res;

	if (ms_timeout > 0) {
		qb_util_timespec_from_epoch_get(&ts_timeout);
		qb_timespec_add_ms(&ts_timeout, ms_timeout);
	}

sem_wait_again:
	if (ms_timeout > 0) {
		res = sem_timedwait(&rb->shared_hdr->posix_sem, &ts_timeout);
	} else if (ms_timeout == 0) {
		res = sem_trywait(&rb->shared_hdr->posix_sem);
	} else {
		res = sem_wait(&rb->shared_hdr->posix_sem);
	}
	if (res == -1) {
		switch (errno) {
		case EINTR:
			goto sem_wait_again;
			break;
		case EAGAIN:
			res = -ETIMEDOUT;
			break;
		case ETIMEDOUT:
			res = -errno;
			break;
		default:
			res = -errno;
			qb_util_perror(LOG_ERR, "error waiting for semaphore");
			break;
		}
	}
	return res;
}

static int32_t
my_posix_sem_post(qb_ringbuffer_t * rb)
{
	if (sem_post(&rb->shared_hdr->posix_sem) < 0) {
		return -errno;
	} else {
		return 0;
	}
}

static ssize_t
my_posix_getvalue_fn(struct qb_ringbuffer_s *rb)
{
	int val;
	if (sem_getvalue(&rb->shared_hdr->posix_sem, &val) < 0) {
		return -errno;
	} else {
		return val;
	}
}

static int32_t
my_posix_sem_destroy(qb_ringbuffer_t * rb)
{
	if (sem_destroy(&rb->shared_hdr->posix_sem) == -1) {
		return -errno;
	} else {
		return 0;
	}
}

static int32_t
my_posix_sem_create(struct qb_ringbuffer_s *rb, uint32_t flags)
{
	int32_t pshared = 0;
	if (flags & QB_RB_FLAG_SHARED_PROCESS) {
		if ((flags & QB_RB_FLAG_CREATE) == 0) {
			return 0;
		}
		pshared = 1;
	}
	if (sem_init(&rb->shared_hdr->posix_sem, pshared, 0) == -1) {
		return -errno;
	} else {
		return 0;
	}
}

#ifndef HAVE_POSIX_SHARED_SEMAPHORE
static int32_t
my_sysv_sem_timedwait(qb_ringbuffer_t * rb, int32_t ms_timeout)
{
	struct sembuf sops[1];
	int32_t res = 0;
#ifndef QB_FREEBSD_GE_8
	struct timespec ts_timeout;
	struct timespec *ts_pt;

	if (ms_timeout >= 0) {
		/*
		 * Note: sem_timedwait takes an absolute time where as semtimedop
		 * takes a relative time.
		 */
		ts_timeout.tv_sec = 0;
		ts_timeout.tv_nsec = 0;
		qb_timespec_add_ms(&ts_timeout, ms_timeout);
		ts_pt = &ts_timeout;
	} else {
		ts_pt = NULL;
	}
#endif /* bsd */

	/*
	 * wait for sem post.
	 */
	sops[0].sem_num = 0;
	sops[0].sem_op = -1;
#ifdef QB_FREEBSD_GE_8
	sops[0].sem_flg = IPC_NOWAIT;
#else
	sops[0].sem_flg = 0;
#endif /* bsd */

semop_again:
#ifdef QB_FREEBSD_GE_8
	if (semop(rb->sem_id, sops, 1) == -1)
#else
	if (semtimedop(rb->sem_id, sops, 1, ts_pt) == -1)
#endif
	{
		if (errno == EINTR) {
			goto semop_again;
		} else if (errno == EAGAIN) {
			/* make consistent with sem_timedwait */
			res = -ETIMEDOUT;
		} else {
			res = -errno;
			qb_util_perror(LOG_ERR, "error waiting for semaphore");
		}
		return res;
	}
	return 0;
}

static int32_t
my_sysv_sem_post(qb_ringbuffer_t * rb)
{
	struct sembuf sops[1];

	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}

	sops[0].sem_num = 0;
	sops[0].sem_op = 1;
	sops[0].sem_flg = 0;

semop_again:
	if (semop(rb->sem_id, sops, 1) == -1) {
		if (errno == EINTR) {
			goto semop_again;
		} else {
			qb_util_perror(LOG_ERR,
				       "could not increment semaphore");
		}

		return -errno;
	}
	return 0;
}

static ssize_t
my_sysv_getvalue_fn(struct qb_ringbuffer_s *rb)
{
	ssize_t res = semctl(rb->sem_id, 0, GETVAL, 0);
	if (res == -1) {
		return -errno;
	}
	return res;
}

static int32_t
my_sysv_sem_destroy(qb_ringbuffer_t * rb)
{
	if (semctl(rb->sem_id, 0, IPC_RMID, 0) == -1) {
		return -errno;
	} else {
		return 0;
	}
}

static int32_t
my_sysv_sem_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	union semun options;
	int32_t res;
	key_t sem_key;

	sem_key = ftok(rb->shared_hdr->hdr_path, (rb->shared_hdr->word_size + 1));

	if (sem_key == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't get a sem id");
		return res;
	}

	if (flags & QB_RB_FLAG_CREATE) {
		rb->sem_id = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
		if (rb->sem_id == -1) {
			res = -errno;
			qb_util_perror(LOG_ERR, "couldn't create a semaphore");
			return res;
		}
		options.val = 0;
		res = semctl(rb->sem_id, 0, SETVAL, options);
	} else {
		rb->sem_id = semget(sem_key, 0, 0600);
		if (rb->sem_id == -1) {
			res = -errno;
			qb_util_perror(LOG_ERR, "couldn't get a sem id");
			return res;
		}
		res = 0;
	}
	qb_util_log(LOG_DEBUG, "sem key:%d, id:%d, value:%d",
		    (int)sem_key, rb->sem_id, semctl(rb->sem_id, 0, GETVAL, 0));

	return res;
}
#endif /* NOT HAVE_POSIX_SHARED_SEMAPHORE */

int32_t
qb_rb_sem_create(struct qb_ringbuffer_s * rb, uint32_t flags)
{
#ifndef HAVE_POSIX_SHARED_SEMAPHORE
	if (rb->flags & QB_RB_FLAG_SHARED_PROCESS) {
		rb->sem_timedwait_fn = my_sysv_sem_timedwait;
		rb->sem_post_fn = my_sysv_sem_post;
		rb->sem_getvalue_fn = my_sysv_getvalue_fn;
		rb->sem_destroy_fn = my_sysv_sem_destroy;
		return my_sysv_sem_create(rb, flags);
	} else
#endif /* NOT HAVE_POSIX_SHARED_SEMAPHORE */
	{
		rb->sem_timedwait_fn = my_posix_sem_timedwait;
		rb->sem_post_fn = my_posix_sem_post;
		rb->sem_getvalue_fn = my_posix_getvalue_fn;
		rb->sem_destroy_fn = my_posix_sem_destroy;
		return my_posix_sem_create(rb, flags);
	}
}
