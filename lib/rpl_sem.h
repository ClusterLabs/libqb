/*
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * Modified by: Angus Salkeld <asalkeld@redhat.com>
 *              Copyright (C) 2012 Red Hat, Inc.
 * To conform to official implementation and support process shared semaphores.
 *
 * The bsd posix semaphore implementation does not have support for timing
 * out while waiting for a synchronization object. This uses the
 * pthread_cond_timedwait function and a mutex to build all the other
 * synchronization objecs with timeout capabilities.
 */

#ifndef _RPL_SEM_H
#define _RPL_SEM_H

#include "os_base.h"
#include <pthread.h>
#include <semaphore.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef HAVE_SEM_TIMEDWAIT
#define rpl_sem_t sem_t
#define rpl_sem_init sem_init
#define rpl_sem_wait sem_wait
#define rpl_sem_timedwait sem_timedwait
#define rpl_sem_post sem_post
#define rpl_sem_getvalue sem_getvalue
#define rpl_sem_destroy sem_destroy
#define rpl_sem_trywait sem_trywait
#else

typedef struct rpl_sem {
	unsigned int count;
	uint32_t destroy_request;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} rpl_sem_t;

int rpl_sem_init(rpl_sem_t * sem, int pshared, unsigned int count);

int rpl_sem_wait(rpl_sem_t * sem);

int rpl_sem_timedwait(rpl_sem_t * sem, const struct timespec *timeout);

int rpl_sem_trywait(rpl_sem_t * sem);

int rpl_sem_post(rpl_sem_t * sem);

int rpl_sem_getvalue(rpl_sem_t * sem, int *sval);

int rpl_sem_destroy(rpl_sem_t * sem);

#endif /* HAVE_SEM_TIMEDWAIT */

#ifdef  __cplusplus
}
#endif
#endif /* _RPL_SEM_H */
