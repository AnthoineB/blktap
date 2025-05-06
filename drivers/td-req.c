/*
 * Copyright (c) 2016, Citrix Systems, Inc.
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

#ifdef __linux__
#include <linux/version.h>
#endif

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

#ifdef DEBUG
#define BLKIF_MSG_POISON 0xdeadbeef
#endif

#define ERR(blkif, fmt, args...) \
    EPRINTF("%d/%d: "fmt, (blkif)->domid, (blkif)->devid, ##args);

#define TD_REQS_BUFCACHE_EXPIRE 3 // time in seconds
#define TD_REQS_BUFCACHE_MIN    1 // buffers to always keep in the cache

/*
 * When the persistent grants list is full we will remove unused grants
 * from the list. The percent number of grants to be removed at each LRU
 * execution.
 */
#define LRU_PERCENT_CLEAN 5

/*
 * Maximum number of grants to map persistently in blkback. For maximum
 * performance this should be the total numbers of grants that can be used
 * to fill the ring, but since this might become too high, specially with
 * the use of indirect descriptors, we set it to a value that provides good
 * performance without using too much memory.
 *
 * When the list of persistent grants is full we clean it up using a LRU
 * algorithm.
 */
static int xen_blkif_max_pgrants = 88;

/*
 * How long a persistent grant is allowed to remain allocated without being in
 * use. The time is in seconds, 0 means indefinitely long.
 */
static unsigned int xen_blkif_pgrant_timeout = 60;

struct persistent_gnt {
    //struct page *page; /* mapped page */
    void *vaddr;
    grant_ref_t gnt;
    //grant_handle_t handle;
    bool active;
    struct timeval last_used;
    struct rb_node node;
    struct list_head remove_node;
};

/* tree ops for persistent grants */
struct persistent_gnt *get_persistent_gnt(struct td_xenblkif * const blkif,
					  grant_ref_t gref);
int add_persistent_gnt(struct td_xenblkif * const blkif,
		       struct persistent_gnt *persistent_gnt);
void put_persistent_gnt(struct td_xenblkif * const blkif,
                        struct persistent_gnt *persistent_gnt);
void free_persistent_gnts(struct td_xenblkif * const blkif);

#define foreach_grant_safe(pos, n, rbtree, node) \
    for ((pos) = container_of(rb_first((rbtree)), typeof(*(pos)), node), \
         (n) = (&(pos)->node) ? rb_next(&(pos)->node) : NULL; \
         &(pos)->node; \
         (pos) = container_of(n, typeof(*(pos)), node), \
         (n) = (&(pos)->node) ? rb_next(&(pos)->node) : NULL)

int add_persistent_gnt(struct td_xenblkif * const blkif,
		       struct persistent_gnt *persistent_gnt)
{
    struct rb_node **new = NULL, *parent = NULL;
    struct persistent_gnt *this;

    if (blkif->persistent_gnt_c >= blkif->persistent_max_grants) {
        EPRINTF("Using maximum number of peristent grants\n");
        if (!blkif->overflow_max_grants)
            blkif->overflow_max_grants = true;
        return -EBUSY;
    }
    /* Figure out where to put new node */
    new = &blkif->persistent_gnts.rb_node;
    while (*new) {
        this = container_of(*new, struct persistent_gnt, node);

        parent = *new;
        if (persistent_gnt->gnt < this->gnt) {
            new = &((*new)->rb_left);
        } else if (persistent_gnt->gnt > this->gnt) {
            new = &((*new)->rb_right);
        } else {
            EPRINTF("Trying to add a gref that's already in the tree\n");
            return -EINVAL;
        }
    }

    persistent_gnt->active = true;
    /* Add new node and rebalance tree. */
    rb_link_node(&(persistent_gnt->node), parent, new);
    rb_insert_color(&(persistent_gnt->node), &blkif->persistent_gnts);
    blkif->persistent_gnt_c++;
    blkif->persistent_gnt_in_use++;
    return 0;
}

struct persistent_gnt *get_persistent_gnt(struct td_xenblkif * const blkif,
					  grant_ref_t gref)
{
    struct persistent_gnt *data;
    struct rb_node *node = NULL;

    node = blkif->persistent_gnts.rb_node;
    while (node) {
        data = container_of(node, struct persistent_gnt, node);

        if (gref < data->gnt) {
            node = node->rb_left;
        } else if (gref > data->gnt) {
            node = node->rb_right;
        } else {
            if (data->active) {
                EPRINTF("Requesting a grant already in use\n");
                return NULL;
            }
            data->active = true;
            blkif->persistent_gnt_in_use++;
            return data;
        }
    }
    return NULL;
}

void put_persistent_gnt(struct td_xenblkif * const blkif,
			struct persistent_gnt *persistent_gnt)
{
    if (!persistent_gnt->active)
        EPRINTF("Freeing a grant already unused\n");
    gettimeofday(&persistent_gnt->last_used, NULL);
    persistent_gnt->active = false;
    blkif->persistent_gnt_in_use--;
}

void free_persistent_gnts(struct td_xenblkif * const blkif)
{
    //struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    //struct page *pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    struct persistent_gnt *persistent_gnt;
    struct rb_root *root = &blkif->persistent_gnts;
    struct rb_node *n;
    //int ret = 0;
    //int pages_to_unmap = 0;
    //void *addr;

    foreach_grant_safe(persistent_gnt, n, root, node) {
        xengnttab_unmap(blkif->ctx->gntdev_xgt, persistent_gnt->vaddr, 1);
#if 0
        BUG_ON(persistent_gnt->handle == NETBACK_INVALID_HANDLE);

        addr = pfn_to_kaddr(page_to_pfn(persistent_gnt->page));
        gnttab_set_unmap_op(&unmap[pages_to_unmap],
                (unsigned long)addr,
                GNTMAP_host_map | GNTMAP_readonly,
                persistent_gnt->handle);

        pages[pages_to_unmap] = persistent_gnt->page;

        if (++pages_to_unmap == BLKIF_MAX_SEGMENTS_PER_REQUEST ||
                !rb_next(&persistent_gnt->node)) {
            ret = gnttab_unmap_refs(unmap, NULL, pages,
                    pages_to_unmap);
            BUG_ON(ret);
            put_free_pages(tree, pages, pages_to_unmap);
            pages_to_unmap = 0;
        }

#endif
        rb_erase(&persistent_gnt->node, root);
        free(persistent_gnt);
        blkif->persistent_gnt_c--;
    }
    ASSERT(blkif->persistent_gnt_c != 0);
}

