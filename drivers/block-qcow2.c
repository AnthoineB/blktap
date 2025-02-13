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
 * block-qcow2.c: asynchronous qcow2 implementation.
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
#include <math.h>

#include "debug.h"
#include "qemu/osdep.h"
#include "qcow2.h"
#include "qemu/main-loop.h"
#include "hw/block/block.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-commands-job.h"

#include "tapdisk.h"
#include "tapdisk-driver.h"
#include "tapdisk-interface.h"
#include "tapdisk-disktype.h"
#include "tapdisk-storage.h"
//#include "block-crypto.h"

#define DEBUGGING   2

#define __TRACE(s)							\
	do {								\
		DBG(TLOG_DBG, "%s: QUEUED: %" PRIu64 ", COMPLETED: %"	\
                    PRIu64", RETURNED: %" PRIu64 ", DATA_ALLOCATED: "	\
                    "%u\n",				\
                    blk_name(s->conf.blk), s->queued, s->completed, s->returned,	\
                    QCOW2_REQS - s->vreq_free_count);                   \
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

enum qcow2_ops {
    QCOW2_OP_READ,
    QCOW2_OP_WRITE,
    QCOW2_OP_COMMIT,
    QCOW2_OP_QUERY,
};

struct qcow2_state;
struct qcow2_request;

#define QCOW2_REQS TAPDISK_DATA_REQUESTS

struct qcow2_request {
    int                     error;
    enum qcow2_ops          op;
    union {
        /* OP_READ, OP_WRITE */
        td_request_t        treq;
        /* OP_COMMIT */
        char *              top;
    };
    struct qcow2_state      *state;
    BlockAIOCB              *aiocb;
    QSIMPLEQ_ENTRY(qcow2_request) list;
    int                     aio_inflight;
    QEMUIOVector            qiov;
#if DEBUGGING != 0
    int                     id;
    struct timeval          allocate_tv;
    struct timeval          submit_tv;
    struct timeval          complete_tv;
#endif
};

#if DEBUGGING != 0
struct io_stat {
    uint64_t max_val;
    uint64_t min_val;
    uint64_t samples;

    double mean;
    double S;
};
#endif

struct qcow2_state {
    td_driver_t               *driver;
    const char                *name;
    struct td_vbd_encryption  *encryption;
    td_flag_t                 flags;

    BlockConf                 conf;
    int                       blk_shift;

    int                       vreq_free_count;
    struct qcow2_request      *vreq_free[QCOW2_REQS];
    struct qcow2_request      vreq_list[QCOW2_REQS];
    QSIMPLEQ_HEAD(inflight_head, qcow2_request) inflight;

    QEMUBH                    *bh;
    AioContext                *ctx;

    /* Open thread */
    pthread_t                 thread;
    pthread_mutex_t           lock;
    pthread_cond_t            cond;
    bool                      driver_opened;
    int                       open_status;
    MemReentrancyGuard        mem_reentrancy_guard;

    /* commit/query synchronization */
#define COMMIT_JOB_ID "JIDCOMMIT0"
    pthread_mutex_t           commit_lock;
    pthread_cond_t            commit_cond;
    JobInfo                   job_info;

    /* Stats */
    uint64_t                  queued;
    uint64_t                  completed;
    uint64_t                  returned;
    uint64_t                  reads;
    uint64_t                  read_size;
    uint64_t                  writes;
    uint64_t                  write_size;
    uint64_t                  kick;
    uint64_t                  schedule;
#if DEBUGGING != 0
    struct io_stat            read_slat;
    struct io_stat            read_clat;
    struct io_stat            read_lat;
    struct io_stat            write_slat;
    struct io_stat            write_clat;
    struct io_stat            write_lat;
#endif
};

#define test_qcow2_flag(word, flag)  ((word) & (flag))
#define set_qcow2_flag(word, flag)   ((word) |= (flag))
#define clear_qcow2_flag(word, flag) ((word) &= ~(flag))

static void qcow2_complete(void *, int);
static inline void do_aio_read(struct qcow2_state *s, struct qcow2_request *req);
static inline void do_aio_write(struct qcow2_state *s, struct qcow2_request *req);
static inline void do_commit(struct qcow2_state *s, struct qcow2_request *req);
static inline void do_query_commit_job(struct qcow2_state *s, struct qcow2_request *req);

static int
qcow2_initialize(struct qcow2_state *s, Error **perr)
{
	int err;

#if DEBUGGING != 0
        s->read_slat.min_val = ULONG_MAX;
        s->read_clat.min_val = ULONG_MAX;
        s->read_lat.min_val = ULONG_MAX;
        s->write_slat.min_val = ULONG_MAX;
        s->write_clat.min_val = ULONG_MAX;
        s->write_lat.min_val = ULONG_MAX;
#endif

        qemu_init_cpu_loop();
        bql_lock();

        bdrv_init();

        err = qemu_init_main_loop(perr);
        if (err != 0) {
            EPRINTF("failed to initialize main loop %d\n", err);
            return err;
        }

	return 0;
}

static void
qcow2_free(struct qcow2_state *s)
{
        qemu_deinit_main_loop();

        bql_unlock();
        qemu_deinit_cpu_loop();
}

static void qcow2_handle_requests(struct qcow2_state *s)
{
    struct qcow2_request *req;

    pthread_mutex_lock(&s->lock);
    while ((req = QSIMPLEQ_FIRST(&s->inflight))) {
        QSIMPLEQ_REMOVE_HEAD(&s->inflight, list);
        pthread_mutex_unlock(&s->lock);

	switch (req->op) {
            case QCOW2_OP_READ:
                do_aio_read(s, req);
                break;
            case QCOW2_OP_WRITE:
                do_aio_write(s, req);
                break;
            case QCOW2_OP_COMMIT:
                do_commit(s, req);
                break;
            case QCOW2_OP_QUERY:
                do_query_commit_job(s, req);
                break;
        }
        pthread_mutex_lock(&s->lock);
    }
    pthread_mutex_unlock(&s->lock);
}

static void block_bh(void *opaque)
{
    struct qcow2_state *s = opaque;

    qcow2_handle_requests(s);
}

static void *
qcow2_open(void *opaque)
{
    int o_flags, err, i;
    struct qcow2_state *s;
    td_driver_t *driver;
    td_flag_t flags;
    const char *name;
    QDict *options;
    QDict *file_layer;
    Error *local_err = NULL;
    BlockConf *conf = NULL;
    bool writethrough;
    bool has_aio_native = true;
    const char *cache = "none";
    bool has_discard = false;
    const char *discard = "off";

    s = opaque;
    conf = &s->conf;
    driver = s->driver;
    name = s->name;
    flags = s->flags;

    err = qcow2_initialize(s, &local_err);
    if (err) {
        error_report("Initialization error: %s", error_get_pretty(local_err));
        goto fail1;
    }

    o_flags = 0;

    if (test_qcow2_flag(flags, TD_OPEN_RDONLY))
        clear_qcow2_flag(o_flags, BDRV_O_RDWR);
    else
        set_qcow2_flag(o_flags, BDRV_O_RDWR);

    if ((test_qcow2_flag(flags, TD_OPEN_RDONLY) ||
                test_qcow2_flag(flags, TD_OPEN_LOCAL_CACHE)) &&
            test_qcow2_flag(flags, TD_OPEN_NO_O_DIRECT)) {
        clear_qcow2_flag(o_flags, BDRV_O_NOCACHE);
        cache = "writeback";
        has_aio_native = false;
    }

    if (test_qcow2_flag(flags, TD_OPEN_QUERY)) {
        clear_qcow2_flag(o_flags, BDRV_O_RDWR);
        set_qcow2_flag(o_flags, BDRV_O_NO_IO);
        set_qcow2_flag(o_flags, BDRV_O_NOCACHE);
    }
    if (test_qcow2_flag(o_flags, TD_OPEN_LOCAL_CACHE)) {
        cache = "writeback";
        has_aio_native = false;
    }

    options = qdict_new();
    file_layer = qdict_new();

    qdict_put_str(file_layer, "filename", name);

    qdict_put_str(options, "driver", "qcow2");

    if (has_aio_native) {
        has_discard = true;
        discard = "unmap";
    }

    if (has_discard) {
        qdict_put_str(options, "discard", discard);
        qdict_put_str(file_layer, "discard", discard);
        qdict_put_str(options, "detect-zeroes", discard);
        qdict_put_str(file_layer, "detect-zeroes", discard);
    }

#if 0
    QDict *cache_qdict = qdict_new();
    qdict_put_bool(cache_qdict, "direct", true);
    qdict_put(file_layer, "cache", cache_qdict);
#endif

    if (has_aio_native) {
        qdict_put_str(file_layer, "aio", "native");
        o_flags |= BDRV_O_NATIVE_AIO;
    }

    qdict_put(options, "file", file_layer);

    err = bdrv_parse_cache_mode(cache, &o_flags, &writethrough);
    if (err < 0) {
        error_setg(&local_err, "Invalid source cache option: %s", cache);
        goto fail1;
    }

    if (has_discard) {
        err = bdrv_parse_discard_flags(discard, &o_flags);
        if (err < 0) {
            error_setg(&local_err, "Invalid discard option: %s", cache);
            goto fail1;
        }
    }

    DPRINTF("discard %d (%s), aio_native %d (cache %s), flags %x\n", has_discard, discard, has_aio_native, cache, o_flags);

    conf->blk = blk_new_open(name, NULL, options, o_flags, &local_err);
    if (!conf->blk) {
        goto fail1;
    }
    blk_set_enable_write_cache(conf->blk, !writethrough);


    if (!blk_is_inserted(conf->blk)) {
        error_setg(&local_err, "device needs media, but drive is empty");
        goto fail;
    }

    if (!blkconf_apply_backend_options(conf, test_qcow2_flag(flags, TD_OPEN_RDONLY), true, &local_err)) {
        goto fail;
    }

    if (!blkconf_geometry(conf, NULL, 65535, 255, 255, &local_err)) {
        goto fail;
    }

    if (!blkconf_blocksizes(conf, &local_err)) {
        goto fail;
    }

    if (conf->discard_granularity == -1) {
        conf->discard_granularity = conf->physical_block_size;
    }

    s->vreq_free_count = QCOW2_REQS;
    for (i = 0; i < QCOW2_REQS; i++) {
            s->vreq_free[i] = s->vreq_list + i;
            qemu_iovec_init(&s->vreq_free[i]->qiov, 1);
    }

    s->blk_shift = 31 - clz32(conf->logical_block_size);
    driver->info.size        = blk_getlength(conf->blk) >> s->blk_shift;
    driver->info.sector_size = conf->logical_block_size;
    driver->info.info        = 0;

    QSIMPLEQ_INIT(&s->inflight);
    s->ctx = qemu_get_aio_context();
    s->bh = aio_bh_new_guarded(s->ctx, block_bh,
                               s,
                               &s->mem_reentrancy_guard);

    DBG(TLOG_INFO, "qcow2_open: ctx %p bh %p\n", s->ctx, s->bh);

    DBG(TLOG_INFO, "qcow2_open: done (sz:%"PRIu64", sct:%lu, inf:%u)\n",
        driver->info.size, driver->info.sector_size, driver->info.info);

    pthread_mutex_lock(&s->lock);
    s->open_status = 0;
    pthread_cond_signal(&s->cond);

    while (s->driver_opened) {
        pthread_mutex_unlock(&s->lock);
        main_loop_wait(false);
        pthread_mutex_lock(&s->lock);
    }
    pthread_mutex_unlock(&s->lock);

    blk_set_aio_context(conf->blk, qemu_get_aio_context(), &error_abort);

    blk_drain_all();

    qemu_bh_delete(s->bh);

    s->vreq_free_count = QCOW2_REQS;
    for (i = 0; i < QCOW2_REQS; i++) {
            qemu_iovec_destroy(&s->vreq_list[i].qiov);
    }

    blk_unref(conf->blk);

    drain_call_rcu();

    qcow2_free(s);

    return NULL;
fail:
    blk_unref(conf->blk);
fail1:
    EPRINTF("open error: %s\n", error_get_pretty(local_err));
    error_free(local_err);

    pthread_mutex_lock(&s->lock);
    s->open_status = -EINVAL;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);

    qcow2_free(s);
    return NULL;
}

