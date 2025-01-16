AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = .
#$(MAYBE_test)

AM_CFLAGS  = -Wall
AM_CFLAGS += -Werror
AM_CFLAGS += $(if $(GCOV),-fprofile-dir=/tmp/coverage/blktap/qcow2/lib -fprofile-arcs -ftest-coverage)

AM_CPPFLAGS  = -D_GNU_SOURCE
AM_CPPFLAGS += -I$(top_srcdir)/include
AM_CPPFLAGS += -I$(top_srcdir)/lvm
AM_CPPFLAGS += -I$(top_srcdir)/part
AM_CPPFLAGS += $(GLIB_CFLAGS)
AM_CPPFLAGS += $(ZLIB_CFLAGS)
AM_CPPFLAGS += $(GNUTLS_CFLAGS)

lib_LTLIBRARIES  = libqcow2.la
lib_LTLIBRARIES += $(MAYBE_libqcow2io_la)

libqcow2_la_SOURCES  = qcow2.c
libqcow2_la_SOURCES += qcow2-refcount.c
libqcow2_la_SOURCES += qcow2-cache.c
libqcow2_la_SOURCES += qcow2-cluster.c
libqcow2_la_SOURCES += qcow2-bitmap.c
libqcow2_la_SOURCES += qcow2-threads.c
libqcow2_la_SOURCES += qcow2-snapshot.c
libqcow2_la_SOURCES += block.c
libqcow2_la_SOURCES += blockdev.c
libqcow2_la_SOURCES += blockjob.c
libqcow2_la_SOURCES += block/accounting.c
libqcow2_la_SOURCES += block/aio_task.c
libqcow2_la_SOURCES += block/block-backend.c
libqcow2_la_SOURCES += block/block-gen.c
libqcow2_la_SOURCES += block/commit.c
libqcow2_la_SOURCES += block/dirty-bitmap.c
libqcow2_la_SOURCES += block/file-posix.c
libqcow2_la_SOURCES += block/graph-lock.c
libqcow2_la_SOURCES += block/io.c
libqcow2_la_SOURCES += block/linux-aio.c
libqcow2_la_SOURCES += block/mirror.c
libqcow2_la_SOURCES += block/monitor/bitmap-qmp-cmds.c
libqcow2_la_SOURCES += block/progress_meter.c
libqcow2_la_SOURCES += block/qapi.c
libqcow2_la_SOURCES += block/raw-format.c
libqcow2_la_SOURCES += block/snapshot.c
libqcow2_la_SOURCES += crypto/hash.c
libqcow2_la_SOURCES += hw/block/block.c
libqcow2_la_SOURCES += hw/block/hd-geometry.c
libqcow2_la_SOURCES += job.c
libqcow2_la_SOURCES += job-qmp.c
libqcow2_la_SOURCES += qapi/qapi-dealloc-visitor.c
libqcow2_la_SOURCES += qapi/qapi-events-block-core.c
libqcow2_la_SOURCES += qapi/qapi-events-job.c
libqcow2_la_SOURCES += qapi/qapi-types-block-core.c
libqcow2_la_SOURCES += qapi/qapi-types-common.c
libqcow2_la_SOURCES += qapi/qapi-types-job.c
libqcow2_la_SOURCES += qapi/qapi-util.c
libqcow2_la_SOURCES += qapi/qapi-visit-block-core.c
libqcow2_la_SOURCES += qapi/qapi-visit-common.c
libqcow2_la_SOURCES += qapi/qapi-visit-core.c
libqcow2_la_SOURCES += qapi/qapi-visit-job.c
libqcow2_la_SOURCES += qapi/qmp-dispatch.c
libqcow2_la_SOURCES += qapi/qmp-event.c
libqcow2_la_SOURCES += qapi/qobject-input-visitor.c
libqcow2_la_SOURCES += qapi/qobject-output-visitor.c
libqcow2_la_SOURCES += qobject/block-qdict.c
libqcow2_la_SOURCES += qobject/json-lexer.c
libqcow2_la_SOURCES += qobject/json-parser.c
libqcow2_la_SOURCES += qobject/json-streamer.c
libqcow2_la_SOURCES += qobject/json-writer.c
libqcow2_la_SOURCES += qobject/qbool.c
libqcow2_la_SOURCES += qobject/qdict.c
libqcow2_la_SOURCES += qobject/qjson.c
libqcow2_la_SOURCES += qobject/qlist.c
libqcow2_la_SOURCES += qobject/qnull.c
libqcow2_la_SOURCES += qobject/qnum.c
libqcow2_la_SOURCES += qobject/qobject.c
libqcow2_la_SOURCES += qobject/qstring.c
libqcow2_la_SOURCES += system/cpus.c
libqcow2_la_SOURCES += util/aio-posix.c
libqcow2_la_SOURCES += util/aio-wait.c
libqcow2_la_SOURCES += util/aiocb.c
libqcow2_la_SOURCES += util/async.c
libqcow2_la_SOURCES += util/bitmap.c
libqcow2_la_SOURCES += util/bitops.c
libqcow2_la_SOURCES += util/bufferiszero.c
libqcow2_la_SOURCES += util/compatfd.c
libqcow2_la_SOURCES += util/coroutine-ucontext.c
libqcow2_la_SOURCES += util/cutils.c
libqcow2_la_SOURCES += util/defer-call.c
libqcow2_la_SOURCES += util/error.c
libqcow2_la_SOURCES += util/error-report.c
libqcow2_la_SOURCES += util/event_notifier-posix.c
libqcow2_la_SOURCES += util/fdmon-epoll.c
libqcow2_la_SOURCES += util/fdmon-poll.c
libqcow2_la_SOURCES += util/hbitmap.c
libqcow2_la_SOURCES += util/host-utils.c
libqcow2_la_SOURCES += util/id.c
libqcow2_la_SOURCES += util/iov.c
libqcow2_la_SOURCES += util/lockcnt.c
libqcow2_la_SOURCES += util/main-loop.c
libqcow2_la_SOURCES += util/memalign.c
libqcow2_la_SOURCES += util/module.c
libqcow2_la_SOURCES += util/notify.c
libqcow2_la_SOURCES += util/osdep.c
libqcow2_la_SOURCES += util/oslib-posix.c
libqcow2_la_SOURCES += util/qemu-coroutine.c
libqcow2_la_SOURCES += util/qemu-coroutine-lock.c
libqcow2_la_SOURCES += util/qemu-option.c
libqcow2_la_SOURCES += util/qemu-print.c
libqcow2_la_SOURCES += util/qemu-thread-posix.c
libqcow2_la_SOURCES += util/qemu-timer.c
libqcow2_la_SOURCES += util/qsp.c
libqcow2_la_SOURCES += util/rcu.c
libqcow2_la_SOURCES += util/stats64.c
libqcow2_la_SOURCES += util/thread-pool.c
libqcow2_la_SOURCES += util/timed-average.c
libqcow2_la_SOURCES += util/transactions.c
libqcow2_la_SOURCES += util/unicode.c


libqcow2_la_LDFLAGS = -version-info 1:1:1

libqcow2_la_LIBADD = -lpthread -luuid -laio -ldl $(LIBICONV)  $(top_srcdir)/lvm/liblvmutil.la
libqcow2_la_LIBADD += $(GLIB_LIBS)
libqcow2_la_LIBADD += $(ZLIB_LIBS)
libqcow2_la_LIBADD += $(GNUTLS_LIBS)
libqcow2_la_LIBADD += $(LIBZSTD_LIBS)

#if ENABLE_TESTS
#MAYBE_test = test
#endif

clean-local:
	-rm -rf *.gc??
