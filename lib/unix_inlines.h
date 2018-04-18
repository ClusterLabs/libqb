/*
 * Copyright 2011 Red Hat, Inc.
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

#ifndef QB_UNIX_INLINES_H_DEFINED
#define QB_UNIX_INLINES_H_DEFINED

#include "os_base.h"
#include "util_int.h"

#ifndef QB_UNIX_INLINES_INLINE
# define QB_UNIX_INLINES_INLINE static inline
#endif

QB_UNIX_INLINES_INLINE void
qb_sigpipe_ctl(enum qb_sigpipe_ctl ctl)
{
#if !defined(HAVE_MSG_NOSIGNAL) && !defined(HAVE_SO_NOSIGPIPE)
	struct sigaction act;
	struct sigaction oact;

	act.sa_handler = SIG_IGN;

	if (ctl == QB_SIGPIPE_IGNORE) {
		sigaction(SIGPIPE, &act, &oact);
	} else {
		sigaction(SIGPIPE, &oact, NULL);
	}
#endif  /* !MSG_NOSIGNAL && !SO_NOSIGPIPE */
}

QB_UNIX_INLINES_INLINE void
qb_socket_nosigpipe(int32_t s)
{
#if !defined(HAVE_MSG_NOSIGNAL) && defined(HAVE_SO_NOSIGPIPE)
	int32_t on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
#endif /* !MSG_NOSIGNAL && SO_NOSIGPIPE */
}

#endif