static int
_qcow2_open(td_driver_t *driver, const char *name,
            struct td_vbd_encryption *encryption, td_flag_t flags)
{
    int err;
    struct qcow2_state *s;

    /* pre-allocate for all but NFS and LVM storage */
    driver->storage = tapdisk_storage_type(name);

    DPRINTF("%s: name %s, flags %x\n", __func__, name, flags);

    s = (struct qcow2_state *)driver->data;
    memset(s, 0, sizeof(struct qcow2_state));

    s->driver = driver;
    s->name = name;
    s->encryption = encryption;
    s->flags = flags;

    err = pthread_cond_init(&s->cond, NULL);
    if (err) {
        EPRINTF("failed to init thread condition %d\n", err);
        return err;
    }
    pthread_mutex_init(&s->lock, NULL);
    err = pthread_cond_init(&s->commit_cond, NULL);
    if (err) {
        EPRINTF("failed to init thread condition %d\n", err);
        return err;
    }
    pthread_mutex_init(&s->commit_lock, NULL);

    pthread_mutex_lock(&s->lock);
    s->driver_opened = true;
    pthread_create(&s->thread, NULL, qcow2_open, s);

    pthread_cond_wait(&s->cond, &s->lock);
    err = s->open_status;
    s->open_status = 0;
    pthread_mutex_unlock(&s->lock);

