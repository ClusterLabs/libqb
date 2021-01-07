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
#include <qb/qbdefs.h>

/*
 * strlcpy() copies up to maxlen - 1 characters from the string src to dst,
 * NUL-terminating the result if maxlen is not 0.
 *
 * @return the total length of the string it tried to create
 *         (the length of src).
 */
size_t
strlcpy(char *dest, const char * src, size_t maxlen)
{
	size_t	srclen = strlen(src);
	size_t	len2cpy = QB_MIN(maxlen-1, srclen);

	/* check maxlen separately as it could have underflowed from 0 above. */
	if (maxlen) {
		if (len2cpy > 0) {
			strncpy(dest, src, len2cpy+1);
		}
		/* Always terminate, even if its empty */
		dest[len2cpy] = '\0';
	}
	return srclen;
}
