/*
 * Copyright (C) 2003 Sebastian Wilhelmi
 * Copyright (C) 2007 Nokia Corporation
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

/*
 * Copied basic locking backup method from the glib code base
 * (glib/gatomic.c) and namespaced for libqb.
 */

#include "os_base.h"

#include <qb/qbatomic.h>
#include <qb/qbutil.h>

/* We have to use the slow, but safe locking method */
static qb_thread_lock_t *qb_atomic_mutex = NULL;

void qb_atomic_init(void)
{
	if (qb_atomic_mutex == NULL) {
		qb_atomic_mutex = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	}
}

int32_t
qb_atomic_int_exchange_and_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t val)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	result = *atomic;
	*atomic += val;
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

void qb_atomic_int_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t val)
{
	qb_thread_lock(qb_atomic_mutex);
	*atomic += val;
	qb_thread_unlock(qb_atomic_mutex);
}

int32_t qb_atomic_int_compare_and_exchange(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
					   int32_t oldval, int32_t newval)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	if (*atomic == oldval) {
		result = QB_TRUE;
		*atomic = newval;
	} else {
		result = QB_FALSE;
	}
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

int32_t qb_atomic_pointer_compare_and_exchange(volatile void* QB_GNUC_MAY_ALIAS *
					       atomic, void* oldval, void* newval)
{
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	if (*atomic == oldval) {
		result = QB_TRUE;
		*atomic = newval;
	} else {
		result = QB_FALSE;
	}
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

#ifdef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED

int32_t(qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic) {
	int32_t result;

	qb_thread_lock(qb_atomic_mutex);
	result = *atomic;
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

void (qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t newval)
{
	qb_thread_lock(qb_atomic_mutex);
	*atomic = newval;
	qb_thread_unlock(qb_atomic_mutex);
}

void* (qb_atomic_pointer_get) (volatile void* QB_GNUC_MAY_ALIAS * atomic)
{
	void* result;

	qb_thread_lock(qb_atomic_mutex);
	result = *atomic;
	qb_thread_unlock(qb_atomic_mutex);

	return result;
}

void (qb_atomic_pointer_set) (volatile void* QB_GNUC_MAY_ALIAS * atomic,
			 void* newval)
{
	qb_thread_lock(qb_atomic_mutex);
	*atomic = newval;
	qb_thread_unlock(qb_atomic_mutex);
}

int32_t (qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic)
{
	QB_ATOMIC_MEMORY_BARRIER;
	return *atomic;
}

void (qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t newval)
{
	*atomic = newval;
	QB_ATOMIC_MEMORY_BARRIER;
}

void* (qb_atomic_pointer_get) (volatile void* QB_GNUC_MAY_ALIAS * atomic)
{
	QB_ATOMIC_MEMORY_BARRIER;
	return *atomic;
}

void (qb_atomic_pointer_set) (volatile void* QB_GNUC_MAY_ALIAS * atomic,
			      void* newval)
{
	*atomic = newval;
	QB_ATOMIC_MEMORY_BARRIER;
}

#endif /* QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */
#ifndef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED

int32_t (qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic)
{
	return qb_atomic_int_get(atomic);
}

void (qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
			  int32_t newval)
{
	qb_atomic_int_set(atomic, newval);
}

void* (qb_atomic_pointer_get) (volatile void* QB_GNUC_MAY_ALIAS * atomic)
{
	return qb_atomic_pointer_get(atomic);
}

void (qb_atomic_pointer_set) (volatile void* QB_GNUC_MAY_ALIAS * atomic,
			      void* newval)
{
	qb_atomic_pointer_set(atomic, newval);
}
#endif /* QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */
