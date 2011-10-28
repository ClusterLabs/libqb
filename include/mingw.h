/*
 * Copyright (C) 2011 Red Hat, Inc.
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
#ifndef QB_MINGW_H_DEFINED
#define QB_MINGW_H_DEFINED

/*
 * getrlimit
 */
struct rlimit {
       unsigned int rlim_cur;
};

#define RLIMIT_NOFILE 0
#define getrlimit qb_sys_getrlimit
int qb_sys_getrlimit(int resource, struct rlimit *rlp);

/*
 * timespec
 */
struct timespec {
	time_t tv_sec;
	long tv_nsec;
};


/*
 * sysconf
 */
#define PAGESIZE 1
#define _SC_PAGESIZE 2
#define sysconf qb_sys_sysconf
long qb_sys_sysconf(int name);

/*
 * poll
 */
#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020

struct pollfd {
    int fd;
    short events;
    short revents;
};
int qb_sys_poll(struct pollfd *fds, unsigned int nfds, int timeout);

/*
 * pipe
 */
#define pipe qb_sys_pipe
int qb_sys_pipe(int filedes[2]);


#endif /* QB_MINGW_H_DEFINED */
