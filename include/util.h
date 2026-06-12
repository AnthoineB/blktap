/*
 * Copyright (c) 2016, Citrix Systems, Inc.
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

#ifndef __TAPDISK_UTIL_H__
#define __TAPDISK_UTIL_H__

#include <stddef.h>
#include <string.h>

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof((_a)[0]))

/*
 * Strncpy variant that guarantees to terminate the string
 */
static inline void
safe_strncpy(char *dest, const char *src, size_t n)
{
	if (n > 0) {
		strncpy(dest, src, n - 1);
		dest[n - 1] = '\0';
	}
}

#endif /* __TAPDISK_UTIL_H__ */