    return err;
}

static int
_qcow2_close(td_driver_t *driver)
{
    void *res;
    int err;
    struct qcow2_state *s = (struct qcow2_state *)driver->data;

    DBG(TLOG_WARN, "qcow2_close\n");

    pthread_mutex_lock(&s->lock);
    s->driver_opened = false;
    pthread_mutex_unlock(&s->lock);

    qemu_bh_schedule(s->bh);

    err = pthread_join(s->thread, &res);
    if (err) {
        EPRINTF("failed to join thread %d\n", err);
        return err;
    }

    err = pthread_cond_destroy(&s->commit_cond);
    if (err) {
        EPRINTF("failed to destroy thread condition %d\n", err);
        return err;
    }
    err = pthread_mutex_destroy(&s->commit_lock);
    if (err) {
        EPRINTF("failed to destroy mutex %d\n", err);
        return err;
    }

    err = pthread_cond_destroy(&s->cond);
    if (err) {
        EPRINTF("failed to destroy thread condition %d\n", err);
        return err;
    }
    err = pthread_mutex_destroy(&s->lock);
    if (err) {
        EPRINTF("failed to destroy mutex %d\n", err);
        return err;
    }

    memset(s, 0, sizeof(struct qcow2_state));

    return 0;
}

int
qcow2_validate_parent(td_driver_t *child_driver,
                      td_driver_t *parent_driver, td_flag_t flags)
{
	DPRINTF("qcow2_validate_parent. ptype %d, ctype %d",
		parent_driver->type, child_driver->type);
	if (parent_driver->type != DISK_TYPE_QCOW)
	{
		if (child_driver->type != DISK_TYPE_QCOW)
			return -EINVAL;
		return 0;
	}


	/* TODO: compare sizes */

	return 0;
}

