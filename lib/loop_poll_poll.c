/*
 * Copyright (C) 2012 Red Hat, Inc.
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

#include "loop_poll_int.h"


static void
_fini(struct qb_poll_source *s)
{
}

static int32_t
_add(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events)
{
	return 0;
}

static int32_t
_mod(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events)
{
	return 0;
}

static int32_t
_del(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t i)
{
	s->ufds[i].fd = -1;
	s->ufds[i].events = 0;
	s->ufds[i].revents = 0;
	return 0;
}

static int32_t
_poll_and_add_to_jobs_(struct qb_loop_source *src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t new_jobs = 0;
	struct qb_poll_entry *pe;
	struct qb_poll_source *s = (struct qb_poll_source *)src;

	qb_poll_fds_usage_check_(s);

	for (i = 0; i < s->poll_entry_count; i++) {
		assert(qb_array_index(s->poll_entries, i, (void **)&pe) == 0);
		memcpy(&s->ufds[i], &pe->ufd, sizeof(struct pollfd));
	}

retry_poll:
	res = poll(s->ufds, s->poll_entry_count, ms_timeout);
	if (errno == EINTR && res == -1) {
		goto retry_poll;
	} else if (res == -1) {
		return -errno;
	}

	for (i = 0; i < s->poll_entry_count; i++) {
		if (s->ufds[i].fd == -1 || s->ufds[i].revents == 0) {
			/*
			 * empty entry
			 */
			continue;
		}
		assert(qb_array_index(s->poll_entries, i, (void **)&pe) == 0);
		if (pe->state != QB_POLL_ENTRY_ACTIVE ||
		    s->ufds[i].revents == pe->ufd.revents) {
			/*
			 * Wrong state to accept an event.
			 */
			continue;
		}
		pe->ufd.revents = s->ufds[i].revents;
		new_jobs += pe->add_to_jobs(src->l, pe);
	}

	return new_jobs;
}

int32_t
qb_poll_init(struct qb_poll_source *s)
{
	s->ufds = 0;
	s->driver.fini = _fini;
	s->driver.add = _add;
	s->driver.mod = _mod;
	s->driver.del = _del;
	s->s.poll = _poll_and_add_to_jobs_;
	return 0;
}

