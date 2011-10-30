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

//typedef int pid_t;
//typedef int uid_t;
typedef int socklen_t;

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 0x2
#define MAP_FAILED (void*)-1


#define EIDRM EINVAL
#define ENOMSG EINVAL
#define EBADMSG EINVAL


#define ENOTCONN        WSAENOTCONN
#define EWOULDBLOCK     WSAEWOULDBLOCK
#define ENOBUFS         WSAENOBUFS
#define ECONNRESET      WSAECONNRESET
#define ESHUTDOWN       WSAESHUTDOWN
#define EAFNOSUPPORT    WSAEAFNOSUPPORT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#define EINPROGRESS     WSAEINPROGRESS
#define EISCONN         WSAEISCONN

#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_ISUID 0
#define S_ISGID 0
#define S_ISVTX 0


//#define SHUT_WR SD_SEND

/*
 * timespec
 */
#include <pthread.h>


#ifndef QB_HAVE_IOVEC
#define QB_HAVE_IOVEC 1
struct iovec {
    void *iov_base;   /* Starting address */
    size_t iov_len;   /* Number of bytes */
};
#endif

#ifndef NAME_MAX

#define NAME_MAX 256
#endif

struct msghdr {
    void         *msg_name;       /* optional address */
    socklen_t     msg_namelen;    /* size of address */
    struct iovec *msg_iov;        /* scatter/gather array */
    size_t        msg_iovlen;     /* # elements in msg_iov */
    void         *msg_control;    /* ancillary data, see below */
    socklen_t     msg_controllen; /* ancillary data buffer len */
    int           msg_flags;      /* flags on received message */
};

#define sendmsg qb_sys_sendmsg
int qb_sys_sendmsg(int s, const struct msghdr *msg, int flags);


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
 * sysconf
 */
#define PAGESIZE 1
#define _SC_PAGESIZE 2
#define sysconf qb_sys_sysconf
long qb_sys_sysconf(int name);

#define chown qb_sys_chown
int qb_sys_chown(const char *path, uid_t owner, gid_t group);

/*
 * mmap & munmap
 */
#define mmap qb_sys_mmap
#define munmap qb_sys_munmap
void *qb_sys_mmap(void *start, size_t length, int prot, int flags,
	     int fd, off_t offset);
int qb_sys_munmap(void *start, size_t length);


/*
 * poll
 */
#define POLLIN      0x0001    /* There is data to read */
#define POLLPRI     0x0002    /* There is urgent data to read */
#define POLLOUT     0x0004    /* Writing now will not block */
#define POLLERR     0x0008    /* Error condition */
#define POLLHUP     0x0010    /* Hung up */
#define POLLNVAL    0x0020    /* Invalid request: fd not open */

struct pollfd {
    int fd;
    short events;
    short revents;
};
#define poll qb_sys_poll
int qb_sys_poll(struct pollfd *fds, unsigned int nfds, int timeout);

/*
 * pipe
 */
#define pipe qb_sys_pipe
int qb_sys_pipe(int filedes[2]);


#endif /* QB_MINGW_H_DEFINED */
