/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#ifndef QB_ATOMIC_INT_H_DEFINED
#define QB_ATOMIC_INT_H_DEFINED

/*
 * This adds some extra atomic functionality, building on the
 * gcc atomic builtins.
 */

#include "os_base.h"
#include <qb/qbdefs.h>
#include <qb/qbatomic.h>

/* This is a thin wrapper around the new gcc atomics.
 */
enum qb_atomic_model {
	QB_ATOMIC_RELAXED,
	QB_ATOMIC_CONSUME,
	QB_ATOMIC_ACQUIRE,
	QB_ATOMIC_RELEASE,
	QB_ATOMIC_ACQ_REL,
	QB_ATOMIC_SEQ_CST,
};

#ifdef HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS
static inline int
qb_model_map(enum qb_atomic_model model)
{
	switch (model) {
	case QB_ATOMIC_ACQUIRE:
		return __ATOMIC_ACQUIRE;
	case QB_ATOMIC_RELEASE:
		return __ATOMIC_RELEASE;
	case QB_ATOMIC_RELAXED:
		return __ATOMIC_RELAXED;
	case QB_ATOMIC_CONSUME:
		return __ATOMIC_CONSUME;
	case QB_ATOMIC_ACQ_REL:
		return __ATOMIC_ACQ_REL;
	case QB_ATOMIC_SEQ_CST:
	default:
		return __ATOMIC_SEQ_CST;
	}
}
#endif /* HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS */

#ifdef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED

#ifdef HAVE_GCC_BUILTINS_FOR_SYNC_OPERATIONS
#define QB_ATOMIC_MEMORY_BARRIER __sync_synchronize ()
#else
#ifndef HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS
#warning you need memory barriers but do not have an implementation.
#endif /* HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS */
#endif /* HAVE_GCC_BUILTINS_FOR_SYNC_OPERATIONS */

#ifndef QB_ATOMIC_MEMORY_BARRIER
#define QB_ATOMIC_MEMORY_BARRIER
#endif /* QB_ATOMIC_MEMORY_BARRIER */

#endif /* QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */

/**
 * Reads the value of the integer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 * @param model the memory model to use.
 *
 * @return the value of atomic
 */
static inline int32_t
qb_atomic_int_get_ex(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
		     enum qb_atomic_model model)
{
#ifdef HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS
	return __atomic_load_n(atomic, qb_model_map(model));
#else
	return qb_atomic_int_get(atomic);
#endif
}


/**
 * Sets the value of the integer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 * @param newval the new value
 * @param model the memory model to use.
 */
static inline void
qb_atomic_int_set_ex(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
		     int32_t newval,
		     enum qb_atomic_model model)
{
#ifdef HAVE_GCC_BUILTINS_FOR_ATOMIC_OPERATIONS
	__atomic_store_n(atomic, newval, qb_model_map(model));
#else
/*
 * If the model is acquire we need the barrier afterwards,
 * and if its cst we need it before and after.
 * Note: qb_atomic_int_set already has a memory barrier after
 * the set.
 */
	if (model != QB_ATOMIC_RELAXED && model != QB_ATOMIC_CONSUME) {
		QB_ATOMIC_MEMORY_BARRIER;
	}
	qb_atomic_int_set(atomic, newval);
#endif
}

#endif /* QB_ATOMIC_INT_H_DEFINED */
