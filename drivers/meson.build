drivers_include = include_directories('.')

#AM_CFLAGS += $(if $(GCOV),-fprofile-arcs -ftest-coverage)
#AM_CPPFLAGS += -I$(top_srcdir)/include

tapdisk_sources = [
  'tapdisk.h',
  'timeout-math.h',
  'scheduler.c',
  'scheduler.h',
  'tapdisk-control.c',
  'tapdisk-control.h',
  'tapdisk-vbd.c',
  'tapdisk-vbd.h',
  'linux-blktap.h',
  'tapdisk-protocol-new.h',
  'tapdisk-nbdserver.c',
  'tapdisk-nbdserver.h',
  'tapdisk-image.c',
  'tapdisk-image.h',
  'tapdisk-driver.c',
  'tapdisk-driver.h',
  'tapdisk-disktype.c',
  'tapdisk-disktype.h',
  'tapdisk-interface.c',
  'tapdisk-interface.h',
  'tapdisk-server.c',
  'tapdisk-server.h',
  'posixaio-backend.c',
  'posixaio-backend.h',
  'libaio-backend.c',
  'libaio-backend.h',
  'tapdisk-logfile.c',
  'tapdisk-logfile.h',
  'tapdisk-log.c',
  'tapdisk-log.h',
  'tapdisk-utils.c',
  'tapdisk-utils.h',
  'tapdisk-syslog.c',
  'tapdisk-syslog.h',
  'tapdisk-stats.c',
  'tapdisk-stats.h',
  'tapdisk-metrics.c',
  'tapdisk-metrics.h',
  'tapdisk-storage.c',
  'tapdisk-storage.h',
  'tapdisk-loglimit.c',
  'tapdisk-loglimit.h',
  'io-optimize.c',
  'io-optimize.h',
  'tapdisk-fdreceiver.c',
  'tapdisk-fdreceiver.h',
  'log.h',

  # block drivers
  'block-aio.c',
  'aio_getevents.c',
  'block-aio.h',
  'block-ram.c',
  'block-cache.c',
  'block-vhd.c',
  'block-valve.c',
  'block-valve.h',
  'block-vindex.c',
  'block-lcache.c',
  'block-llcache.c',
  'block-nbd.c',
  'block-log.c',

  # shared ring
  'td-blkif.c',
  'td-blkif.h',
  'td-req.c',
  'td-req.h',
  'td-ctx.c',
  'td-ctx.h',
  'td-stats.c',
  'td-stats.h',
  ]

compiler = meson.get_compiler('c')
aio_dep = compiler.find_library('aio')
zlib_dep = dependency('zlib')
evtchn_dep = dependency('xenevtchn')
gnttab_dep = dependency('xengnttab')

tapdisk_slib = static_library('tapdisk',
  tapdisk_sources,
  include_directories : incdir,
  dependencies : [
    aio_dep,
    evtchn_dep,
    gnttab_dep,
    zlib_dep,
  ],
  link_with : [
    vhd_libs.get_shared_lib(),
  ],
  c_args : [
    '-D_GNU_SOURCE',
  ],
)

blockcrypto_sources = [
  'block-crypto.c',
  'block-crypto.h',
  'crypto/xts_aes.c',
  'crypto/xts_aes.h',
  ]

crypto_dep = dependency('libcrypto')

blockcrypto_libs = shared_library('blockcrypto',
  blockcrypto_sources,
  include_directories : incdir,
  c_args : [
    '-D_GNU_SOURCE',
  ],
  dependencies : crypto_dep,
  version : '0.0.0',
  override_options : [
    'b_lundef=false',
  ],
)

tapdisk_sources = 'tapdisk2.c'

executable('tapdisk', tapdisk_sources,
  link_with : tapdisk_slib,
  include_directories : incdir,
  dependencies : [
    uuid_dep,
  ],
  c_args : [
    '-D_GNU_SOURCE',
  ],
)

tapdisk_stream_sources = 'tapdisk-stream.c'

executable('tapdisk-stream', tapdisk_stream_sources,
  link_with : tapdisk_slib,
  include_directories : incdir,
  dependencies : [
    uuid_dep,
  ],
  c_args : [
    '-D_GNU_SOURCE',
  ],
)

td_util_sources = 'td.c'

executable('td-util', td_util_sources,
  link_with : tapdisk_slib,
  include_directories : incdir,
  dependencies : [
    uuid_dep,
  ],
  c_args : [
    '-D_GNU_SOURCE',
  ],
)

td_rated_sources = 'td-rated.c'

executable('td-rated', td_rated_sources,
  link_with : tapdisk_slib,
  include_directories : incdir,
  c_args : [
    '-D_GNU_SOURCE',
  ],
)