int
qcow2_get_parent_id(td_driver_t *driver, td_disk_id_t *id)
{
	DBG(TLOG_DBG, "\n");
	memset(id, 0, sizeof(td_disk_id_t));

	//s = (struct qcow2_state *)driver->data;

#if 0
	if (bdrv_is_root_node(blk_bs(s->blk)))
#endif
		return TD_NO_PARENT;

	return 0;
}

static inline void
init_qcow2_request(struct qcow2_state *s, struct qcow2_request *req)
{
	//memset(req, 0, sizeof(struct qcow2_request));
	req->state = s;
#if DEBUGGING != 0
	req->id = req - s->vreq_list;
        gettimeofday(&req->allocate_tv, NULL);
#endif
}

static inline struct qcow2_request *
alloc_qcow2_request(struct qcow2_state *s)
{
	struct qcow2_request *req = NULL;

        pthread_mutex_lock(&s->lock);
	if (s->vreq_free_count > 0) {
		req = s->vreq_free[--s->vreq_free_count];
                pthread_mutex_unlock(&s->lock);
		ASSERT(req->treq.secs == 0);
		init_qcow2_request(s, req);
		return req;
	}

        pthread_mutex_unlock(&s->lock);
	return NULL;
}

static inline void
free_qcow2_request(struct qcow2_state *s, struct qcow2_request *req)
{
	memset(&req->treq, 0, sizeof(req->treq));
        pthread_mutex_lock(&s->lock);
	s->vreq_free[s->vreq_free_count++] = req;
        pthread_mutex_unlock(&s->lock);

        qemu_iovec_reset(&req->qiov);
}

