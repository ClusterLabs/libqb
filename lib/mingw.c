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


int
qb_sys_getrlimit(int resource, struct rlimit *rlp)
{
        if (resource != RLIMIT_NOFILE) {
                errno = EINVAL;
                return -1;
        }

        rlp->rlim_cur = 2048;
        return 0;
}


long
qb_sys_sysconf(int name)
{
	static long g_pagesize = 0;
	if (name == PAGESIZE || name == _SC_PAGESIZE) {
		if (!g_pagesize) {
			SYSTEM_INFO system_info;
			GetSystemInfo (&system_info);
			g_pagesize = system_info.dwPageSize;
		}
		return g_pagesize;
	} else {
                errno = EINVAL;
                return -1;
	}
}

struct tm *
qb_sys_localtime_r(const time_t *timep, struct tm *result)
{
        /* localtime() in MSVCRT.DLL is thread-safe, but not reentrant */
        memcpy(result, localtime(timep), sizeof(struct tm));
        return result;
}

char *
qb_strerror_r(int errnum, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%d", errnum);
	return buf;
}

QB_MMAP_FILE_HANDLE
qb_util_mmap_file_open(char *path, const char *file, size_t bytes,
		       uint32_t file_flags)
{
	HANDLE hMapFile;
	if (file_flags & O_CREAT) {
		hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE,
					     NULL, PAGE_READWRITE,
					     0, bytes * 2,
					     file);
	} else {
		hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS,
					   FALSE,
					   file);
		if (hMapFile == NULL) {
//			_tprintf(TEXT("Could not open file mapping object (%d).\n"),
//					GetLastError());
			return -1;
		}
	}
	return hMapFile;
}


int32_t
qb_util_circular_mmap(QB_MMAP_FILE_HANDLE fd, void **buf, size_t bytes)
{
	BYTE *pBuf;
	// determine valid buffer size
	SYSTEM_INFO info;
	GetSystemInfo(&info);

	// note that the base address must be a multiple of the allocation granularity
	DWORD bufferSize=info.dwAllocationGranularity;


	pBuf = (BYTE*)MapViewOfFile(fd,
			FILE_MAP_ALL_ACCESS,
			0,
			0,
			bytes);
	MapViewOfFileEx(fd,
			FILE_MAP_ALL_ACCESS,
			0,
			0,
			bytes,
			pBuf + bytes);
	*buf = pBuf;
	return 0;
}

void *
qb_sys_mmap(void *start, size_t length, int prot, int flags,
	     int fd, off_t offset)
{
	HANDLE handle;

	if (start != NULL || !(flags & MAP_PRIVATE)) {
		die("Invalid usage of mingw_mmap");
	}

	if (offset % getpagesize() != 0) {
		die("Offset does not match the memory allocation granularity");
	}
	handle = CreateFileMapping((HANDLE)_get_osfhandle(fd), NULL,
				   PAGE_WRITECOPY,
				   0, 0, NULL);

	if (handle != NULL) {
		start = MapViewOfFile(handle, FILE_MAP_COPY, 0, offset,
				      length);
		CloseHandle(handle);
	}

	return start;
}

int
qb_sys_munmap(void *start, size_t length)
{
	UnmapViewOfFile(start);
	return 0;
}

int32_t
qb_sys_fd_nonblock_cloexec_set(int32_t fd)
{
	u_long mode = 1;
	ioctlsocket(fd, FIONBIO, &mode);
	return 0;
}

int
qb_sys_pipe(int filedes[2])
{
	HANDLE h[2];

	/* this creates non-inheritable handles */
	if (!CreatePipe(&h[0], &h[1], NULL, 8192)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	filedes[0] = _open_osfhandle((int)h[0], O_NOINHERIT);
	if (filedes[0] < 0) {
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	filedes[1] = _open_osfhandle((int)h[1], O_NOINHERIT);
	if (filedes[0] < 0) {
		close(filedes[0]);
		CloseHandle(h[1]);
		return -1;
	}
	return 0;
}

int
qb_sys_poll(struct pollfd *fds, unsigned int nfds, int timeout)
{
	struct timeval timeout_v, *toptr;
	fd_set ifds, ofds, efds, *ip, *op;
	int i, rc;

	/* Set up the file-descriptor sets in ifds, ofds and efds. */
	FD_ZERO(&ifds);
	FD_ZERO(&ofds);
	FD_ZERO(&efds);
	for (i = 0, op = ip = 0; i < nfds; ++i) {
		fds[i].revents = 0;
		if(fds[i].events & (POLLIN|POLLPRI)) {
			ip = &ifds;
			FD_SET(fds[i].fd, ip);
		}
		if(fds[i].events & POLLOUT) {
			op = &ofds;
			FD_SET(fds[i].fd, op);
		}
		FD_SET(fds[i].fd, &efds);
	}

	/* Set up the timeval structure for the timeout parameter */
	if (timeout < 0) {
		toptr = 0;
	} else {
		toptr = &timeout_v;
		timeout_v.tv_sec = timeout / 1000;
		timeout_v.tv_usec = (timeout - timeout_v.tv_sec * 1000) * 1000;
	}

	rc = select(0, ip, op, &efds, toptr);
	if (rc <= 0)
		return rc;

	if (rc > 0) {
		for (i = 0; i < nfds; ++i) {
			int fd = fds[i].fd;
			if (fds[i].events & (POLLIN|POLLPRI) && FD_ISSET(fd, &ifds)) {
				fds[i].revents |= POLLIN;
			}
			if (fds[i].events & POLLOUT && FD_ISSET(fd, &ofds)) {
				fds[i].revents |= POLLOUT;
			}
			if (FD_ISSET(fd, &efds)) {
				fds[i].revents |= POLLHUP;
			}
		}
	}
	return rc;
}
