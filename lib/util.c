/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include <config.h>

#include "os_base.h"
#include <sys/shm.h>
#include <sys/mman.h>
#include <pthread.h>
#if _POSIX_THREAD_PROCESS_SHARED > 0
#include <semaphore.h>
#else
#include <sys/sem.h>
#endif
#include <qb/qbutil.h>

struct qb_thread_lock_s {
	qb_thread_lock_type_t type;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	pthread_spinlock_t spinlock;
#endif
	pthread_mutex_t mutex;

};


qb_thread_lock_t* qb_thread_lock_create (qb_thread_lock_type_t type)
{
	struct qb_thread_lock_s* tl = malloc (sizeof (struct qb_thread_lock_s));
	int res;

#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (type == QB_THREAD_LOCK_SHORT) {
		tl->type = QB_THREAD_LOCK_SHORT;
		res = pthread_spin_init (&tl->spinlock, 1);
	} else
#endif
	{
		tl->type = QB_THREAD_LOCK_LONG;
		res = pthread_mutex_init (&tl->mutex, NULL);
	}
	if (res == 0) {
		return tl;
	} else {
		free (tl);
		return NULL;
	}
}

int32_t qb_thread_lock (qb_thread_lock_t* tl)
{
	int res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_lock (&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_lock (&tl->mutex);
	}
	return (int32_t)res;
}

int32_t qb_thread_unlock (qb_thread_lock_t* tl)
{
	int res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_unlock (&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_unlock (&tl->mutex);
	}
	return (int32_t)res;
}

int32_t qb_thread_trylock (qb_thread_lock_t* tl)
{
	int res;
#if defined(HAVE_PTHREAD_SPIN_LOCK)
	if (tl->type == QB_THREAD_LOCK_SHORT) {
		res = pthread_spin_trylock (&tl->spinlock);
	} else
#endif
	{
		res = pthread_mutex_trylock (&tl->mutex);
	}
	return (int32_t)res;
}



