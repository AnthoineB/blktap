/*
 * Copyright (c) 2024, Vates
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

/*
 * block-dummy.c: test implementation.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <uuid/uuid.h>
#include <string.h>
#include <libaio.h>
#include <sys/mman.h>
#include <limits.h>
#include <dlfcn.h>

#include "debug.h"
#include "tapdisk.h"
#include "tapdisk-driver.h"
#include "tapdisk-interface.h"
#include "tapdisk-disktype.h"
#include "tapdisk-storage.h"

#define DEBUGGING   0

#define __TRACE(s)							\
	do {								\
		DBG(TLOG_DBG, "QUEUED: %" PRIu64 ", COMPLETED: %"	\
                    PRIu64", RETURNED: %" PRIu64 ", DATA_ALLOCATED: "	\
                    "%u\n",				\
                    s->queued, s->completed, s->returned,	\
                    DUMMY_REQS - s->vreq_free_count);                   \
	} while(0)

#if (DEBUGGING == 1)
  #define DBG(level, _f, _a...)      DPRINTF(_f, ##_a)
  #define ERR(_s, err, _f, _a...)    DPRINTF("ERROR: %d: " _f, err, ##_a)
  #define TRACE(s)                   ((void)0)
#elif (DEBUGGING == 2)
  #define DBG(level, _f, _a...)      tlog_write(level, _f, ##_a)
  #define ERR(_s, _err, _f, _a...)   tlog_drv_error((_s)->driver, _err, _f, ##_a)
  #define TRACE(s)                   __TRACE(s)
#else
  #define DBG(level, _f, _a...)      ((void)0)
  #define ERR(_s, err, _f, _a...)    ((void)0)
  #define TRACE(s)                   ((void)0)
#endif

enum dummy_ops {
        DUMMY_OP_READ,
        DUMMY_OP_WRITE,
};

struct dummy_state;
struct dummy_request;

#define DUMMY_REQS 64

struct dummy_request {
    int                     error;
    enum dummy_ops          op;
    td_request_t            treq;
    struct dummy_state      *state;
    //QLIST_ENTRY(dummy_request) list;
    //int                     aio_inflight;
#if DEBUGGING != 0
    int                     id;
    struct timeval          reqtv;
#endif
};

struct dummy_state {
    td_driver_t               *driver;
#if 0
    const char                *name;
    struct td_vbd_encryption  *encryption;
    td_flag_t                 td_flags;
    dummy_flag_t              flags;

    BlockConf                 conf;
    int                       blk_shift;
#endif

    int                       vreq_free_count;
    struct dummy_request      *vreq_free[DUMMY_REQS];
    struct dummy_request      vreq_list[DUMMY_REQS];
#if 0
    QLIST_HEAD(inflight_head, dummy_request) inflight;

    QEMUBH                    *bh;
    AioContext                *ctx;
#endif

    uint64_t                  queued;
    uint64_t                  completed;
    uint64_t                  returned;
    uint64_t                  reads;
    uint64_t                  read_size;
    uint64_t                  writes;
    uint64_t                  write_size;
};

static int
dummy_open(td_driver_t *driver, const char *name,
           struct td_vbd_encryption *encryption, td_flag_t flags)
{
    int i;
    struct dummy_state *s = (struct dummy_state *)driver->data;

    memset(s, 0, sizeof(struct dummy_state));

    s->driver = driver;

    s->vreq_free_count = DUMMY_REQS;
    for (i = 0; i < DUMMY_REQS; i++)
        s->vreq_free[i] = s->vreq_list + i;

    driver->info.size        = 20 * 1024 * 1024;
    driver->info.sector_size = 1 << SECTOR_SHIFT;
    driver->info.info        = 0;

    return 0;
}

static int
dummy_close(td_driver_t *driver)
{
    return 0;
}

int
dummy_validate_parent(td_driver_t *child_driver,
                      td_driver_t *parent_driver, td_flag_t flags)
{
	return 0;
}

int
dummy_get_parent_id(td_driver_t *driver, td_disk_id_t *id)
{
	return TD_NO_PARENT;
}

static inline void
init_dummy_request(struct dummy_state *s, struct dummy_request *req)
{
	req->state = s;
#if DEBUGGING != 0
	req->id = req - s->vreq_list;
        gettimeofday(&req->reqtv, NULL);
#endif
}

static inline struct dummy_request *
alloc_dummy_request(struct dummy_state *s)
{
	struct dummy_request *req = NULL;

	if (s->vreq_free_count > 0) {
		req = s->vreq_free[--s->vreq_free_count];
		init_dummy_request(s, req);
		return req;
	}

	return NULL;
}

static inline void
free_dummy_request(struct dummy_state *s, struct dummy_request *req)
{
	s->vreq_free[s->vreq_free_count++] = req;
}

static inline void
signal_completion(struct dummy_request *r)
{
	struct dummy_state *s = r->state;

        td_complete_request(r->treq, 0);

        free_dummy_request(s, r);

        s->returned++;
        TRACE(s);
}

static void dummy_complete(void *opaque)
{
    struct dummy_request *req = (struct dummy_request *)opaque;
    struct dummy_state *s = req->state;
#if DEBUGGING != 0
    unsigned long long interval;
    struct timeval now;
#endif

    if (!s) {
        DBG(TLOG_DBG, "s is null\n");
        return;
    }

    req->error = 0;

    if (req->error)
        ERR(s, req->error, "%s: op: %u, lsec: %"PRIu64", secs: %u, "
                "blk: %"PRIu64", blk_offset: ",
                req->treq.image->name, req->op, req->treq.sec, req->treq.secs,
                req->treq.sec);

#if DEBUGGING != 0
    gettimeofday(&now, NULL);
    interval = timeval_to_us(&now) - timeval_to_us(&req->reqtv);
    DBG(TLOG_DBG, "%d: interval %llu us\n", req->id, interval);
#endif

    s->completed++;
    TRACE(s);

    switch (req->op) {
        case DUMMY_OP_READ:
            signal_completion(req);
            break;

        case DUMMY_OP_WRITE:
            DBG(TLOG_DBG, "%s: op: %u, lsec: %"PRIu64", secs: %u, "
                "blk: %"PRIu64", blk_offset: ",
                req->treq.image->name, req->op, req->treq.sec, req->treq.secs,
                req->treq.sec);
            signal_completion(req);
            break;

        default:
            ASSERT(0);
            break;
    }
}

static int
schedule_request(struct dummy_state *s, td_request_t *treq, enum dummy_ops op)
{
	struct dummy_request *req;

	req = alloc_dummy_request(s);
	if (!req)
		return -EBUSY;

	req->treq  = *treq;
	req->op    = op;

	s->queued++;
	if (op == DUMMY_OP_READ) {
		s->reads++;
		s->read_size += req->treq.secs;
	} else {
		s->writes++;
		s->write_size += req->treq.secs;
	}

	dummy_complete(req);

	TRACE(s);

	DBG(TLOG_DBG, "%s: lsec: 0x%08"PRIx64", "
            "nr_secs: 0x%08x, buf: %p, id %d\n",
            treq->image->name, treq->sec, treq->secs,
            treq->buf, req->id);

	return 0;
}

static void
dummy_queue_block_status(td_driver_t *driver, td_request_t treq)
{
}

static void
dummy_queue_read(td_driver_t *driver, td_request_t treq)
{
    struct dummy_state *s = (struct dummy_state *)driver->data;
    int err;

    err = schedule_request(s, &treq, DUMMY_OP_READ);
    if (err)
        goto fail;

    return;
fail:
    td_complete_request(treq, err);
}

static void
dummy_queue_write(td_driver_t *driver, td_request_t treq)
{
    struct dummy_state *s = (struct dummy_state *)driver->data;
    int err;

    err = schedule_request(s, &treq, DUMMY_OP_WRITE);
    if (err)
        goto fail;

    return;
fail:
    td_complete_request(treq, err);
}

void
dummy_debug(td_driver_t *driver)
{
}

struct tap_disk tapdisk_dummy = {
	.disk_type          = "tapdisk_dummy",
	.flags              = 0,
	.private_data_size  = sizeof(struct dummy_state),
	.td_open            = dummy_open,
	.td_close           = dummy_close,
	.td_queue_read      = dummy_queue_read,
	.td_queue_block_status = dummy_queue_block_status,
	.td_queue_write     = dummy_queue_write,
	.td_get_parent_id   = dummy_get_parent_id,
	.td_validate_parent = dummy_validate_parent,
	.td_debug           = dummy_debug,
};
