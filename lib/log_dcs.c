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
#ifdef HAVE_LINK_H
#include <link.h>
#endif
#include <stdarg.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include <qb/qbarray.h>
#include "log_int.h"

static qb_array_t *lookup_arr = NULL;
static qb_array_t *callsite_arr = NULL;
static uint32_t callsite_arr_next = 0;
static uint32_t callsite_elems_per_bin = 0;
static qb_thread_lock_t *arr_next_lock = NULL;

struct callsite_list {
	struct qb_log_callsite *cs;
	struct callsite_list *next;
};

static void
_log_register_callsites(qb_array_t * a, uint32_t bin)
{
	struct qb_log_callsite *start;
	struct qb_log_callsite *stop;
	int32_t rc = qb_array_index(callsite_arr,
				    bin * callsite_elems_per_bin,
				    (void **)&start);
	if (rc == 0) {
		stop = &start[callsite_elems_per_bin];
		rc = qb_log_callsites_register(start, stop);
		assert(rc == 0);
	}
}

static struct qb_log_callsite *
_log_dcs_new_cs(const char *message_id,
		const char *function,
		const char *filename,
		const char *format,
		uint8_t priority, uint32_t lineno, uint32_t tags)
{
	struct qb_log_callsite *cs;
	int32_t rc = qb_array_index(callsite_arr,
				    callsite_arr_next++,
				    (void **)&cs);
	assert(rc == 0);
	assert(cs != NULL);

	cs->message_id = message_id ? strdup(message_id) : NULL;
	cs->function = strdup(function);
	cs->filename = strdup(filename);
	cs->format = strdup(format);
	cs->priority = priority;
	cs->lineno = lineno;
	cs->tags = tags;

	return cs;
}

struct qb_log_callsite *
qb_log_dcs_get(int32_t * newly_created,
	       const char *message_id,
	       const char *function,
	       const char *filename,
	       const char *format,
	       uint8_t priority, uint32_t lineno, uint32_t tags)
{
	int32_t rc;
	struct qb_log_callsite *cs = NULL;
	struct callsite_list *csl_head;
	struct callsite_list *csl_last = NULL;
	struct callsite_list *csl;
	const char *safe_filename = filename;
	const char *safe_function = function;
	const char *safe_format = format;

	if (filename == NULL) {
		safe_filename = "";
	}
	if (function == NULL) {
		safe_function = "";
	}
	if (format == NULL) {
		safe_format = "";
	}

	/*
	 * try the fastest access first.
	 */
	rc = qb_array_index(lookup_arr, lineno, (void **)&csl_head);
	assert(rc == 0);

	/*
	 * Still need to lock here as we are reading from cs->* members
	 * and they might not be completely filled in yet by another thread
	 * that's running in _log_dcs_new_cs() below
	 */
	(void)qb_thread_lock(arr_next_lock);
	if (csl_head->cs &&
		priority == csl_head->cs->priority &&
		(message_id ? (strcmp(message_id, csl_head->cs->message_id) == 0) : 1) &&
		strcmp(safe_filename, csl_head->cs->filename) == 0 &&
		strcmp(safe_format, csl_head->cs->format) == 0) {
		(void)qb_thread_unlock(arr_next_lock);
		return csl_head->cs;
	}

	/*
	 * so we will either have to create it or go through a list
	 */
	if (csl_head->cs == NULL) {
		csl_head->cs = _log_dcs_new_cs(message_id, safe_function,
					       safe_filename, safe_format,
					       priority, lineno, tags);
		cs = csl_head->cs;
		csl_head->next = NULL;
		*newly_created = QB_TRUE;
	} else {
		for (csl = csl_head; csl; csl = csl->next) {
			assert(csl->cs->lineno == lineno);
			if (priority == csl->cs->priority &&
			    strcmp(safe_format, csl->cs->format) == 0 &&
			    strcmp(safe_filename, csl->cs->filename) == 0) {
				cs = csl->cs;
				break;
			}
			csl_last = csl;
		}

		if (cs == NULL) {
			csl = calloc(1, sizeof(struct callsite_list));
			if (csl == NULL) {
				goto cleanup;
			}
			csl->cs = _log_dcs_new_cs(message_id, safe_function,
						  safe_filename, safe_format,
						  priority, lineno, tags);
			csl->next = NULL;
			csl_last->next = csl;
			cs = csl->cs;
			*newly_created = QB_TRUE;
		}
	}
cleanup:
	(void)qb_thread_unlock(arr_next_lock);

	return cs;
}

void
qb_log_dcs_init(void)
{
	int32_t rc;

	lookup_arr = qb_array_create_2(16, sizeof(struct callsite_list), 1);
	callsite_arr = qb_array_create_2(16, sizeof(struct qb_log_callsite), 1);

	arr_next_lock = qb_thread_lock_create(QB_THREAD_LOCK_SHORT);

	callsite_elems_per_bin = qb_array_elems_per_bin_get(callsite_arr);
	rc = qb_array_new_bin_cb_set(callsite_arr, _log_register_callsites);
	assert(rc == 0);
}

void
qb_log_dcs_fini(void)
{
	struct callsite_list *csl_head;
	struct callsite_list *csl_next;
	struct callsite_list *csl;
	int32_t i;
	int32_t rc;
	struct qb_log_callsite *cs = NULL;
	int32_t cnt = qb_array_num_bins_get(lookup_arr);
	cnt *= qb_array_elems_per_bin_get(lookup_arr);

	for (i = 0; i < cnt; i++) {
		rc = qb_array_index(lookup_arr, i, (void **)&csl_head);
		if (rc != 0 || csl_head->next == NULL) {
			continue;
		}

		for (csl = csl_head->next; csl; csl = csl_next) {
			csl_next = csl->next;
			free(csl);
		}
	}

	for (i = 0; i < callsite_arr_next; i++) {
		rc = qb_array_index(callsite_arr, i, (void **)&cs);
		if (rc == 0 && cs){
			free((char*)cs->function);
			free((char*)cs->filename);
			free((char*)cs->format);
		}
	}

	qb_array_free(lookup_arr);
	qb_array_free(callsite_arr);
	(void)qb_thread_lock_destroy(arr_next_lock);
}