#if 0
static int xen_blkbk_map(struct td_xenblkif * const blkif,
                        grant_ref_t *grefs,
                        int num, bool ro)
{
    struct gnttab_map_grant_ref map[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    //struct page *pages_to_gnt[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    struct persistent_gnt *persistent_gnt = NULL;
    int i, seg_idx, new_map_idx;
    int segs_to_map = 0;
    int ret = 0;
    int last_map = 0, map_until = 0;
    int use_persistent_gnts;

    use_persistent_gnts = (blkif->ctx->persistent_grants);

    /*
     * Fill out preq.nr_sects with proper amount of sectors, and setup
     * assign map[..] with the PFN of the page in our domain with the
     * corresponding grant reference for each page.
     */
again:
    for (i = map_until; i < num; i++) {
        uint32_t flags;

        if (use_persistent_gnts) {
            persistent_gnt = get_persistent_gnt(blkif, grefs[i]);
        }

        if (persistent_gnt) {
            /*
             * We are using persistent grants and
             * the grant is already mapped
             */
            grefs[i]->vma = persistent_gnt->vma;
            grefs[i]->persistent_gnt = persistent_gnt;
        } else {
            if (get_free_page(ring, &pages[i]->page))
                goto out_of_memory;
            addr = vaddr(pages[i]->page);
            pages_to_gnt[segs_to_map] = pages[i]->page;
            pages[i]->persistent_gnt = NULL;
            flags = GNTMAP_host_map;
            if (!use_persistent_gnts && ro)
                flags |= GNTMAP_readonly;
            gnttab_set_map_op(&map[segs_to_map++], addr,
                    flags, pages[i]->gref,
                    blkif->domid);
        }
        map_until = i + 1;
        if (segs_to_map == BLKIF_MAX_SEGMENTS_PER_REQUEST)
            break;
    }

    if (segs_to_map) {
        req->vma = xengnttab_map_domain_grant_refs(blkif->ctx->gntdev_xgt,
                req->msg.nr_segments,
                blkif->domid,
                refs,
                PROT_READ | PROT_WRITE);
        ret = gnttab_map_refs(map, NULL, pages_to_gnt, segs_to_map);
        BUG_ON(ret);
    }

    /*
     * Now swizzle the MFN in our domain with the MFN from the other domain
     * so that when we access vaddr(pending_req,i) it has the contents of
     * the page from the other domain.
     */
    for (seg_idx = last_map, new_map_idx = 0; seg_idx < map_until; seg_idx++) {
        if (!pages[seg_idx]->persistent_gnt) {
            /* This is a newly mapped grant */
            BUG_ON(new_map_idx >= segs_to_map);
            if (unlikely(map[new_map_idx].status != 0)) {
                pr_debug("invalid buffer -- could not remap it\n");
                put_free_pages(ring, &pages[seg_idx]->page, 1);
                pages[seg_idx]->handle = BLKBACK_INVALID_HANDLE;
                ret |= 1;
                goto next;
            }
            pages[seg_idx]->handle = map[new_map_idx].handle;
        } else {
            continue;
        }
        if (use_persistent_gnts &&
                ring->persistent_gnt_c < xen_blkif_max_pgrants) {
            /*
             * We are using persistent grants, the grant is
             * not mapped but we might have room for it.
             */
            persistent_gnt = malloc(sizeof(struct persistent_gnt));
            if (!persistent_gnt) {
                /*
                 * If we don't have enough memory to
                 * allocate the persistent_gnt struct
                 * map this grant non-persistenly
                 */
                goto next;
            }
            persistent_gnt->gnt = map[new_map_idx].ref;
            persistent_gnt->handle = map[new_map_idx].handle;
            persistent_gnt->page = pages[seg_idx]->page;
            if (add_persistent_gnt(ring,
                        persistent_gnt)) {
                kfree(persistent_gnt);
                persistent_gnt = NULL;
                goto next;
            }
            pages[seg_idx]->persistent_gnt = persistent_gnt;
            pr_debug("grant %u added to the tree of persistent grants, using %u/%u\n",
                    persistent_gnt->gnt, ring->persistent_gnt_c,
                    xen_blkif_max_pgrants);
            goto next;
        }
        if (use_persistent_gnts && !blkif->overflow_max_grants) {
            blkif->overflow_max_grants = true;
            pr_debug("domain %u, device %#x is using maximum number of persistent grants\n",
                    blkif->domid, blkif->vbd.handle);
        }
        /*
         * We could not map this grant persistently, so use it as
         * a non-persistent grant.
         */
next:
        new_map_idx++;
    }
    segs_to_map = 0;
    last_map = map_until;
    if (map_until != num)
        goto again;

    return ret;

out_of_memory:
    pr_alert("%s: out of memory\n", __func__);
    put_free_pages(ring, pages_to_gnt, segs_to_map);
    return -ENOMEM;
}

static unsigned int xen_blkbk_unmap_prepare(
	struct xen_blkif_ring *ring,
	struct grant_page **pages,
	unsigned int num,
	struct gnttab_unmap_grant_ref *unmap_ops,
	struct page **unmap_pages)
{
    unsigned int i, invcount = 0;

    for (i = 0; i < num; i++) {
        if (pages[i]->persistent_gnt != NULL) {
            put_persistent_gnt(ring, pages[i]->persistent_gnt);
            continue;
        }
        if (pages[i]->handle == BLKBACK_INVALID_HANDLE)
            continue;
        unmap_pages[invcount] = pages[i]->page;
        gnttab_set_unmap_op(&unmap_ops[invcount], vaddr(pages[i]->page),
                GNTMAP_host_map, pages[i]->handle);
        pages[i]->handle = BLKBACK_INVALID_HANDLE;
        invcount++;
    }

    return invcount;
}

static void xen_blkbk_unmap_and_respond(struct pending_req *req)
{
    struct gntab_unmap_queue_data* work = &req->gnttab_unmap_data;
    struct xen_blkif_ring *ring = req->ring;
    struct grant_page **pages = req->segments;
    unsigned int invcount;

    invcount = xen_blkbk_unmap_prepare(ring, pages, req->nr_segs,
            req->unmap, req->unmap_pages);

    work->data = req;
    work->done = xen_blkbk_unmap_and_respond_callback;
    work->unmap_ops = req->unmap;
    work->kunmap_ops = NULL;
    work->pages = req->unmap_pages;
    work->count = invcount;

    gnttab_unmap_refs_async(&req->gnttab_unmap_data);
}

/*
 * Unmap the grant references.
 *
 * This could accumulate ops up to the batch size to reduce the number
 * of hypercalls, but since this is only used in error paths there's
 * no real need.
 */
static void xen_blkbk_unmap(struct xen_blkif_ring *ring,
                            struct grant_page *pages[],
                            int num)
{
    struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    struct page *unmap_pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    unsigned int invcount = 0;
    int ret;

    while (num) {
        unsigned int batch = num < BLKIF_MAX_SEGMENTS_PER_REQUEST ?
                                num : BLKIF_MAX_SEGMENTS_PER_REQUEST;

        invcount = xen_blkbk_unmap_prepare(ring, pages, batch,
                unmap, unmap_pages);
        if (invcount) {
            ret = gnttab_unmap_refs(unmap, NULL, unmap_pages, invcount);
            BUG_ON(ret);
            put_free_pages(ring, unmap_pages, invcount);
        }
        pages += batch;
        num -= batch;
    }
}
#endif

static void
td_xenblkif_bufcache_free(struct td_xenblkif * const blkif);
static void
td_xenblkif_free_pgnt_caches(struct td_xenblkif * const blkif);
static void
td_xenblkif_purge_pgnt_list(struct td_xenblkif * const blkif);
static inline void
td_xenblkif_bufcache_evt_unreg(struct td_xenblkif * const blkif);

static void
td_xenblkif_bufcache_event(event_id_t id, char mode, void *private)
{
    struct td_xenblkif *blkif = private;

    pthread_mutex_lock(&blkif->mutex);
    td_xenblkif_bufcache_free(blkif);

    td_xenblkif_purge_pgnt_list(blkif);

    td_xenblkif_bufcache_evt_unreg(blkif);
    pthread_mutex_unlock(&blkif->mutex);
}

/**
 * Unregister the event to expire the request buffer cache.
 *
 * @param blkif the block interface
 */
static inline void
td_xenblkif_bufcache_evt_unreg(struct td_xenblkif * const blkif)
{
    if (blkif->reqs_bufcache_evtid > 0){
        tapdisk_server_unregister_event(blkif->reqs_bufcache_evtid);
    }
    blkif->reqs_bufcache_evtid = 0;
}

/**
 * Register the event to expire the request buffer cache.
 *
 * @param blkif the block interface
 */
static inline void
td_xenblkif_bufcache_evt_reg(struct td_xenblkif * const blkif)
{
    blkif->reqs_bufcache_evtid =
        tapdisk_server_register_event(SCHEDULER_POLL_TIMEOUT,
                                      -1, /* dummy fd */
                                      TV_SECS(TD_REQS_BUFCACHE_EXPIRE),
                                      td_xenblkif_bufcache_event,
                                      blkif);
}

/**
 * Free request buffer cache.
 *
 * @param blkif the block interface
 */
static void
td_xenblkif_bufcache_free(struct td_xenblkif * const blkif)
{
    ASSERT(blkif);

    while (blkif->n_reqs_bufcache_free > TD_REQS_BUFCACHE_MIN){
        munmap(blkif->reqs_bufcache[--blkif->n_reqs_bufcache_free],
               (size_t)BLKIF_MAX_BUFFER_SEGMENTS_PER_REQUEST << PAGE_SHIFT);
    }
}

static void
td_xenblkif_free_pgnt_caches(struct td_xenblkif * const blkif)
{
    /* Free all persistent grant pages */
    if (!RB_EMPTY_ROOT(&blkif->persistent_gnts))
        free_persistent_gnts(blkif);

    ASSERT(!RB_EMPTY_ROOT(&blkif->persistent_gnts));
    blkif->persistent_gnt_c = 0;

#if 0
    /* Since we are shutting down remove all pages from the buffer */
    shrink_free_pagepool(blkif, 0 /* All */);
#endif
}

void xen_blkbk_unmap_purged_grants(struct td_xenblkif *ring)
{
	//struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	//struct page *pages[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct persistent_gnt *persistent_gnt;
	//int segs_to_unmap = 0;
	//struct xen_blkif_ring *ring = container_of(work, typeof(*ring), persistent_purge_work);
	//struct gntab_unmap_queue_data unmap_data;

	//unmap_data.pages = pages;
	//unmap_data.unmap_ops = unmap;
	//unmap_data.kunmap_ops = NULL;

	while(!list_empty(&ring->persistent_purge_list)) {
		persistent_gnt = list_first_entry(&ring->persistent_purge_list,
		                                  struct persistent_gnt,
		                                  remove_node);
		list_del(&persistent_gnt->remove_node);

#if 0
		gnttab_set_unmap_op(&unmap[segs_to_unmap],
			vaddr(persistent_gnt->page),
			GNTMAP_host_map,
			persistent_gnt->handle);

		pages[segs_to_unmap] = persistent_gnt->page;

		if (++segs_to_unmap == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
			unmap_data.count = segs_to_unmap;
			BUG_ON(gnttab_unmap_refs_sync(&unmap_data));
			put_free_pages(ring, pages, segs_to_unmap);
			segs_to_unmap = 0;
		}
#else
                xengnttab_unmap(ring->ctx->gntdev_xgt, persistent_gnt->vaddr, 1);
#endif
		free(persistent_gnt);
	}
#if 0
	if (segs_to_unmap > 0) {
		unmap_data.count = segs_to_unmap;
		BUG_ON(gnttab_unmap_refs_sync(&unmap_data));
		put_free_pages(ring, pages, segs_to_unmap);
	}
#endif
}

static inline bool persistent_gnt_timeout(struct persistent_gnt *persistent_gnt)
{
        struct timeval now, deadline;

        if (!xen_blkif_pgrant_timeout)
            return false;

        gettimeofday(&now, NULL);

        TV_ADD(persistent_gnt->last_used, TV_SECS(xen_blkif_pgrant_timeout), deadline);

        if (TV_AFTER(deadline, now))
            return true;

        return false;
}

static void purge_persistent_gnt(struct td_xenblkif *ring)
{
	struct persistent_gnt *persistent_gnt;
	struct rb_node *n;
	unsigned int num_clean, total;
	bool scan_used = false;
	struct rb_root *root;

	if (ring->persistent_gnt_c < xen_blkif_max_pgrants ||
	    (ring->persistent_gnt_c == xen_blkif_max_pgrants &&
	    !ring->overflow_max_grants)) {
		num_clean = 0;
	} else {
		num_clean = (xen_blkif_max_pgrants / 100) * LRU_PERCENT_CLEAN;
		num_clean = ring->persistent_gnt_c - xen_blkif_max_pgrants +
			    num_clean;
		num_clean = ring->persistent_gnt_c < num_clean ?
                                ring->persistent_gnt_c : num_clean;
		DPRINTF("Going to purge at least %u persistent grants\n",
			 num_clean);
	}

	/*
	 * At this point, we can assure that there will be no calls
         * to get_persistent_grant (because we are executing this code from
         * xen_blkif_schedule), there can only be calls to put_persistent_gnt,
         * which means that the number of currently used grants will go down,
         * but never up, so we will always be able to remove the requested
         * number of grants.
	 */

	total = 0;

	ASSERT(!list_empty(&ring->persistent_purge_list));
	root = &ring->persistent_gnts;
purge_list:
	foreach_grant_safe(persistent_gnt, n, root, node) {
		//ASSERT(persistent_gnt->handle == BLKBACK_INVALID_HANDLE);

		if (persistent_gnt->active)
			continue;
		if (!scan_used && !persistent_gnt_timeout(persistent_gnt))
			continue;
		if (scan_used && total >= num_clean)
			continue;

		rb_erase(&persistent_gnt->node, root);
		list_add(&persistent_gnt->remove_node,
			 &ring->persistent_purge_list);
		total++;
	}
	/*
	 * Check whether we also need to start cleaning
	 * grants that were used since last purge in order to cope
	 * with the requested num
	 */
	if (!scan_used && total < num_clean) {
		DPRINTF("Still missing %u purged frames\n", num_clean - total);
		scan_used = true;
		goto purge_list;
	}

	if (total) {
		ring->persistent_gnt_c -= total;
		ring->overflow_max_grants = false;

		/* We can defer this work */
		//schedule_work(&ring->persistent_purge_work);
                xen_blkbk_unmap_purged_grants(ring);
		DPRINTF("Purged %u/%u\n", num_clean, total);
	}

	return;
}

static void
td_xenblkif_purge_pgnt_list(struct td_xenblkif * const blkif)
{
    if (blkif->ctx->persistent_grants &&
            !list_empty(&blkif->persistent_purge_list)) {
        purge_persistent_gnt(blkif);
    }
}

/**
 * Get buffer for a request. From cache if available or newly allocated.
 *
 * @param blkif the block interface
 */
static void *
td_xenblkif_bufcache_get(struct td_xenblkif * const blkif)
{
    void *buf;

    ASSERT(blkif);

    if (!blkif->n_reqs_bufcache_free) {
	    buf = mmap(NULL, (size_t)TD_REQ_BUFFER_SIZE,
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (unlikely(buf == MAP_FAILED))
            buf = NULL;
    } else
        buf = blkif->reqs_bufcache[--blkif->n_reqs_bufcache_free];

    // If we just got a request, we cancel the cache expire timer
    td_xenblkif_bufcache_evt_unreg(blkif);

    return buf;
}

static void
td_xenblkif_bufcache_put(struct td_xenblkif * const blkif, void *buf)
{
    ASSERT(blkif);

    if (unlikely(!buf))
        return;

#ifdef DEBUG
	{
		int i;

		for (i = 0; i < blkif->n_reqs_bufcache_free; i++)
			ASSERT(blkif->reqs_bufcache[i] != buf);
	}
#endif

    blkif->reqs_bufcache[blkif->n_reqs_bufcache_free++] = buf;

    /* If we're in low memory mode, prune the bufcache immediately. */
    if (tapdisk_server_mem_mode() == LOW_MEMORY_MODE) {
        td_xenblkif_bufcache_free(blkif);
        td_xenblkif_purge_pgnt_list(blkif);
    } else {
        // We only set the expire event when no requests are inflight
        if (blkif->n_reqs_free == blkif->ring_size)
            td_xenblkif_bufcache_evt_reg(blkif);
    }
}

#if 0
static void
guest_unmap(struct td_xenblkif * const blkif, void *buf, int count)
{
    //munmap(buf, PAGE_SIZE);
    xengnttab_unmap(blkif->ctx->gntdev_xgt, buf, count);
}
#endif

static void
put_persistent_gnts(struct td_xenblkif *const blkif,
                    struct td_xenblkif_req * const tapreq)
{
    int i;

    ASSERT(blkif);
    ASSERT(tapreq);
    ASSERT(blkif->ctx->persistent_grants);

    for (i = 0; i < tapreq->msg.nr_segments; i++) {
        put_persistent_gnt(blkif, tapreq->pgrefs[i]);
    }
}

/**
 * Puts the request back to the free list of this block interface.
 *
 * @param blkif the block interface
 * @param tapreq the request to give back
 */
static void
tapdisk_xenblkif_free_request(struct td_xenblkif * const blkif,
        struct td_xenblkif_req * const tapreq)
{
    int put_bufcache;
    void *vma;

    ASSERT(blkif);
    ASSERT(tapreq);
    ASSERT(blkif->n_reqs_free < blkif->ring_size);

    put_bufcache = tapreq->msg.operation != BLKIF_OP_DISCARD && tapreq->msg.nr_segments != 0;
    if (put_bufcache) {
        vma = tapreq->vma;
    }

    if (blkif->ctx->persistent_grants && tapreq->gntop == GRANT_MAP)
        put_persistent_gnts(blkif, tapreq);
#ifdef DEBUG
	memset(&tapreq->msg, BLKIF_MSG_POISON, sizeof(tapreq->msg));
#endif

    blkif->reqs_free[blkif->ring_size - (++blkif->n_reqs_free)] = &tapreq->msg;

	if (likely(put_bufcache))
	    td_xenblkif_bufcache_put(blkif, vma);
}

/**
 * Returns the size, in request descriptors, of the shared ring
 *
 * @param blkif the block interface
 * @returns the size, in request descriptors, of the shared ring
 */
static int
td_blkif_ring_size(const struct td_xenblkif * const blkif)
{
    ASSERT(blkif);

    switch (blkif->proto) {
        case BLKIF_PROTOCOL_NATIVE:
            return RING_SIZE(&blkif->rings.native);

        case BLKIF_PROTOCOL_X86_32:
            return RING_SIZE(&blkif->rings.x86_32);

        case BLKIF_PROTOCOL_X86_64:
            return RING_SIZE(&blkif->rings.x86_64);

        default:
            return -EPROTONOSUPPORT;
    }
}

/**
 * Get the response that corresponds to the specified ring index in a H/W
 * independent way.
 *
 * @returns a pointer to the response, NULL on error, sets errno
 *
 * TODO use function pointers instead of switch
 * XXX only called by xenio_blkif_put_response
 */
static inline blkif_response_t *
xenio_blkif_get_response(struct td_xenblkif* const blkif, const RING_IDX rp)
{
    blkif_back_rings_t * const rings = &blkif->rings;
    blkif_response_t * p = NULL;

    switch (blkif->proto) {
        case BLKIF_PROTOCOL_NATIVE:
            p = (blkif_response_t *) RING_GET_RESPONSE(&rings->native, rp);
            break;
        case BLKIF_PROTOCOL_X86_32:
            p = (blkif_response_t *) RING_GET_RESPONSE(&rings->x86_32, rp);
            break;
        case BLKIF_PROTOCOL_X86_64:
            p = (blkif_response_t *) RING_GET_RESPONSE(&rings->x86_64, rp);
            break;
        default:
            errno = EPROTONOSUPPORT;
			return NULL;
    }

    return p;
}

/**
 * Puts a response in the ring.
 *
 * @param blkif the VBD
 * @param req the request for which the response should be put
 * @param status the status of the response (success or an error code)
 * @param final controls whether the front-end will be notified, if necessary
 *
 * TODO @req can be NULL so the function will only notify the other end. This
 * is used in the error path of tapdisk_xenblkif_queue_requests. The point is
 * that the other will just be notified, does this make sense?
 */
static int
xenio_blkif_put_response(struct td_xenblkif * const blkif,
        struct td_xenblkif_req *req, int const status, int const final)
{
    blkif_common_back_ring_t * const ring = &blkif->rings.common;

    if (req) {
        blkif_response_t * msg = xenio_blkif_get_response(blkif,
                ring->rsp_prod_pvt);
		if (!msg)
			return -errno;

        ASSERT(status == BLKIF_RSP_EOPNOTSUPP || status == BLKIF_RSP_ERROR
                || status == BLKIF_RSP_OKAY);

        msg->id = req->msg.id;

        msg->operation = req->msg.operation;

        msg->status = status;

        ring->rsp_prod_pvt++;
    }

    if (final) {
        int notify;
        RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);
        if (notify) {
            int err = xenevtchn_notify(blkif->ctx->xce_handle, blkif->port);
            if (err < 0) {
                err = -errno;
                if (req) {
                    RING_ERR(blkif, "req %lu: failed to notify event channel: "
                            "%s\n", req->msg.id, strerror(-err));
                } else {
                    RING_ERR(blkif, "failed to notify event channel: %s\n",
                            strerror(-err));
                }
                return err;
            }
        }
    }

    return 0;
}


/**
 * Tells whether the request requires data to be read.
 */
static inline bool
blkif_rq_rd(blkif_request_t const * const msg)
{
	return BLKIF_OP_READ == msg->operation;
}


/**
 * Tells whether the request requires data to be written.
 */
static inline bool
blkif_rq_wr(blkif_request_t const * const msg)
{
	return BLKIF_OP_WRITE == msg->operation ||
		(BLKIF_OP_WRITE_BARRIER == msg->operation && msg->nr_segments);
}


/**
 * Tells whether the request requires data to be discard.
 */
static inline bool
blkif_rq_ds(blkif_request_t const * const msg)
{
	return BLKIF_OP_DISCARD == msg->operation;
}


/**
 * Tells whether the request requires data to transferred.
 */
static inline bool
blkif_rq_data(blkif_request_t const * const msg)
{
	return blkif_rq_rd(msg) || blkif_rq_wr(msg);
}


static int
guest_map(struct td_xenblkif * const blkif,
        struct td_xenblkif_req * const req)
{
    int i;
    grant_ref_t *refs;
    uint32_t *indices;
    int gref_to_map = 0;
    struct persistent_gnt *pgref;
    struct blkif_request_segment *seg;
    void *vaddr;
#if 0
    long err = 0;
    struct ioctl_gntdev_map_grant_ref *gmap;
#endif

    ASSERT(blkif);
    ASSERT(blkif->ctx);
    ASSERT(req);
    ASSERT(blkif_rq_data(&req->msg));
    ASSERT(req->msg.nr_segments > 0);

    refs = alloca(sizeof(*refs) * req->msg.nr_segments);
    indices = alloca(sizeof(*indices) * req->msg.nr_segments);

    req->gntop = GRANT_MAP;
    for (i = 0; i < req->msg.nr_segments; i++) {
        seg = &req->msg.seg[i];
        pgref = get_persistent_gnt(blkif, seg->gref);
        if (!pgref) {
            refs[gref_to_map] = seg->gref;
            indices[gref_to_map] = i;
            gref_to_map++;
        }
        req->pgrefs[i] = pgref;
    }

    if (gref_to_map) {
        vaddr = xengnttab_map_domain_grant_refs(blkif->ctx->gntdev_xgt,
                                              gref_to_map,
                                              blkif->domid,
                                              refs,
                                              PROT_READ | PROT_WRITE);

        for (i = 0; i < gref_to_map; i++) {
            pgref = malloc(sizeof(struct persistent_gnt));
            if (!pgref)
                return -ENOMEM;
            pgref->vaddr = vaddr + PAGE_SIZE * i;
            pgref->gnt = refs[i];

            req->pgrefs[indices[i]] = pgref;

            add_persistent_gnt(blkif, pgref);
        }
    }

    if (blkif_rq_rd(&req->msg)) {
        for (i = 0; i < req->msg.nr_segments; i++) {
            seg = &req->msg.seg[i];
            memcpy(req->pgrefs[i]->vaddr + (seg->first_sect << SECTOR_SHIFT),
                    req->vma + (PAGE_SIZE * i),
                    (seg->last_sect - seg->first_sect + 1) << SECTOR_SHIFT);
        }
    } else if (blkif_rq_wr(&req->msg)) {
        for (i = 0; i < req->msg.nr_segments; i++) {
            seg = &req->msg.seg[i];
            memcpy(req->vma + (PAGE_SIZE * i),
                    req->pgrefs[i]->vaddr + (seg->first_sect << SECTOR_SHIFT),
                    (seg->last_sect - seg->first_sect + 1) << SECTOR_SHIFT);
        }
    }

#if 0
    gmap = alloca(sizeof(struct ioctl_gntdev_map_grant_ref) +
            sizeof(struct ioctl_gntdev_grant_ref) *
            BLKIF_MAX_SEGMENTS_PER_REQUEST - 1);

    for (i = 0; i < req->msg.nr_segments; i++) {
        struct blkif_request_segment *blkif_seg = &req->msg.seg[i];
        struct ioctl_gntdev_grant_ref *refs = &gmap->refs[i];
        refs->domid = blkif->domid;
        refs->ref = blkif_seg->gref;
    }
    gmap->count = req->msg.nr_segments;

    err = -ioctl(fd, IOCTL_GNTDEV_MAP_GRANT_REF, gmap);
    if (err) {
        err = -errno;
        RING_ERR(blkif, "failed to grant-map request %"PRIu64" "
                "(%d segments): %s\n", req->msg.id,
                req->msg.nr_segments, strerror(-err));
        goto out;
    }

    req->vma = mmap(NULL, PAGE_SIZE * gmap->count, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gmap->index);
    if (unlikely(req->vma == MAP_FAILED)) {
        req->vma = NULL;
        err = -errno;
        goto out;
    }
#endif

    return 0;
//out:
//    return err;
}


static int
guest_copy2(struct td_xenblkif * const blkif,
        struct td_xenblkif_req * const tapreq /* TODO rename to req */) {

    int i = 0;
    long err = 0;
    struct ioctl_gntdev_grant_copy gcopy;

    ASSERT(blkif);
    ASSERT(blkif->ctx);
    ASSERT(tapreq);
    ASSERT(blkif_rq_data(&tapreq->msg));
	ASSERT(tapreq->msg.nr_segments > 0);
	ASSERT(tapreq->msg.nr_segments <= ARRAY_SIZE(tapreq->gcopy_segs));

    tapreq->gntop = GRANT_COPY;
    for (i = 0; i < tapreq->msg.nr_segments; i++) {
        struct blkif_request_segment *blkif_seg = &tapreq->msg.seg[i];
        struct gntdev_grant_copy_segment *gcopy_seg = &tapreq->gcopy_segs[i];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
        if (blkif_rq_wr(&tapreq->msg)) {
            /* copy from guest */
            gcopy_seg->dest.virt = tapreq->vma + (i << PAGE_SHIFT)
                + (blkif_seg->first_sect << SECTOR_SHIFT);
            gcopy_seg->source.foreign.ref = blkif_seg->gref;
            gcopy_seg->source.foreign.offset = blkif_seg->first_sect << SECTOR_SHIFT;
            gcopy_seg->source.foreign.domid = blkif->domid;
            gcopy_seg->flags = GNTCOPY_source_gref;
        } else {
            /* copy to guest */
            gcopy_seg->source.virt = tapreq->vma + (i << PAGE_SHIFT)
                + (blkif_seg->first_sect << SECTOR_SHIFT);
            gcopy_seg->dest.foreign.ref = blkif_seg->gref;
            gcopy_seg->dest.foreign.offset = blkif_seg->first_sect << SECTOR_SHIFT;
            gcopy_seg->dest.foreign.domid = blkif->domid;
            gcopy_seg->flags = GNTCOPY_dest_gref;
        }

        gcopy_seg->len = (blkif_seg->last_sect
                - blkif_seg->first_sect
                + 1)
            << SECTOR_SHIFT;
    }
#else
        gcopy_seg->iov.iov_base = tapreq->vma + (i << PAGE_SHIFT)
            + (blkif_seg->first_sect << SECTOR_SHIFT);
        gcopy_seg->iov.iov_len = (blkif_seg->last_sect
                - blkif_seg->first_sect
                + 1)
            << SECTOR_SHIFT;
        gcopy_seg->ref = blkif_seg->gref;
        gcopy_seg->offset = blkif_seg->first_sect << SECTOR_SHIFT;
    }

    gcopy.dir = blkif_rq_wr(&tapreq->msg);
    gcopy.domid = blkif->domid;
#endif
    gcopy.count = tapreq->msg.nr_segments;
	gcopy.segments = tapreq->gcopy_segs;

    err = -ioctl(blkif->ctx->gntdev_fd, IOCTL_GNTDEV_GRANT_COPY, &gcopy);
    if (err) {
        err = -errno;
        RING_ERR(blkif, "failed to grant-copy request %"PRIu64" "
                "(%d segments): %s\n", tapreq->msg.id,
                tapreq->msg.nr_segments, strerror(-err));
        goto out;
    }

	for (i = 0; i < tapreq->msg.nr_segments; i++) {
		struct gntdev_grant_copy_segment *gcopy_seg = &tapreq->gcopy_segs[i];
		if (gcopy_seg->status != GNTST_okay) {
			/*
			 * TODO use gnttabop_error for reporting errors, defined in
			 * xen/extras/mini-os/include/gnttab.h (header not available to
			 * user space)
			 */
			RING_ERR(blkif, "req %lu: failed to grant-copy segment %d: %d\n",
                    tapreq->msg.id, i, gcopy_seg->status);
			err = -EIO;
			goto out;
		}
	}

out:
    return err;
}

