/*
 * Copyright (C) 2003 Sebastian Wilhelmi
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
 * Copied from the glib code base (glib/gatomic.h) and namespaced
 * for libqb.
 */

#ifndef QB_ATOMIC_H_DEFINED
#define QB_ATOMIC_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include <stdint.h>
#include <qb/qbdefs.h>
#include <qb/qbconfig.h>

/**
 * @file
 * Basic atomic integer and pointer operations
 *
 * The following functions can be used to atomically access integers and
 * pointers. They are implemented as inline assembler function on most
 * platforms and use slower fall-backs otherwise. Using them can sometimes
 * save you from using a performance-expensive pthread_mutex to protect the
 * integer or pointer.
 *
 * The most important usage is reference counting. Using
 * qb_atomic_int_inc() and qb_atomic_int_dec_and_test() makes reference
 * counting a very fast operation.
 *
 * You must not directly read integers or pointers concurrently
 * accessed by multiple threads, but use the atomic accessor functions
 * instead. That is, always use qb_atomic_int_get() and qb_atomic_pointer_get()
 * for read outs. They provide the necessary synchronization mechanisms
 * like memory barriers to access memory locations concurrently.
 *
 * If you are using those functions for anything apart from
 * simple reference counting, you should really be aware of the implications
 * of doing that. There are literally thousands of ways to shoot yourself
 * in the foot. So if in doubt, use a pthread_mutex. If you don't know, what
 * memory barriers are, do not use anything but qb_atomic_int_inc() and
 * qb_atomic_int_dec_and_test().
 *
 * It is not safe to set an integer or pointer just by assigning
 * to it, when it is concurrently accessed by other threads with the following
 * functions. Use qb_atomic_int_compare_and_exchange() or
 * qb_atomic_pointer_compare_and_exchange() respectively.
 */

void qb_atomic_init(void);

/**
 * Atomically adds val to the integer pointed to by atomic.
 * It returns the value of *atomic just before the addition
 * took place. Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 * @param val the value to add to *atomic
 * @return the value of *atomic before the addition.
 */
int32_t qb_atomic_int_exchange_and_add(volatile int32_t QB_GNUC_MAY_ALIAS *
				      atomic, int32_t val);
/**
 * Atomically adds val to the integer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 * @param val the value to add to *atomic
 */
void qb_atomic_int_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t val);

/**
 * Compares oldval with the integer pointed to by atomic and
 * if they are equal, atomically exchanges *atomic with newval.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 * @param oldval the assumed old value of *atomic
 * @param newval the new value of *atomic
 *
 * @return QB_TRUE, if *atomic was equal oldval. QB_FALSE otherwise.
 */
int32_t qb_atomic_int_compare_and_exchange(volatile int32_t QB_GNUC_MAY_ALIAS *
					  atomic, int32_t oldval,
					  int32_t newval);

/**
 * Compares oldval with the pointer pointed to by atomic and
 * if they are equal, atomically exchanges *atomic with newval.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to a void*
 * @param oldval the assumed old value of *atomic
 * @param newval the new value of *atomic
 *
 * @return QB_TRUE if atomic was equal oldval, else QB_FALSE.
 */
int32_t qb_atomic_pointer_compare_and_exchange(volatile void* QB_GNUC_MAY_ALIAS
					      * atomic, void* oldval,
					      void* newval);

/**
 * Reads the value of the integer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 *
 * @return the value of atomic
 */
int32_t qb_atomic_int_get(volatile int32_t QB_GNUC_MAY_ALIAS * atomic);

/**
 * Sets the value of the integer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to an integer
 * @param newval the new value
 */
void qb_atomic_int_set(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
		      int32_t newval);

/**
 * Reads the value of the pointer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to a void*.
 * @return the value to add to atomic.
 */
void* qb_atomic_pointer_get(volatile void* QB_GNUC_MAY_ALIAS * atomic);

/**
 * Sets the value of the pointer pointed to by atomic.
 * Also acts as a memory barrier.
 *
 * @param atomic a pointer to a void*
 * @param newval the new value
 *
 */
void qb_atomic_pointer_set(volatile void* QB_GNUC_MAY_ALIAS * atomic,
			  void* newval);

#ifndef QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED
#define qb_atomic_int_get(atomic) 		((int32_t)*(atomic))
#define qb_atomic_int_set(atomic, newval) 	((void) (*(atomic) = (newval)))
#define qb_atomic_pointer_get(atomic) 		((void*)*(atomic))
#define qb_atomic_pointer_set(atomic, newval)	((void*) (*(atomic) = (newval)))
#else
#define qb_atomic_int_get(atomic) \
 ((void) sizeof (char* [sizeof (*(atomic)) == sizeof (int32_t) ? 1 : -1]), \
  (qb_atomic_int_get) ((volatile int32_t QB_GNUC_MAY_ALIAS *) (volatile void *) (atomic)))
#define qb_atomic_int_set(atomic, newval) \
 ((void) sizeof (char* [sizeof (*(atomic)) == sizeof (int32_t) ? 1 : -1]), \
  (qb_atomic_int_set) ((volatile int32_t QB_GNUC_MAY_ALIAS *) (volatile void *) (atomic), (newval)))
#define qb_atomic_pointer_get(atomic) \
 ((void) sizeof (char* [sizeof (*(atomic)) == sizeof (void*) ? 1 : -1]), \
  (qb_atomic_pointer_get) ((volatile void* QB_GNUC_MAY_ALIAS *) (volatile void *) (atomic)))
#define qb_atomic_pointer_set(atomic, newval) \
 ((void) sizeof (char* [sizeof (*(atomic)) == sizeof (void*) ? 1 : -1]), \
  (qb_atomic_pointer_set) ((volatile void* QB_GNUC_MAY_ALIAS *) (volatile void *) (atomic), (newval)))
#endif /* QB_ATOMIC_OP_MEMORY_BARRIER_NEEDED */

/**
 * Atomically increments the integer pointed to by atomic by 1.
 *
 * @param atomic a pointer to an integer.
 */
#define qb_atomic_int_inc(atomic) (qb_atomic_int_add ((atomic), 1))

/**
 * Atomically decrements the integer pointed to by atomic by 1.
 *
 * @param atomic a pointer to an integer
 *
 * @return QB_TRUE if the integer pointed to by atomic is 0
 *     after decrementing it
 */
#define qb_atomic_int_dec_and_test(atomic) \
  (qb_atomic_int_exchange_and_add ((atomic), -1) == 1)

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_ATOMIC_H_DEFINED */
