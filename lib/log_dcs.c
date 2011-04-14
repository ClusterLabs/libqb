/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * All rights reserved.
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
#include "os_base.h"
#include <ctype.h>
#include <link.h>
#include <stdarg.h>

#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include <qb/qbarray.h>
#include "log_int.h"

static qb_array_t *lookup_arr = NULL;
static qb_array_t *callsite_arr = NULL;
static uint32_t callsite_arr_next = 0;
static uint32_t callsite_num_bins = 0;
static uint32_t callsite_elems_per_bin = 0;

struct callsite_list {
	struct qb_log_callsite *cs;
	struct qb_list_head list;
};


static void _log_register_callsites(void)
{
	struct qb_log_callsite* start;
	struct qb_log_callsite* stop;
	int32_t b;
	int32_t rc;
	uint32_t num_bins = qb_array_num_bins_get(callsite_arr);

	for (b = callsite_num_bins; b < num_bins; b++) {
		/* get the first element in the bin */
		rc = qb_array_index(callsite_arr,
				    b * callsite_elems_per_bin,
				    (void**)&start);
		if (rc == 0) {
			stop = &start[callsite_elems_per_bin];
			qb_log_callsites_register(start, stop);
		}
	}
	callsite_num_bins = num_bins;
}

static struct qb_log_callsite *
_log_dcs_new_cs(const char *function,
		const char *filename,
		const char *format,
		uint8_t priority,
		uint32_t lineno,
		uint32_t tags)
{
	struct qb_log_callsite *cs;
	int32_t rc;
       
	if (qb_array_index(callsite_arr, callsite_arr_next, (void**)&cs) < 0) {
		rc = qb_array_grow(callsite_arr, callsite_arr_next + 255);
		assert(rc == 0);
		rc = qb_array_index(callsite_arr, callsite_arr_next, (void**)&cs);
		assert(rc == 0);
		assert(cs != NULL);
		_log_register_callsites();
	}
	callsite_arr_next++;

	cs->function = function;
	cs->filename = filename;
	cs->format = format;
	cs->priority = priority;
	cs->lineno = lineno;
	cs->tags = tags;

	return cs;
}

struct qb_log_callsite *
qb_log_dcs_get(int32_t *newly_created,
	       const char *function,
	       const char *filename,
	       const char *format,
	       uint8_t priority,
	       uint32_t lineno,
	       uint32_t tags)
{
	int32_t rc;
	struct qb_log_callsite *cs;
	struct callsite_list *csl_head;
	struct callsite_list *csl_next;
	struct callsite_list *csl;
	struct qb_list_head *iter = NULL;

	rc = qb_array_index(lookup_arr, lineno, (void**)&csl_head);
	if (rc < 0) {
		rc = qb_array_grow(lookup_arr, lineno + 255);
		assert(rc == 0);
		rc = qb_array_index(lookup_arr, lineno, (void**)&csl_head);
		assert(rc == 0);
	}
	if (csl_head->cs == NULL) {
		csl_head->cs = _log_dcs_new_cs(function, filename, format,
				     priority, lineno, tags);
		cs = csl_head->cs;
		qb_list_init(&csl_head->list);
		*newly_created = QB_TRUE;
	} else {
		csl_next = csl_head;
		do {
			csl = csl_next;
			if (strcmp(filename, csl->cs->filename) == 0) {
				cs = csl->cs;
				break;
			}
			if (iter == NULL) {
				iter = csl_head->list.next;
			} else {
				iter = iter->next;
			}
			csl_next = qb_list_entry(iter, struct callsite_list, list);
		} while (iter != &csl_head->list);
		if (cs == NULL) {
			/*
			 * create new list entry
			 */
			csl_next = calloc(1, sizeof(struct callsite_list));
			csl_next->cs = _log_dcs_new_cs(function, filename, format,
						       priority, lineno, tags);
			cs = csl_next->cs;
			qb_list_init(&csl_next->list);
			qb_list_add(&csl_next->list, &csl_head->list);
			*newly_created = QB_TRUE;
		}
	}
	return cs;
}


void qb_log_dcs_init(void)
{
	lookup_arr =  qb_array_create(256, sizeof(struct callsite_list));
	callsite_arr =  qb_array_create(256, sizeof(struct callsite_list));

	callsite_elems_per_bin = qb_array_elems_per_bin_get(callsite_arr);
	_log_register_callsites();
}

void qb_log_dcs_fini(void)
{
}