static int
guest_copy(struct td_xenblkif * const blkif,
        struct td_xenblkif_req * const req) {
    if (blkif->ctx->persistent_grants &&
            blkif->persistent_gnt_c < xen_blkif_max_pgrants) {
        return guest_map(blkif, req);
    } else {
        return guest_copy2(blkif, req);
    }
}

/**
 * Completes a request. If this is the last pending request of a dead block
 * interface, the block interface is destroyed, the caller must not access it
 * any more.
 *
 * @blkif the VBD the request belongs belongs to
 * @tapreq the request to complete TODO rename to req
 * @error completion status of the request
 * @final controls whether the other end should be notified
 * @lock must always be true except in this function to control recursion
 */
static void
tapdisk_xenblkif_complete_request(struct td_xenblkif * const blkif,
		struct td_xenblkif_req* tapreq, int err, const int final,
		bool lock)
{
	int _err;
	long long *max = NULL, *sum = NULL, *cnt = NULL;
	static int depth = 0;
	bool processing_barrier_message;
	uint64_t *ticks = NULL;

	ASSERT(blkif);
	ASSERT(tapreq);
	ASSERT(depth >= 0);

	if (lock)
		pthread_mutex_lock(&blkif->mutex);
	depth++;

	processing_barrier_message =
		tapreq->msg.operation == BLKIF_OP_WRITE_BARRIER;

	/*
	 * If a barrier request completes, check whether it's an I/O completion
	 * (the barrier carries write I/O data), or a completion because the last
	 * pending non-barrier request completed. If the former case is true, we
	 * need to check again whether the latter is true and proceed with the
	 * completion, otherwise simply note the fact that I/O is done and when
	 * the last pending non-barrier requests completes, this function will be
	 * called again passing the barrier request.
	 */
	if (unlikely(processing_barrier_message)) {
		ASSERT(blkif->barrier.msg == &tapreq->msg);
		if (tapreq->msg.nr_segments && !blkif->barrier.io_done) {
			blkif->barrier.io_err = err;
			blkif->barrier.io_done = true;
		}
		if (!tapdisk_xenblkif_barrier_should_complete(blkif))
			goto out;
	}

	if (likely(!blkif->dead)) {
		if (blkif_rq_rd(&tapreq->msg)) {
			/*
			 * TODO stats should be collected after grant-copy for better
			 * accuracy
			 */
			if (likely(blkif->stats.xenvbd)) {
				cnt = &blkif->stats.xenvbd->st_rd_cnt;
				sum = &blkif->stats.xenvbd->st_rd_sum_usecs;
				max = &blkif->stats.xenvbd->st_rd_max_usecs;
			}
			blkif->vbd_stats.stats->read_reqs_completed++;
			ticks = &blkif->vbd_stats.stats->read_total_ticks;
			if (likely(!err)) {
				_err = guest_copy(blkif, tapreq);
				if (unlikely(_err)) {
					err = _err;
					RING_ERR(blkif, "req %lu: failed to copy from/to guest: "
							"%s\n", tapreq->msg.id, strerror(-err));
				}
			}
		} else if (blkif_rq_wr(&tapreq->msg)) {
			if (likely(blkif->stats.xenvbd)) {
				cnt = &blkif->stats.xenvbd->st_wr_cnt;
				sum = &blkif->stats.xenvbd->st_wr_sum_usecs;
				max = &blkif->stats.xenvbd->st_wr_max_usecs;
			}
			blkif->vbd_stats.stats->write_reqs_completed++;
			ticks = &blkif->vbd_stats.stats->write_total_ticks;
		} else if (blkif_rq_ds(&tapreq->msg)) {
			if (likely(blkif->stats.xenvbd)) {
				cnt = &blkif->stats.xenvbd->st_ds_cnt;
				sum = &blkif->stats.xenvbd->st_ds_sum_usecs;
				max = &blkif->stats.xenvbd->st_ds_max_usecs;
			}
			blkif->vbd_stats.stats->discard_reqs_completed++;
			ticks = &blkif->vbd_stats.stats->discard_total_ticks;
		}

		if (likely(cnt)) {
			struct timeval now;
			long long interval;
			gettimeofday(&now, NULL);
			interval = timeval_to_us(&now) - timeval_to_us(&tapreq->ts);
			*ticks += interval;
			if (interval > *max)
				*max = interval;

			*sum += interval;
			*cnt += 1;
		}

		if (likely(err == 0))
			_err = BLKIF_RSP_OKAY;
		else
			_err = BLKIF_RSP_ERROR;

		xenio_blkif_put_response(blkif, tapreq, _err, final);
	}

	tapdisk_xenblkif_free_request(blkif, tapreq);

	blkif->stats.reqs.out++;
	if (final)
		blkif->stats.kicks.out++;

	if (unlikely(processing_barrier_message))
		blkif->barrier.msg = NULL;

	/*
	 * Schedule a ring check in case we left requests in it due to lack of
	 * memory or in case we stopped processing it because of a barrier.
	 *
	 * FIXME we should decide whether a ring check is necessary more
	 * intelligently.
	*/
	if (!blkif->barrier.msg) {
		if (likely(!blkif->dead))
			tapdisk_xenblkif_sched_chkrng(blkif);
	} else {
		/*
		 * If this is the last request, complete the barrier request.
		 */
		if (tapdisk_xenblkif_barrier_should_complete(blkif)) {
			tapdisk_xenblkif_complete_request(blkif,
					msg_to_tapreq(blkif->barrier.msg), 0, 1, false);
                }
	}

	/*
	 * Last request of a dead ring completes, destroy the ring.
	 */
	if (unlikely(1 == depth
				&& blkif->dead
				&& !tapdisk_xenblkif_reqs_pending(blkif))) {

		RING_DEBUG(blkif, "destroying dead ring\n");
		pthread_mutex_unlock(&blkif->mutex);
		tapdisk_xenblkif_destroy(blkif);
		lock = 0; /* blkif with its mutex were destroyed above so don't try to unlock it */
	}

out:
	depth--;
	if (lock)
		pthread_mutex_unlock(&blkif->mutex);
}

