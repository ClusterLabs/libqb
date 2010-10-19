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
#include "os_base.h"

#include <qb/qbarray.h>

#define MAX_BIN_ELEMENTS 256
#define MAX_BINS 256

#define BIN_NUM_GET(_idx_) (_idx_ >> 8)
#define ELEM_NUM_GET(_idx_) (_idx_ & 0xff)

struct qb_array {
	void *bin[MAX_BIN_ELEMENTS];
	size_t max_elements;
	size_t element_size;
	size_t num_bins;
};

qb_array_t* qb_array_create(size_t max_elements, size_t element_size)
{
	int32_t i;
	struct qb_array *a = calloc(1, sizeof(struct qb_array));

	if (max_elements > (MAX_BIN_ELEMENTS*MAX_BINS)) {
		errno = -EINVAL;
		return NULL;
	}
	if (element_size < 1) {
		errno = -EINVAL;
		return NULL;
	}
	a->element_size = element_size;
	a->max_elements = max_elements;
	a->num_bins = (max_elements / MAX_BIN_ELEMENTS) + 1;

	for (i = 0; i < MAX_BINS; i++) {
		if (i < a->num_bins) {
			a->bin[i] = calloc(MAX_BIN_ELEMENTS, element_size);
		} else {
			a->bin[i] = NULL;
		}
	}
	return a;
}

int32_t qb_array_index(struct qb_array* a, int32_t idx, void** element_out)
{
	int32_t b;
	int32_t elem;
	char *bin;

	if (a == NULL || element_out == NULL) {
		return -EINVAL;
	}
	if (idx >= a->max_elements || idx < 0) {
		return -EINVAL;
	}
	b = BIN_NUM_GET(idx);
	assert(b < a->num_bins);
	elem = ELEM_NUM_GET(idx);
	assert(elem < MAX_BIN_ELEMENTS);

	bin = a->bin[b];
	*element_out = (bin + (a->element_size * elem));
	return 0;
}

int32_t qb_array_grow(struct qb_array* a, size_t max_elements)
{
	int32_t i;
	int32_t old_bins;

	if (a == NULL || max_elements > (MAX_BIN_ELEMENTS*MAX_BINS)) {
		return -EINVAL;
	}
	if (max_elements <= a->max_elements) {
		return 0;
	}
	a->max_elements = max_elements;
	if (a->num_bins >= ((max_elements / MAX_BIN_ELEMENTS) + 1)) {
		return 0;
	}
	old_bins = a->num_bins;
	a->num_bins = ((max_elements / MAX_BIN_ELEMENTS) + 1);
	for (i = old_bins; i < a->num_bins; i++) {
		if (a->bin[i] == NULL) {
			a->bin[i] = calloc(MAX_BIN_ELEMENTS, a->element_size);
		}
	}
	return 0;
}

void qb_array_free(struct qb_array* a)
{
	int32_t i;
	for (i = 0; i < a->num_bins; i++) {
		free(a->bin[i]);
	}
	free(a);
}

