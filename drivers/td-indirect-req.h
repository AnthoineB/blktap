/*
 * Copyright (c) 2025, Vates
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the names of its 
 *     contributors may be used to endorse or promote products derived from 
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __TD_INDIRECT_REQ_H__
#define __TD_INDIRECT_REQ_H__

#include "tapdisk.h"
#include "blktap-xenif.h"
#include <sys/types.h>
#include <xen/io/blkif.h>
#include <xen/gntdev.h>
#include "td-blkif.h"

#define TD_MAX_INDIRECT_SEGMENTS (16)
#define TD_TOTAL_INDIRECT_SEGMENTS (TD_MAX_INDIRECT_SEGMENTS * BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST)
#define TD_REQ_BIG_BUFFER_SIZE (TD_TOTAL_INDIRECT_SEGMENTS << PAGE_SHIFT)

void
td_xenblkif_bigbufcache_put(struct td_xenblkif * const blkif, void *buf);
void
td_xenblkif_bigbufcache_free(struct td_xenblkif * const blkif);

int
tapdisk_xenblkif_parse_request_indirect(struct td_xenblkif * const blkif,
					struct td_xenblkif_req * const req);

/**
 * Tells whether the indirect request requires data to be read.
 */
bool
blkif_indirect_rq_rd(blkif_request_indirect_t const * const msg);

/**
 * Tells whether the indirect request requires data to be written.
 */
bool
blkif_indirect_rq_wr(blkif_request_indirect_t const * const msg);

/**
 * Tells whether the indirect request requires data to transferred.
 */
bool
blkif_indirect_rq_data(blkif_request_indirect_t const * const msg);

#endif