/**
 * Request completion callback, executed when the tapdisk has finished
 * processing the request.
 *
 * @param vreq the completed request
 * @param error status of the request
 * @param token token previously associated with this request
 * @param final controls whether the other end should be notified
 */
static inline void
__tapdisk_xenblkif_request_cb(struct td_vbd_request * const vreq,
        const int error, void * const token, const int final)
{
    struct td_xenblkif_req *tapreq;
    struct td_xenblkif * const blkif = token;

    ASSERT(vreq);
    ASSERT(blkif);

    tapreq = container_of(vreq, struct td_xenblkif_req, vreq);

    if (error) {
        pthread_mutex_lock(&blkif->mutex);
        if (likely(!blkif->dead)) {
            blkif->stats.errors.img++;
            blkif->vbd_stats.stats->io_errors++;
        }
        pthread_mutex_unlock(&blkif->mutex);
    }

    tapdisk_xenblkif_complete_request(blkif, tapreq, error, final, true);
}


static inline int
tapdisk_xenblkif_parse_request(struct td_xenblkif * const blkif,
        struct td_xenblkif_req * const req)
{
    td_vbd_request_t *vreq;
    int i;
    struct td_iovec *iov;
    void *page, *next, *last;
    int err = 0;
    unsigned nr_sect = 0;

