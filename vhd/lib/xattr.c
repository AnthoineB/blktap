/*
 * Copyright (c) 2018, Citrix Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.1 only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "xattr.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <features.h>
#include <sys/xattr.h>

#ifndef ENOATTR
# define ENOATTR ENODATA        /* No such attribute */
#endif

int
xattr_get(int fd, const char *name, void *value, size_t size)
{
	if (fgetxattr(fd, name, value, size) == -1) {
		if ((errno == ENOATTR) || (errno == ENOTSUP)) {
			memset(value, 0, size);
			return 0;
		}
		return -errno;
	}

	return 0;
}

int
xattr_set(int fd, const char *name, const void *value, size_t size)
{
	if (fsetxattr(fd, name, value, size, 0) == -1)
		return -errno;
	return 0;
}
