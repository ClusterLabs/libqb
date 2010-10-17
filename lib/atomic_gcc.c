/*
 * Copyright (C) 2009 Hiroyuki Ikezoe
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
 * Copied from the glib code base (glib/gatomic-gcc.c) and namespaced
 * for libqb.
 */

#include "config.h"
#include <qb/qbatomic.h>

void qb_atomic_init(void)
{
}

int32_t qb_atomic_int_exchange_and_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
				       int32_t val)
{
	return __sync_fetch_and_add(atomic, val);
}

void qb_atomic_int_add(volatile int32_t QB_GNUC_MAY_ALIAS * atomic, int32_t val)
{
	__sync_fetch_and_add(atomic, val);
}

int32_t qb_atomic_int_compare_and_exchange(volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
					   int32_t oldval, int32_t newval)
{
	return __sync_bool_compare_and_swap(atomic, oldval, newval);
}

int32_t qb_atomic_pointer_compare_and_exchange(volatile void* QB_GNUC_MAY_ALIAS * atomic,
					       void* oldval, void* newval)
{
	return __sync_bool_compare_and_swap(atomic, oldval, newval);
}

int32_t (qb_atomic_int_get) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic)
{
	__sync_synchronize();
	return *atomic;
}

void (qb_atomic_int_set) (volatile int32_t QB_GNUC_MAY_ALIAS * atomic,
			  int32_t newval)
{
	*atomic = newval;
	__sync_synchronize();
}

void* (qb_atomic_pointer_get) (volatile void* QB_GNUC_MAY_ALIAS * atomic)
{
	__sync_synchronize();
	return (void*)*atomic;
}

void (qb_atomic_pointer_set) (volatile void* QB_GNUC_MAY_ALIAS * atomic,
			      void* newval)
{
	*atomic = newval;
	__sync_synchronize();
}
