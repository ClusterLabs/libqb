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

#ifndef QB_TSAFE_H_DEFINED
#define QB_TSAFE_H_DEFINED

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * Initialize the thread safe checker.
 *
 * It will make a read-only copy of your environ array
 * and (if tsafe is on) use this copy for getenv() calls.
 * @param envp your environment (as passed into main())
 */
extern void qb_tsafe_init(char **envp);

/**
 * turn on the thread safe checker.
 *
 * It will assert() on any call to a thread unsafe system call
 * as defined in pthread(5)
 */
extern void qb_tsafe_on(void);

/**
 * turn off the thread safe checker.
 *
 * It is fine to do this if you have only one thread.
 */
extern void qb_tsafe_off(void);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_TSAFE_H_DEFINED */
