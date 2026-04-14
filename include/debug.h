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

#ifndef __TAPDISK_DEBUG_H__
#define __TAPDISK_DEBUG_H__

#include <syslog.h>
#include <stdlib.h>

#ifdef NDEBUG
#define ASSERT(condition)	\
	do {					\
		;					\
	} while(0);
#else
#define ASSERT(condition)													\
	do {																	\
		if (!(condition)) {													\
			syslog(LOG_ERR, "%s:%d: %s: Assertion `%s' failed.", __FILE__,	\
					__LINE__, __func__, __STRING(condition));				\
			abort();														\
		}																	\
	} while (0);
#endif

#endif /* __TAPDISK_DEBUG_H__ */