    ASSERT(blkif);
    ASSERT(req);

    vreq = &req->vreq;
    ASSERT(vreq);

    req->vma = td_xenblkif_bufcache_get(blkif);
    if (unlikely(!req->vma)) {
        err = errno;
        RING_ERR(blkif, "errno %d: invalid vma\n", err);
        goto out;
    }

    for (i = 0; i < req->msg.nr_segments; i++) {
        struct blkif_request_segment *seg = &req->msg.seg[i];
        req->gref[i] = seg->gref;

        /*
         * Note that first and last may be equal, which means only one sector
         * must be transferred.
         */
        if (seg->last_sect < seg->first_sect) {
            RING_ERR(blkif, "req %lu: invalid sectors %d-%d\n",
                    req->msg.id, seg->first_sect, seg->last_sect);
            err = EINVAL;
            goto out;
        }
    }

    /*
     * Vectorises the request: creates the struct iovec (in tapreq->iov) that
     * describes each segment to be transferred. Also, merges consecutive
     * segments.
     *
     * In each loop, iov points to the previous scatter/gather element in
     * order to reuse it if the current and previous segments are
     * consecutive.
     */
    iov = req->iov - 1;
    last = NULL;
    page = req->vma;

    for (i = 0; i < req->msg.nr_segments; i++) { /* for each segment */
        struct blkif_request_segment *seg = &req->msg.seg[i];
        size_t size;

        /* TODO check that first_sect/last_sect are within page */

        next = page + (seg->first_sect << SECTOR_SHIFT);
        size = seg->last_sect - seg->first_sect + 1;

        if (next != last) {
            iov++;
            iov->base = next;
            iov->secs = size;
        } else /* The "else" is true if fist_sect is 0. */
            iov->secs += size;

        last = iov->base + (iov->secs << SECTOR_SHIFT);
        page += PAGE_SIZE;
        nr_sect += size;
    }