static inline void
signal_completion(struct qcow2_request *r)
{
	struct qcow2_state *s = r->state;
        td_vbd_t *vbd = r->treq.vreq->vbd;

        td_complete_request(r->treq, r->error);
        DBG(TLOG_DBG, "lsec: 0x%08"PRIx64", blk: 0x%04x, "
                "err: %d\n", r->treq.sec, r->treq.secs, r->error);
        if (r->error == 0) {
                tapdisk_vbd_kick(vbd);
                s->kick++;
        }
        free_qcow2_request(s, r);

        s->returned++;
        TRACE(s);
}

#if DEBUGGING != 0
static void sum_stat(struct io_stat *dst, struct io_stat *src, bool first)
{
    double mean, S;

    dst->min_val = MIN(dst->min_val, src->min_val);
    dst->max_val = MAX(dst->max_val, src->max_val);

    if (first) {
        mean = src->mean;
        S = src->S;
    } else {
        double delta = src->mean - dst->mean;

        mean = ((src->mean * src->samples) +
                (dst->mean * dst->samples)) /
                (dst->samples + src->samples);

        S =  src->S + dst->S + pow(delta, 2.0) *
                (dst->samples * src->samples) /
                (dst->samples + src->samples);
    }

    dst->samples += src->samples;
    dst->mean = mean;
    dst->S = S;
}

bool calc_lat(struct io_stat *is, uint64_t *min,
              uint64_t *max, double *mean, double *dev)
{
    double n = (double) is->samples;

    if (n == 0)
        return false;

    *min = is->min_val;
    *max = is->max_val;
    *mean = is->mean;

    if (n > 1.0)
        *dev = sqrt(is->S / (n - 1.0));
    else
        *dev = 0;

    return true;
}

#define update_latency(dst, latency) \
{ \
    struct io_stat ios; \
    ios.samples = 1; \
    ios.S = 0; \
    ios.min_val = ios.max_val = ios.mean = (latency); \
    sum_stat(&(dst), &ios, dst.samples == 0); \
}

