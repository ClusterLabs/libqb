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


/* For qb_rb_close_helper, we need to open directory in read-only
   mode and with as lightweight + strict flags as available at
   given platform (O_PATH for the former, O_DIRECTORY for the
   latter); end result is available as RB_DIR_RO_FLAGS.
 */
#if defined(HAVE_OPENAT) && defined(HAVE_UNLINKAT)
#  ifndef O_DIRECTORY
#    define RB_DIR_RO_FLAGS1 O_RDONLY
#  else
#    define RB_DIR_RO_FLAGS1 O_RDONLY|O_DIRECTORY
#  endif
#  ifndef O_PATH
#    define RB_DIR_RO_FLAGS RB_DIR_RO_FLAGS1
#  else
#    define RB_DIR_RO_FLAGS RB_DIR_RO_FLAGS1|O_PATH
#  endif

int32_t
qb_rb_close_helper(struct qb_ringbuffer_s * rb, int32_t unlink_it,
		   int32_t truncate_fallback)
{
	int32_t res = 0, res2 = 0;
	uint32_t word_size = rb->shared_hdr->word_size;
	char *hdr_path = rb->shared_hdr->hdr_path;

	if (unlink_it) {
		qb_util_log(LOG_DEBUG, "Free'ing ringbuffer: %s", hdr_path);
		if (rb->notifier.destroy_fn) {
			(void)rb->notifier.destroy_fn(rb->notifier.instance);
		}
	} else {
		qb_util_log(LOG_DEBUG, "Closing ringbuffer: %s", hdr_path);
		hdr_path = NULL;
	}

	if (unlink_it) {
		char *data_path = rb->shared_hdr->data_path;
		char *sep = strrchr(data_path, '/');
		/* we could modify data_path in-situ, but that would segfault if
		   we hadn't write permissions to the underlying mmap'd file */
		char dir_path[PATH_MAX];
		int dirfd;

		if (sep != NULL) {
			strncpy(dir_path, data_path, sep - data_path);
			dir_path[sep - data_path] = '\0';
			if ((dirfd = open(dir_path, RB_DIR_RO_FLAGS)) != -1) {
				res = qb_sys_unlink_or_truncate_at(dirfd, sep + 1,
								   truncate_fallback);

				/* the dirname part is assumed to be the same */
				assert(!strncmp(dir_path, hdr_path, sep - data_path));

				sep = hdr_path + (sep - data_path);
				/* now, don't touch neither data_path nor hdr_path */
				res2 = qb_sys_unlink_or_truncate_at(dirfd, sep + 1,
								    truncate_fallback);
				close(dirfd);
			} else {
				res = -errno;
				qb_util_perror(LOG_DEBUG,
					       "Cannot open dir: %s", hdr_path);
			}
		} else {
			res = -EINVAL;
			qb_util_perror(LOG_DEBUG,
				       "Not dir-separable path: %s", hdr_path);
		}
#else
		res = qb_sys_unlink_or_truncate(data_path, truncate_fallback);
		res2 = qb_sys_unlink_or_truncate(hdr_path, truncate_fallback);
#endif  /* defined(HAVE_OPENAT) && defined(HAVE_UNLINKAT) */

		res = res ? res : res2;
		hdr_path = NULL;
	}  /* if (unlink_it) */

	if (munmap(rb->shared_data, (word_size * sizeof(uint32_t)) << 1) == -1) {
		res = res ? res : -errno;
		qb_util_perror(LOG_DEBUG, "Cannot munmap shared_data");
	}
	if (munmap(rb->shared_hdr, sizeof(struct qb_ringbuffer_shared_s)) == -1) {
		res = res ? res : -errno;
		qb_util_perror(LOG_DEBUG, "Cannot munmap shared_hdr");
	}
	free(rb);
	return res;
}
