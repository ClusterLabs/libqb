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
my_posix_sem_timedwait(void * instance, int32_t ms_timeout)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	struct timespec ts_timeout;
	int32_t res;

	if (ms_timeout > 0) {
		qb_util_timespec_from_epoch_get(&ts_timeout);
		qb_timespec_add_ms(&ts_timeout, ms_timeout);
	}

sem_wait_again:
	if (ms_timeout > 0) {
		res = rpl_sem_timedwait(&rb->shared_hdr->posix_sem, &ts_timeout);
	} else if (ms_timeout == 0) {
		res = rpl_sem_trywait(&rb->shared_hdr->posix_sem);
	} else {
		res = rpl_sem_wait(&rb->shared_hdr->posix_sem);
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
my_posix_sem_post(void * instance, size_t msg_size)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	if (rpl_sem_post(&rb->shared_hdr->posix_sem) < 0) {
		return -errno;
	} else {
		return 0;
	}
}

static ssize_t
my_posix_getvalue_fn(void * instance)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	int val;
	if (rpl_sem_getvalue(&rb->shared_hdr->posix_sem, &val) < 0) {
		return -errno;
	} else {
		return val;
	}
}

static int32_t
my_posix_sem_destroy(void * instance)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	qb_enter();
	if (rpl_sem_destroy(&rb->shared_hdr->posix_sem) == -1) {
		return -errno;
	} else {
		return 0;
	}
}

static int32_t
my_posix_sem_create(void * instance, uint32_t flags)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	int32_t pshared = QB_FALSE;
	if (flags & QB_RB_FLAG_SHARED_PROCESS) {
		if ((flags & QB_RB_FLAG_CREATE) == 0) {
			return 0;
		}
		pshared = QB_TRUE;
	}
	if (rpl_sem_init(&rb->shared_hdr->posix_sem, pshared, 0) == -1) {
		return -errno;
	} else {
		return 0;
	}
}

static int32_t
my_sysv_sem_timedwait(void * instance, int32_t ms_timeout)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	struct sembuf sops[1];
	int32_t res = 0;
#ifdef HAVE_SEMTIMEDOP
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
#endif /* HAVE_SEMTIMEDOP */

	/*
	 * wait for sem post.
	 */
	sops[0].sem_num = 0;
	sops[0].sem_op = -1;
#ifdef HAVE_SEMTIMEDOP
	sops[0].sem_flg = 0;
#else
	sops[0].sem_flg = IPC_NOWAIT;
#endif /* HAVE_SEMTIMEDOP */

semop_again:
#ifdef HAVE_SEMTIMEDOP
	if (semtimedop(rb->sem_id, sops, 1, ts_pt) == -1)
#else
	if (semop(rb->sem_id, sops, 1) == -1)
#endif /* HAVE_SEMTIMEDOP */
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
my_sysv_sem_post(void * instance, size_t msg_size)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
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
my_sysv_getvalue_fn(void * instance)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	ssize_t res = semctl(rb->sem_id, 0, GETVAL, 0);
	if (res == -1) {
		return -errno;
	}
	return res;
}

static int32_t
my_sysv_sem_destroy(void * instance)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
	if (semctl(rb->sem_id, 0, IPC_RMID, 0) == -1) {
		return -errno;
	} else {
		return 0;
	}
}

static int32_t
my_sysv_sem_create(void * instance, uint32_t flags)
{
	struct qb_ringbuffer_s *rb = (struct qb_ringbuffer_s *)instance;
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

int32_t
qb_rb_sem_create(struct qb_ringbuffer_s * rb, uint32_t flags)
{
	int32_t rc;
	int32_t use_posix = QB_TRUE;

	if ((flags & QB_RB_FLAG_SHARED_PROCESS) &&
	    !(flags & QB_RB_FLAG_NO_SEMAPHORE)) {
#if defined(HAVE_POSIX_PSHARED_SEMAPHORE) || \
    defined(HAVE_RPL_PSHARED_SEMAPHORE)
		use_posix = QB_TRUE;
#else
	#ifdef HAVE_SYSV_PSHARED_SEMAPHORE
		use_posix = QB_FALSE;
	#else
		return -ENOTSUP;
	#endif /* HAVE_SYSV_PSHARED_SEMAPHORE */
#endif /* HAVE_POSIX_PSHARED_SEMAPHORE */
	}
	if (flags & QB_RB_FLAG_NO_SEMAPHORE) {
		rc = 0;
		rb->notifier.instance = NULL;
		rb->notifier.timedwait_fn = NULL;
		rb->notifier.post_fn = NULL;
		rb->notifier.q_len_fn = NULL;
		rb->notifier.space_used_fn = NULL;
		rb->notifier.destroy_fn = NULL;
	} else if (use_posix) {
		rc = my_posix_sem_create(rb, flags);
		rb->notifier.instance = rb;
		rb->notifier.timedwait_fn = my_posix_sem_timedwait;
		rb->notifier.post_fn = my_posix_sem_post;
		rb->notifier.q_len_fn = my_posix_getvalue_fn;
		rb->notifier.space_used_fn = NULL;
		rb->notifier.destroy_fn = my_posix_sem_destroy;
	} else {
		rc = my_sysv_sem_create(rb, flags);
		rb->notifier.instance = rb;
		rb->notifier.timedwait_fn = my_sysv_sem_timedwait;
		rb->notifier.post_fn = my_sysv_sem_post;
		rb->notifier.q_len_fn = my_sysv_getvalue_fn;
		rb->notifier.space_used_fn = NULL;
		rb->notifier.destroy_fn = my_sysv_sem_destroy;
	}
	return rc;
}
