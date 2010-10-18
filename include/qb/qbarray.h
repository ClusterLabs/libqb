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
#include <unistd.h>
#include <qb/qbdefs.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @file qbarray.h
 */

struct qb_array;

/**
 *
 */
typedef struct qb_array qb_array_t;

/**
 *
 */
qb_array_t* qb_array_create(size_t max_elements, size_t element_size);

/**
 *
 */
int32_t qb_array_index(qb_array_t* a, int32_t idx, void** element_out);

/**
 *
 */
int32_t qb_array_grow(qb_array_t* a, size_t max_elements);

/**
 *
 */
void qb_array_free(qb_array_t * a);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_ARRAY_H_DEFINED */
