/*
 * Copyright (C) 2007 Alan Robertson <alanr@unix.sh>
 * This software licensed under the GNU LGPL.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include "os_base.h"

/*
 * strlcat() appends string src to the end of dst.  It will append at most
 * maxlen - strlen(dst) - 1 characters.  It will then NUL-terminate, unless
 * maxlen is 0 or the original dst string was longer than maxlen (in
 * practice this should not happen as it means that either maxlen is
 * incorrect or that dst is not a proper string).
 *
 * @return the total length of the string it tried to create
 *         (the length of dst plus the length of src).
 */
size_t
strlcat(char *dest, const char * src, size_t maxlen)
{
	size_t	curlen = strlen(dest);
	size_t	addlen = strlen(src);
	size_t	appendlen = maxlen - curlen;
	if (appendlen > 0) {
		strlcpy(dest+curlen, src, appendlen);
	}
	return curlen + addlen;
}
