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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "blktap-xenif.h"
#include "debug.h"
#include "td-req.h"
#include "td-blkif.h"
#include "td-ctx.h"
#include "tapdisk-server.h"
#include "tapdisk-metrics.h"
#include "tapdisk-vbd.h"
#include "tapdisk-log.h"
#include "tapdisk.h"
#include "timeout-math.h"
#include "util.h"

/**
 * Free request big buffer cache.
 *
 * @param blkif the block interface
 */
void
td_xenblkif_bigbufcache_free(struct td_xenblkif * const blkif)
{
    ASSERT(blkif);

    while (blkif->n_reqs_bigbufcache_free > 0){
        munmap(blkif->reqs_bigbufcache[--blkif->n_reqs_bigbufcache_free],
               (size_t)TD_REQ_BIG_BUFFER_SIZE);
    }
}

/**
 * Get big buffer for a request. From cache if available or newly allocated.
 *
 * @param blkif the block interface
 */
void *
td_xenblkif_bigbufcache_get(struct td_xenblkif * const blkif)
{
    void *buf;

    ASSERT(blkif);

    if (!blkif->n_reqs_bigbufcache_free) {
	    buf = mmap(NULL, (size_t)TD_REQ_BIG_BUFFER_SIZE,
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (unlikely(buf == MAP_FAILED))
            buf = NULL;
    } else
        buf = blkif->reqs_bigbufcache[--blkif->n_reqs_bigbufcache_free];

    // If we just got a request, we cancel the cache expire timer
    td_xenblkif_bufcache_evt_unreg(blkif);

    return buf;
}

void
td_xenblkif_bigbufcache_put(struct td_xenblkif * const blkif, void *buf)
{
    ASSERT(blkif);

    if (unlikely(!buf))
        return;

#ifdef DEBUG
	{
		int i;

		for (i = 0; i < blkif->n_reqs_bigbufcache_free; i++)
			ASSERT(blkif->reqs_bigbufcache[i] != buf);
	}
#endif

    blkif->reqs_bigbufcache[blkif->n_reqs_bigbufcache_free++] = buf;

    /* If we're in low memory mode, prune the bufcache immediately. */
    if (tapdisk_server_mem_mode() == LOW_MEMORY_MODE) {
	td_xenblkif_bigbufcache_free(blkif);
    } else {
        // We only set the expire event when no requests are inflight
        if (blkif->n_reqs_free == blkif->ring_size)
            td_xenblkif_bufcache_evt_reg(blkif);
    }
}

bool
blkif_indirect_rq_rd(blkif_request_indirect_t const * const msg)
{
	return BLKIF_OP_READ == msg->indirect_op;
}


bool
blkif_indirect_rq_wr(blkif_request_indirect_t const * const msg)
{
	return BLKIF_OP_WRITE == msg->indirect_op;
}


bool
blkif_indirect_rq_data(blkif_request_indirect_t const * const msg)
{
	return blkif_indirect_rq_rd(msg) || blkif_indirect_rq_wr(msg);
}


int
guest_copy_indirect(struct td_xenblkif * const blkif,
		    struct td_xenblkif_req * const req) {
    int i = 0, indirect_pages;
    long err = 0;
    struct ioctl_gntdev_grant_copy gcopy;


    ASSERT(blkif);
    ASSERT(blkif->ctx);
    ASSERT(req);

    if (req->msg.operation != BLKIF_OP_INDIRECT)
        return 0;

    ASSERT(req->ind.nr_segments > 0);
    ASSERT(req->ind.nr_segments <= ARRAY_SIZE(req->gcopy_segs));
    ASSERT(blkif->indirect_segments > 0);

    indirect_pages = req->ind.nr_segments / blkif->indirect_segments;
    indirect_pages += req->ind.nr_segments % blkif->indirect_segments ? 1 : 0;

    for (i = 0; i < indirect_pages; i++) {
        struct gntdev_grant_copy_segment *gcopy_seg = &req->gcopy_segs[i];
	/* copy from guest */
	gcopy_seg->dest.virt = req->vma + (i << PAGE_SHIFT);
	gcopy_seg->source.foreign.ref = req->ind.indirect_grefs[i];
	gcopy_seg->source.foreign.offset = 0;
	gcopy_seg->source.foreign.domid = blkif->domid;
	gcopy_seg->flags = GNTCOPY_source_gref;

        gcopy_seg->len = sizeof(struct blkif_request_segment) * blkif->indirect_segments;
    }
    gcopy.count = indirect_pages;
    gcopy.segments = req->gcopy_segs;

    err = -ioctl(blkif->ctx->gntdev_fd, IOCTL_GNTDEV_GRANT_COPY, &gcopy);
    if (err) {
        err = -errno;
        RING_ERR(blkif, "failed to grant-copy indirect request %"PRIu64" "
                "(%d segments): %s\n", req->ind.id,
                req->ind.nr_segments, strerror(-err));
        goto out;
    }

    for (i = 0; i < indirect_pages; i++) {
	struct gntdev_grant_copy_segment *gcopy_seg = &req->gcopy_segs[i];
	if (gcopy_seg->status != GNTST_okay) {
	    /*
	     * TODO use gnttabop_error for reporting errors, defined in
	     * xen/extras/mini-os/include/gnttab.h (header not available to
	     * user space)
	     */
	    RING_ERR(blkif, "req %lu: failed to grant-copy indirect segment %d: %d\n",
		    req->ind.id, i, gcopy_seg->status);
	    err = -EIO;
	    goto out;
	}
    }

out:
    return err;
}
