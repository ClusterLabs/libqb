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
#ifndef QB_ARRAY_H_DEFINED
#define QB_ARRAY_H_DEFINED

#include <stdint.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <qb/qbdefs.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @file qbarray.h
 * This is a dynamic array (it can grow, but without moving memory).
 *
 * @code
 * arr = qb_array_create_2(64, sizeof(struct my_struct), 256);
 * ...
 * res = qb_array_index(arr, idx, (void**)&my_ptr);
 * if (res < 0) {
 *	return res;
 * }
 * // use my_ptr, now even if there is a grow, this pointer will be valid.
 * @endcode
 */

struct qb_array;

/**
 * This is an opaque data type representing an instance of an array.
 */
typedef struct qb_array qb_array_t;

/**
 * Create an array with fixed sized elements.
 *
 * @param max_elements initial max elements.
 * @param element_size size of each element.
 * @return array instance.
 */
qb_array_t* qb_array_create(size_t max_elements, size_t element_size);

/**
 * Create an array with fixed sized elements.
 *
 * @param max_elements initial max elements.
 * @param element_size size of each element.
 * @param autogrow_elements the number of elements to grow automatically by.
 *
 * @return array instance.
 */
qb_array_t* qb_array_create_2(size_t max_elements, size_t element_size,
                              size_t autogrow_elements);


/**
 * Get an element at a particular index.
 * @param a array instance.
 * @param idx the index
 * @param element_out the pointer to the element data.
 * @return (0 == success, else -errno)
 */
int32_t qb_array_index(qb_array_t* a, int32_t idx, void** element_out);

/**
 * Grow the array.
 *
 * @param a array instance.
 * @param max_elements the new maximum size of the array.
 * @return (0 == success, else -errno)
 */
int32_t qb_array_grow(qb_array_t* a, size_t max_elements);

/**
 * Get the number of bins used or the array.
 */
size_t qb_array_num_bins_get(qb_array_t* a);

/**
 * Get the number of elements per bin.
 */
size_t qb_array_elems_per_bin_get(qb_array_t* a);

/**
 * Free all the memory used by the array.
 * @param a array instance.
 */
void qb_array_free(qb_array_t * a);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_ARRAY_H_DEFINED */
