/*
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * Modified by: Angus Salkeld <asalkeld@redhat.com>
 *              Copyright (C) 2012 Red Hat, Inc.
 * To conform to posix API and support process shared semaphores.
 *
 * The bsd posix semaphore implementation does not have support for timing
 * out while waiting for a synchronization object. This uses the
 * pthread_cond_timedwait function and a mutex to build all the other
 * synchronization objecs with timeout capabilities.
 */

#include "os_base.h"
#include <pthread.h>
#include "rpl_sem.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>

int
rpl_sem_init(rpl_sem_t * sem, int pshared, unsigned int count)
{
	int rc;
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

#ifndef HAVE_RPL_PSHARED_SEMAPHORE
	if (pshared) {
		errno = ENOSYS;
		return -1;
	}
#endif /* HAVE_RPL_PSHARED_SEMAPHORE */
	sem->count = count;
	sem->destroy_request = QB_FALSE;

	(void)pthread_mutexattr_init(&mattr);
	(void)pthread_condattr_init(&cattr);
#ifdef HAVE_RPL_PSHARED_SEMAPHORE
	if (pshared) {
		rc = pthread_mutexattr_setpshared(&mattr,
						  PTHREAD_PROCESS_SHARED);
		if (rc != 0) {
			goto cleanup;
		}
		rc = pthread_condattr_setpshared(&cattr,
						 PTHREAD_PROCESS_SHARED);
		if (rc != 0) {
			goto cleanup;
		}
	}
#endif /* HAVE_RPL_PSHARED_SEMAPHORE */
	rc = pthread_mutex_init(&sem->mutex, &mattr);
	if (rc != 0) {
		goto cleanup;
	}
	rc = pthread_cond_init(&sem->cond, &cattr);

	if (rc != 0) {
		goto cleanup_mutex;
	}
	return 0;

cleanup_mutex:
	pthread_mutex_destroy(&sem->mutex);

cleanup:
	pthread_mutexattr_destroy(&mattr);
	pthread_condattr_destroy(&cattr);
	return rc;
}

static int
_rpl_sem_timedwait(rpl_sem_t * sem, const struct timespec *timeout)
{
	int retval = pthread_mutex_lock(&sem->mutex);
	if (retval != 0) {
		return -errno;
	}
	if (sem->destroy_request) {
		retval = -EINVAL;
		goto unlock_it;
	}

	/* wait for sem->count to be not zero, or error
	 */
	while (0 == retval && !sem->count) {
		retval = -pthread_cond_timedwait(&sem->cond,
						 &sem->mutex, timeout);
	}
	if (sem->destroy_request) {
		retval = -EINVAL;
		goto unlock_it;
	}

	switch (retval) {
	case 0:
		/* retval is 0 and sem->count is not, the sem is ours
		 */
		sem->count--;
		break;

	case ETIMEDOUT:
		/* timedout waiting for count to be not zero
		 */
		retval = -EAGAIN;
		break;

	default:
		break;
	}

unlock_it:
	pthread_mutex_unlock(&sem->mutex);
	return retval;
}

int
rpl_sem_wait(rpl_sem_t * sem)
{

	struct timespec ts_timeout;
	int32_t rc;

	do {
		qb_util_timespec_from_epoch_get(&ts_timeout);
		qb_timespec_add_ms(&ts_timeout, 1000);
		rc = _rpl_sem_timedwait(sem, &ts_timeout);
	} while (rc == -EAGAIN);
	if (rc < 0) {
		errno = -rc;
		return -1;
	}
	return 0;
}

int
rpl_sem_timedwait(rpl_sem_t * sem, const struct timespec *timeout)
{
	int rc = _rpl_sem_timedwait(sem, timeout);
	if (rc < 0) {
		errno = -rc;
		return -1;
	}
	return 0;
}

int
rpl_sem_trywait(rpl_sem_t * sem)
{
	int retval = pthread_mutex_lock(&sem->mutex);
	if (retval != 0) {
		errno = retval;
		return -1;
	}
	if (sem->count) {
		sem->count--;
		pthread_mutex_unlock(&sem->mutex);
		return 0;
	}
	errno = EAGAIN;
	pthread_mutex_unlock(&sem->mutex);
	return -1;
}

int
rpl_sem_post(rpl_sem_t * sem)
{
	int retval = pthread_mutex_lock(&sem->mutex);
	if (retval != 0) {
		errno = retval;
		return -1;
	}
	sem->count++;
	retval = pthread_cond_broadcast(&sem->cond);
	pthread_mutex_unlock(&sem->mutex);
	if (retval != 0) {
		errno = retval;
		return -1;
	}
	return 0;
}

int
rpl_sem_getvalue(rpl_sem_t * sem, int *sval)
{
	int retval = pthread_mutex_lock(&sem->mutex);
	if (retval != 0) {
		errno = retval;
		return -1;
	}
	*sval = sem->count;
	pthread_mutex_unlock(&sem->mutex);
	return 0;
}

int
rpl_sem_destroy(rpl_sem_t * sem)
{
	int retval = pthread_mutex_lock(&sem->mutex);
	if (retval != 0) {
		errno = retval;
		return -1;
	}
	sem->destroy_request = QB_TRUE;
	pthread_mutex_unlock(&sem->mutex);
	(void)pthread_cond_broadcast(&sem->cond);

	(void)pthread_cond_destroy(&sem->cond);
	(void)pthread_mutex_destroy(&sem->mutex);

	return 0;
}
