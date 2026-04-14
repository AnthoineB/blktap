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

#ifndef _TD_BLKTAP_H_
#define _TD_BLKTAP_H_

#define BLKTAP2_CONTROL_NAME           "blktap/control"
#define BLKTAP2_CONTROL_DIR            "/run/blktap-control"
#define BLKTAP2_NP_RUN_DIR             BLKTAP2_CONTROL_DIR"/tapdisk"
#define BLKTAP2_CONTROL_SOCKET         "ctl"
#define BLKTAP2_ENOSPC_SIGNAL_FILE     "/run/tapdisk-enospc"

/* Maximum number of possible minor ids, to match old kernel definition */
#define MAX_ID  16384

#endif /* _TD_BLKTAP_H_ */
