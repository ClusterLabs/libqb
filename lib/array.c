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
#include <qb/qbutil.h>

#define MAX_ELEMENTS_PER_BIN 16
#define MAX_BINS 4096

#define BIN_NUM_GET(_idx_) (_idx_ >> 4)
#define ELEM_NUM_GET(_idx_) (_idx_ & 0x0F)

struct qb_array {
	void **bin;
	size_t max_elements;
	size_t element_size;
	size_t num_bins;
	size_t autogrow_elements;
	qb_thread_lock_t *grow_lock;
};

qb_array_t *
qb_array_create(size_t max_elements, size_t element_size)
{
	return qb_array_create_2(max_elements, element_size, 0);
}

static int32_t
_grow_bin_array(struct qb_array * a, int32_t new_bin_size)
{
	int32_t b;

	a->bin = realloc(a->bin, sizeof(void*) * new_bin_size);
	if (a->bin == NULL) {
		return -ENOMEM;
	}
	for (b = a->num_bins; b < new_bin_size; b++) {
		a->bin[b] = NULL;
	}
	a->num_bins = new_bin_size;

	return 0;
}

qb_array_t *
qb_array_create_2(size_t max_elements, size_t element_size,
		  size_t autogrow_elements)
{
	struct qb_array *a = NULL;
	int32_t b;

	if (max_elements > (MAX_ELEMENTS_PER_BIN * MAX_BINS)) {
		errno = -EINVAL;
		return NULL;
	}
	if (element_size < 1) {
		errno = -EINVAL;
		return NULL;
	}
	if (autogrow_elements > MAX_ELEMENTS_PER_BIN) {
		errno = -EINVAL;
		return NULL;
	}
	a = calloc(1, sizeof(struct qb_array));
	if (a == NULL) {
		return NULL;
	}
	a->element_size = element_size;
	a->max_elements = max_elements;
	b = QB_MIN((max_elements / MAX_ELEMENTS_PER_BIN) + 1, MAX_BINS);
	a->autogrow_elements = autogrow_elements;
	a->bin = NULL;
	if (_grow_bin_array(a, b) < 0) {
		free(a);
		return NULL;
	}
	a->grow_lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);
	return a;
}

int32_t
qb_array_index(struct qb_array * a, int32_t idx, void **element_out)
{
	int32_t b;
	int32_t elem;
	char *bin;
	int32_t rc = 0;

	if (a == NULL || element_out == NULL) {
		return -EINVAL;
	}
	if (idx < 0) {
		return -ERANGE;
	}
	if (idx >= a->max_elements) {
		if (a->autogrow_elements == 0) {
			return -ERANGE;
		} else {
			rc = qb_array_grow(a, idx + 1);
			if (rc != 0) {
				return rc;
			}
		}
	}
	b = BIN_NUM_GET(idx);
	assert(b < MAX_BINS);

	if (b >= a->num_bins || a->bin[b] == NULL) {

		(void)qb_thread_lock(a->grow_lock);

		if (b >= a->num_bins) {
			rc = _grow_bin_array(a, b + 1);
			if (rc < 0) {
				goto unlock_error;
			}
		}
		if (a->bin[b] == NULL) {
			a->bin[b] = calloc(MAX_ELEMENTS_PER_BIN, a->element_size);
			if (a->bin[b] == NULL) {
				goto unlock_error;
			}
		}

		(void)qb_thread_unlock(a->grow_lock);
	}

	elem = ELEM_NUM_GET(idx);
	assert(elem < MAX_ELEMENTS_PER_BIN);

	bin = a->bin[b];
	*element_out = (bin + (a->element_size * elem));

	return 0;

unlock_error:

	(void)qb_thread_unlock(a->grow_lock);
	return rc;
}

size_t
qb_array_num_bins_get(struct qb_array * a)
{
	if (a == NULL) {
		return -EINVAL;
	}
	return a->num_bins;
}

size_t
qb_array_elems_per_bin_get(struct qb_array * a)
{
	if (a == NULL) {
		return -EINVAL;
	}
	return MAX_ELEMENTS_PER_BIN;
}

int32_t
qb_array_grow(struct qb_array * a, size_t max_elements)
{
	int32_t b;
	int32_t rc = 0;

	if (a == NULL || max_elements > (MAX_ELEMENTS_PER_BIN * MAX_BINS)) {
		return -EINVAL;
	}
	if (max_elements <= a->max_elements) {
		return 0;
	}
	a->max_elements = max_elements;
	b = QB_MIN((max_elements / MAX_ELEMENTS_PER_BIN) + 1, MAX_BINS);
	if (b > a->num_bins) {
		(void)qb_thread_lock(a->grow_lock);
		if (b >= a->num_bins) {
			rc = _grow_bin_array(a, b + 1);
		}
		(void)qb_thread_unlock(a->grow_lock);
	}
	return rc;
}

void
qb_array_free(struct qb_array *a)
{
	int32_t i;
	for (i = 0; i < a->num_bins; i++) {
		free(a->bin[i]);
	}
	free(a->bin);
	(void)qb_thread_lock_destroy(a->grow_lock);
	free(a);
}