    vreq->iov = req->iov;
    vreq->iovcnt = iov - req->iov + 1;
    vreq->sec = req->msg.sector_number;

    if (blkif_rq_wr(&req->msg)) {
        err = guest_copy(blkif, req);
        if (err) {
            RING_ERR(blkif, "req %lu: failed to copy from guest: %s\n",
                    req->msg.id, strerror(-err));
            goto out;
        }
		if (likely(blkif->stats.xenvbd))
			blkif->stats.xenvbd->st_wr_sect += nr_sect;
		if (likely(blkif->vbd_stats.stats))
			blkif->vbd_stats.stats->write_sectors += nr_sect;
    } else {
		if (likely(blkif->stats.xenvbd))
			blkif->stats.xenvbd->st_rd_sect += nr_sect;
		if (likely(blkif->vbd_stats.stats))
			blkif->vbd_stats.stats->read_sectors += nr_sect;
    } 

    /*
     * TODO Isn't this kind of expensive to do for each requests? Why does
     * the tapdisk need this in the first place?
     */
    snprintf(req->name, sizeof(req->name), "xenvbd-%d-%d.%"SCNx64"",
             blkif->domid, blkif->devid, req->msg.id);

    vreq->name = req->name;
    vreq->token = blkif;
    vreq->cb = __tapdisk_xenblkif_request_cb;

out:
    return err;
}