#define print_latency(s, name, lat) \
{ \
    if (calc_lat(&(s)->name ## _ ## lat, &min, &max, &mean, &dev)) { \
        DBG(TLOG_WARN, #name ": " #lat " min %luus, max %luus, mean %fus, S %f, N %lu\n", \
            min, max, mean, dev, (s)->name ## _ ## lat.samples); \
    } else { \
        DBG(TLOG_WARN, #name ": " #lat " min 0us, max 0us, mean 0us, S 0.0, N 0\n"); \
    } \
}

#define print_latencies(s) \
{ \
    uint64_t min, max; \
    double mean, dev; \
    print_latency((s), read, slat); \
    print_latency((s), read, clat); \
    print_latency((s), read, lat); \
    print_latency((s), write, slat); \
    print_latency((s), write, clat); \
    print_latency((s), write, lat); \
}

#endif

static void qcow2_complete(void *opaque, int ret)
{
    struct qcow2_request *req = (struct qcow2_request *)opaque;
    struct qcow2_state *s = req->state;
#if DEBUGGING != 0
    unsigned long long latency, submit_latency, complete_latency;
    struct timeval now;
#endif

    if (!s) {
        DBG(TLOG_DBG, "s is null\n");
        return;
    }

    req->error = ret;

    if (req->error)
        ERR(s, req->error, "%s: op: %u, lsec: 0x%08"PRIx64", secs: 0x%04x",
                req->treq.image->name, req->op, req->treq.sec, req->treq.secs);

    DBG(TLOG_DBG, "%d: inflight %d\n", req->id, req->aio_inflight);

#if DEBUGGING != 0
    gettimeofday(&now, NULL);
    latency = timeval_to_us(&now) - timeval_to_us(&req->allocate_tv);
    submit_latency = timeval_to_us(&req->submit_tv) - timeval_to_us(&req->allocate_tv);
    complete_latency = timeval_to_us(&now) - timeval_to_us(&req->submit_tv);
#endif

    s->completed++;
    TRACE(s);

    switch (req->op) {
        case QCOW2_OP_READ:
            signal_completion(req);
#if DEBUGGING != 0
            update_latency(s->read_lat, latency);
            update_latency(s->read_slat, submit_latency);
            update_latency(s->read_clat, complete_latency);
#endif
            break;

        case QCOW2_OP_WRITE:
            DBG(TLOG_DBG, "%s: op: %u, lsec: 0x%08"PRIx64", secs: 0x%04x",
                req->treq.image->name, req->op, req->treq.sec, req->treq.secs);
            signal_completion(req);
#if DEBUGGING != 0
            update_latency(s->write_lat, latency);
            update_latency(s->write_slat, submit_latency);
            update_latency(s->write_clat, complete_latency);
#endif
            break;

        default:
            ASSERT(0);
            break;
    }
}

static inline void
do_aio_read(struct qcow2_state *s, struct qcow2_request *req)
{
        BlockBackend *blk = s->conf.blk;

        qemu_iovec_add(&req->qiov, req->treq.buf, req->treq.secs << s->blk_shift);

        req->aio_inflight++;
#if DEBUGGING != 0
        gettimeofday(&req->submit_tv, NULL);
#endif
        req->aiocb = blk_aio_preadv(blk, req->treq.sec << s->blk_shift, &req->qiov, 0, qcow2_complete, req);

	s->queued++;
	s->reads++;
	s->read_size += req->treq.secs;
	TRACE(s);
}

static inline void
do_aio_write(struct qcow2_state *s, struct qcow2_request *req)
{
        BlockBackend *blk = s->conf.blk;

        qemu_iovec_add(&req->qiov, req->treq.buf, req->treq.secs << s->blk_shift);

        req->aio_inflight++;
#if DEBUGGING != 0
        gettimeofday(&req->submit_tv, NULL);
#endif
        req->aiocb = blk_aio_pwritev(blk, req->treq.sec << s->blk_shift, &req->qiov, 0, qcow2_complete, req);

	s->queued++;
	s->writes++;
	s->write_size += req->treq.secs;
	TRACE(s);
}

static int
schedule_request(struct qcow2_state *s, td_request_t *treq, enum qcow2_ops op)
{
	struct qcow2_request *req = NULL;

	req = alloc_qcow2_request(s);
	if (!req)
		return -EBUSY;

	req->treq  = *treq;
	req->op    = op;

        pthread_mutex_lock(&s->lock);
        QSIMPLEQ_INSERT_TAIL(&s->inflight, req, list);
        pthread_mutex_unlock(&s->lock);

        qemu_bh_schedule(s->bh);
        s->schedule++;

	DBG(TLOG_DBG, "%s: lsec: 0x%08"PRIx64", "
            "nr_secs: 0x%08x, buf: %p, id %d\n",
            treq->image->name, treq->sec, treq->secs,
            treq->buf, req->id);

	return 0;
}

static void
qcow2_queue_block_status(td_driver_t *driver, td_request_t treq)
{
#if 0
    struct qcow2_state *s = (struct qcow2_state *)driver->data;
    BlockBackend *blk = s->conf.blk;
    BlockDriverState *file;
    int64_t pnum, map;

    DBG(TLOG_DBG, "block status: %s: lsec: 0x%08"PRIx64", secs: 0x%04x (seg: %d)\n",
            blk_name(blk), treq.sec, treq.secs, treq.sidx);

    blk_co_block_status_above(blk, NULL, treq.sec, treq.secs, &pnum, &map, &file);
#endif
    treq.status = TD_BLOCK_STATE_NONE;
}

static void
qcow2_queue_read(td_driver_t *driver, td_request_t treq)
{
    struct qcow2_state *s = (struct qcow2_state *)driver->data;
    int err;

    DBG(TLOG_DBG, "%s: lsec: 0x%08"PRIx64", secs: 0x%04x (seg: %d)\n",
            treq.image->name, treq.sec, treq.secs, treq.sidx);

    err = schedule_request(s, &treq, QCOW2_OP_READ);
    if (err)
        goto fail;

    return;
fail:
    DBG(TLOG_DBG, "request failed\n");
    td_complete_request(treq, err);
}

static void
qcow2_queue_write(td_driver_t *driver, td_request_t treq)
{
    struct qcow2_state *s = (struct qcow2_state *)driver->data;
    int err;

    DBG(TLOG_DBG, "%s: lsec: 0x%08"PRIx64", secs: 0x%04x, (seg: %d)\n",
            treq.image->name, treq.sec, treq.secs, treq.sidx);

    err = schedule_request(s, &treq, QCOW2_OP_WRITE);
    if (err)
        goto fail;

    return;
fail:
    DBG(TLOG_DBG, "request failed\n");
    td_complete_request(treq, err);
}

int
qcow2_commit(td_driver_t *driver, const char *name)
{
    struct qcow2_state *s = (struct qcow2_state *)driver->data;
    struct qcow2_request *req;
    int err;

    if (name == NULL)
        return -EINVAL;

    DBG(TLOG_WARN, "Qcow2: commit %s.\n", name);

    req = alloc_qcow2_request(s);
    if (!req)
        return -EBUSY;

    req->top   = strdup(name);
    req->op    = QCOW2_OP_COMMIT;

    pthread_mutex_lock(&s->lock);
    QSIMPLEQ_INSERT_TAIL(&s->inflight, req, list);
    pthread_mutex_unlock(&s->lock);

    pthread_mutex_lock(&s->commit_lock);
    qemu_bh_schedule(s->bh);

    pthread_cond_wait(&s->commit_cond, &s->commit_lock);
    err = req->error;
    pthread_mutex_unlock(&s->commit_lock);

    free(req->top);
    free_qcow2_request(s, req);

    return err;
}

static inline void
do_commit(struct qcow2_state *s, struct qcow2_request *req)
{
    Error *local_err = NULL;
    char *node, *top_node, *base_node;
    BlockDriverState *bs, *top_bs, *base_bs;
    int err = 0;

    bs = blk_bs(s->conf.blk);
    node = bs->node_name;
    if (strcmp(bs->filename, req->top) == 0) {
        top_bs = bs;
    } else {
        top_bs = bdrv_find_backing_image(bs, req->top);
    }
    top_node = top_bs->node_name;
    base_bs = bdrv_backing_chain_next(top_bs);
    base_node = base_bs->node_name;

    DBG(TLOG_DBG, "Qcow2: block commit %s (node-name: '%s').\n", req->top, node);
    DBG(TLOG_DBG, "Qcow2: block commit: top-node: '%s' on '%s' base-node).\n", top_node, base_node);

    qmp_block_commit(COMMIT_JOB_ID, node, base_node, NULL, top_node, NULL, NULL,
            false, false, false, 0, false, BLOCKDEV_ON_ERROR_REPORT,
            NULL, false, false, true, false, &local_err);

    if (local_err) {
        DPRINTF("qcow2_commit: error: %s\n", error_get_pretty(local_err));
        error_free(local_err);
        err = -EINVAL;
    }

    pthread_mutex_lock(&s->commit_lock);
    req->error = err;
    pthread_cond_signal(&s->commit_cond);
    pthread_mutex_unlock(&s->commit_lock);
}

int
qcow2_query_commit_job(td_driver_t *driver, td_query_t *query)
{
    struct qcow2_state *s = (struct qcow2_state *)driver->data;
    struct qcow2_request *req;
    int err;

    DBG(TLOG_DBG, "Qcow2: query commit job.\n");

    req = alloc_qcow2_request(s);
    if (!req)
        return -EBUSY;

    req->op    = QCOW2_OP_QUERY;

    pthread_mutex_lock(&s->lock);
    QSIMPLEQ_INSERT_TAIL(&s->inflight, req, list);
    pthread_mutex_unlock(&s->lock);

    pthread_mutex_lock(&s->commit_lock);
    qemu_bh_schedule(s->bh);

    pthread_cond_wait(&s->commit_cond, &s->commit_lock);

    if (query) {
        query->status = JobStatus_str(s->job_info.status);
        query->current_progress = s->job_info.current_progress;
        query->total_progress = s->job_info.total_progress;
    }

    memset(&s->job_info, 0, sizeof(JobInfo));

    err = req->error;
    pthread_mutex_unlock(&s->commit_lock);

    DBG(TLOG_WARN, "Qcow2: query commit job done (%d).\n", err);

    free_qcow2_request(s, req);

    return err;
}

static inline void
do_query_commit_job(struct qcow2_state *s, struct qcow2_request *req)
{
    Error *local_err = NULL;
    int err = 0;
    BlockJob *bjob;
    JobStatus status;
    uint64_t current, total;

    job_lock();
    bjob = block_job_get_locked(COMMIT_JOB_ID);
    if (!bjob) {
        job_unlock();
        DPRINTF("Qcow2: no job running.\n");
        goto signal;
    }

    status  = bjob->job.status;
    current = bjob->job.progress.current;
    total   = bjob->job.progress.total;

    if (status == JOB_STATUS_READY) {
        Job *job = &bjob->job;
        job_complete_locked(job, &local_err);
        if (local_err) {
            DPRINTF("Qcow2: job complete error: %s\n", error_get_pretty(local_err));
            error_free(local_err);
            err = -EINVAL;
        }
    }
    if (status == JOB_STATUS_CONCLUDED) {
        Job *job = &bjob->job;
        job_dismiss_locked(&job, &local_err);
        if (local_err) {
            DPRINTF("Qcow2: job dismiss error: %s\n", error_get_pretty(local_err));
            error_free(local_err);
            err = -EINVAL;
        }
    }
    job_unlock();

    DPRINTF("Qcow2: commit job '%s'.\n", JobStatus_str(status));

    pthread_mutex_lock(&s->commit_lock);
    s->job_info.status = status;
    s->job_info.current_progress = current;
    s->job_info.total_progress = total;
    pthread_mutex_unlock(&s->commit_lock);

signal:
    pthread_mutex_lock(&s->commit_lock);
    req->error = err;
    pthread_cond_signal(&s->commit_cond);
    pthread_mutex_unlock(&s->commit_lock);
}

void
qcow2_debug(td_driver_t *driver)
{
    struct qcow2_state *s = (struct qcow2_state *)driver->data;

    DBG(TLOG_WARN, "Qcow2: %s: queued %lu, completed %lu, returned %lu, "
            "reads %lu, read sz avg %f, "
            "writes %lu, write sz avg %f, schedule %lu, kick %lu\n",
            blk_name(s->conf.blk),
            s->queued, s->completed, s->returned,
            s->reads, (s->reads ? ((float)s->read_size / s->reads) : 0.0),
            s->writes, (s->writes ? ((float)s->write_size / s->writes) : 0.0),
            s->schedule, s->kick);

#if DEBUGGING != 0
    print_latencies(s);
#endif
}

struct tap_disk tapdisk_qcow = {
	.disk_type          = "tapdisk_qcow2",
	.flags              = 0,
	.private_data_size  = sizeof(struct qcow2_state),
	.td_open            = _qcow2_open,
	.td_close           = _qcow2_close,
	.td_queue_read      = qcow2_queue_read,
	.td_queue_block_status = qcow2_queue_block_status,
	.td_queue_write     = qcow2_queue_write,
	.td_get_parent_id   = qcow2_get_parent_id,
	.td_validate_parent = qcow2_validate_parent,
	.td_commit          = qcow2_commit,
	.td_query_commit_job = qcow2_query_commit_job,
	.td_debug           = qcow2_debug,
};
