/*
 * Copyright (c) 2020, Citrix Systems, Inc.
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

#ifndef LIBAIO_BACKEND_H
#define LIBAIO_BACKEND_H

#include <libaio.h>

#include "io-optimize.h"
#include "scheduler.h"
#include "io-backend.h"

enum {
	TIO_DRV_LIO     = 1,
};

struct backend* get_libaio_backend();

#endif /* LIBAIO_BACKEND_H */