static inline int
tapdisk_xenblkif_parse_request_discard(struct td_xenblkif * const blkif,
				       struct td_xenblkif_req * const req)
{
	td_vbd_request_t *vreq;
	blkif_request_discard_t *req_discard;
	int err = 0;

	ASSERT(blkif);
	ASSERT(req);

	vreq = &req->vreq;
	ASSERT(vreq);

	req_discard = (blkif_request_discard_t *)&req->msg;
	vreq->sec = req_discard->sector_number;
	vreq->nr_sectors = req_discard->nr_sectors;

	if (likely(blkif->stats.xenvbd))
		blkif->stats.xenvbd->st_ds_sect += vreq->nr_sectors;
	if (likely(blkif->vbd_stats.stats))
		blkif->vbd_stats.stats->discard_sectors += vreq->nr_sectors;

	/*
	 * TODO Isn't this kind of expensive to do for each requests? Why does
	 * the tapdisk need this in the first place?
	 */
	snprintf(req->name, sizeof(req->name), "xenvbd-%d-%d.%"SCNx64"",
			blkif->domid, blkif->devid, req->msg.id);

	vreq->name = req->name;
	vreq->token = blkif;
	vreq->cb = __tapdisk_xenblkif_request_cb;

	return err;
}

/**
 * Initialises the standard tapdisk request (td_vbd_request_t) from the
 * intermediate ring request (td_xenblkif_req) in order to prepare it
 * processing.
 *
 * @param blkif the block interface
 * @param tapreq the request to prepare TODO rename to req
 * @returns 0 on success
 *
 * XXX only called by tapdisk_xenblkif_queue_request
 */
static inline int
tapdisk_xenblkif_make_vbd_request(struct td_xenblkif * const blkif,
        struct td_xenblkif_req * const tapreq)
{
    int err = 0;
    td_vbd_request_t *vreq;

    ASSERT(tapreq);

    vreq = &tapreq->vreq;
    ASSERT(vreq);
    memset(vreq, 0, sizeof(*vreq));

	tapreq->vma = NULL;
    switch (tapreq->msg.operation) {
    case BLKIF_OP_READ:
        if (likely(blkif->stats.xenvbd))
			blkif->stats.xenvbd->st_rd_req++;
	if (likely(blkif->vbd_stats.stats))
		blkif->vbd_stats.stats->read_reqs_submitted++;
        tapreq->prot = PROT_WRITE;
        vreq->op = TD_OP_READ;
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_WRITE_BARRIER:
        if (likely(blkif->stats.xenvbd) && tapreq->msg.nr_segments)
			blkif->stats.xenvbd->st_wr_req++;
	if (likely(blkif->vbd_stats.stats) && tapreq->msg.nr_segments)
		blkif->vbd_stats.stats->write_reqs_submitted++;
        tapreq->prot = PROT_READ;
        vreq->op = TD_OP_WRITE;
        break;
    case BLKIF_OP_DISCARD:
        if (likely(blkif->stats.xenvbd))
                blkif->stats.xenvbd->st_ds_req++;
        if (likely(blkif->vbd_stats.stats))
                blkif->vbd_stats.stats->discard_reqs_submitted++;
        tapreq->prot = PROT_READ;
        vreq->op = TD_OP_DISCARD;
        break;
    default:
        RING_ERR(blkif, "req %lu: invalid request type %d\n",
                tapreq->msg.id, tapreq->msg.operation);
        err = EOPNOTSUPP;
        goto out;
    }
    /* Timestamp before the requests leave the blkif layer */
    gettimeofday(&tapreq->ts, NULL);

    /*
     * Check that the number of segments is sane.
     */
    if (unlikely(tapreq->msg.operation != BLKIF_OP_DISCARD &&
                ((tapreq->msg.nr_segments == 0 &&
                tapreq->msg.operation != BLKIF_OP_WRITE_BARRIER) ||
            tapreq->msg.nr_segments > BLKIF_MAX_BUFFER_SEGMENTS_PER_REQUEST))) {
        RING_ERR(blkif, "req %lu: bad number of segments in request (%d)\n",
                tapreq->msg.id, tapreq->msg.nr_segments);
        err = EINVAL;
        goto out;
    }

    if (unlikely(tapreq->msg.operation == BLKIF_OP_DISCARD)) {
        pthread_mutex_lock(&blkif->mutex);
        err = tapdisk_xenblkif_parse_request_discard(blkif, tapreq);
        pthread_mutex_unlock(&blkif->mutex);
    } else if (likely(tapreq->msg.nr_segments)) {
        pthread_mutex_lock(&blkif->mutex);
        err = tapdisk_xenblkif_parse_request(blkif, tapreq);
        pthread_mutex_unlock(&blkif->mutex);
    /*
     * If we only got one request from the ring and that was a barrier one,
     * check whether the barrier requests completion conditions are satisfied
	 * and if they are, complete the barrier request.
     *
     * It could be that there are more requests in the ring after the barrier
     * request, tapdisk_xenblkif_complete_request() will schedule a ring check.
     */
    } else {
        pthread_mutex_lock(&blkif->mutex);
        if (tapdisk_xenblkif_barrier_should_complete(blkif)) {
            tapdisk_xenblkif_complete_request(blkif,
                    msg_to_tapreq(blkif->barrier.msg), 0, 1, false);
            err = 0;
        }
        pthread_mutex_unlock(&blkif->mutex);
    }
out:
    return err;
}


/**
 * Queues a ring request, after it prepares it, to the standard taodisk queue
 * for processing.
 *
 * @param blkif the block interface
 * @param msg the ring request
 * @param tapreq the intermediate request TODO rename to req
 *
 * TODO don't really need to supply the ring request since it's either way
 * contained in the tapreq
 *
 * XXX only called by tapdisk_xenblkif_queue_requests
 */
static inline int
tapdisk_xenblkif_queue_request(struct td_xenblkif * const blkif,
        blkif_request_t *msg, struct td_xenblkif_req *tapreq)
{
    int err;
    int queue_request;

    ASSERT(blkif);
    ASSERT(msg);
    ASSERT(tapreq);

    queue_request = tapreq->msg.operation == BLKIF_OP_DISCARD || tapreq->msg.nr_segments != 0;

    /*
     * Do not use tapreq after tapdisk_xenblkif_make_vbd_request
     * because this function can release tapreq->msg and reinsert it
     * in the reqs_free array.
     */
    err = tapdisk_xenblkif_make_vbd_request(blkif, tapreq);
    if (unlikely(err)) {
        /* TODO log error */
        blkif->stats.errors.map++;
        return err;
    }

	if (likely(queue_request)) {
		err = tapdisk_vbd_queue_request(blkif->vbd, &tapreq->vreq);
		if (unlikely(err)) {
			/* TODO log error */
			blkif->stats.errors.vbd++;
			return err;
		}
	}

    return 0;
}


void
tapdisk_xenblkif_queue_requests(struct td_xenblkif * const blkif,
        blkif_request_t *reqs[], const int nr_reqs)
{
    int i;
    int err;
    int nr_errors = 0;

    ASSERT(blkif);
    ASSERT(reqs);
    ASSERT(nr_reqs >= 0);

    for (i = 0; i < nr_reqs; i++) { /* for each request in the ring... */
        blkif_request_t *msg = reqs[i];
        struct td_xenblkif_req *tapreq;

        ASSERT(msg);

        tapreq = msg_to_tapreq(msg);

        ASSERT(tapreq);

        err = tapdisk_xenblkif_queue_request(blkif, msg, tapreq);
        if (err) {
            /* TODO log error */
            nr_errors++;
            tapdisk_xenblkif_complete_request(blkif, tapreq, err, 1, true);
        }
    }

    /* there is a possibility of blkif getting freed if ring is 
       dead and current request is the last one, hence adding 
       this check to avoid seg fault */

    if (nr_errors && blkif) {
        pthread_mutex_lock(&blkif->mutex);
        xenio_blkif_put_response(blkif, NULL, 0, 1);
        pthread_mutex_unlock(&blkif->mutex);
    }
}

void
tapdisk_xenblkif_reqs_free(struct td_xenblkif * const blkif)
{
    ASSERT(blkif);

    td_xenblkif_bufcache_free(blkif);
    td_xenblkif_free_pgnt_caches(blkif);
    td_xenblkif_bufcache_evt_unreg(blkif);

    free(blkif->reqs_bufcache);
    blkif->reqs_bufcache = NULL;

    free(blkif->reqs);
    blkif->reqs = NULL;

    free(blkif->reqs_free);
    blkif->reqs_free = NULL;

    pthread_mutex_destroy(&blkif->mutex);
}

int
tapdisk_xenblkif_reqs_init(struct td_xenblkif *td_blkif)
{
    void *buf;
    int i = 0;
    int err = 0;

    ASSERT(td_blkif);

    pthread_mutex_init(&td_blkif->mutex, NULL);

    td_blkif->ring_size = td_blkif_ring_size(td_blkif);
    ASSERT(td_blkif->ring_size > 0);

    td_blkif->reqs =
        calloc(td_blkif->ring_size, sizeof(struct td_xenblkif_req));
    if (!td_blkif->reqs) {
        err = -errno;
        goto fail;
    }

    td_blkif->reqs_free =
        malloc(td_blkif->ring_size * sizeof(struct blkif_request_t *));
    if (!td_blkif->reqs_free) {
        err = -errno;
        goto fail;
    }

    td_blkif->n_reqs_free = 0;
    for (i = 0; i < td_blkif->ring_size; i++)
        tapdisk_xenblkif_free_request(td_blkif, &td_blkif->reqs[i]);

    // Allocate the buffer cache
    td_blkif->reqs_bufcache = malloc(sizeof(void*) * td_blkif->ring_size);
    if (!td_blkif->reqs_bufcache) {
        err = -errno;
        goto fail;
    }
    td_blkif->n_reqs_bufcache_free = 0;
    td_blkif->reqs_bufcache_evtid = 0;

    // Populate cache with one buffer
    buf = td_xenblkif_bufcache_get(td_blkif);
    td_xenblkif_bufcache_put(td_blkif, buf);
    td_xenblkif_bufcache_evt_unreg(td_blkif);

    return 0;

fail:
    tapdisk_xenblkif_reqs_free(td_blkif);
    return err;
}
