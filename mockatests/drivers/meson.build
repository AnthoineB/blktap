
#AM_CFLAGS  = -Wall
#AM_CFLAGS += -Werror
#AM_CFLAGS += -fprofile-arcs -ftest-coverage
#AM_CFLAGS += -Og -fno-inline-functions -g

#AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/drivers -I../include

test_drivers_sources = [
  'test-drivers.c',
  'test-tapdisk-stats.c',
  'test-tapdisk-vbd.c',
  'vbd-wrappers.c',
  'test-tapdisk-nbdserver.c',
  'test-scheduler.c',
  ]

test_drivers = executable('test-drivers', test_drivers_sources,
  include_directories : [
    incdir,
    mockatests_include,
    drivers_include,
  ],
  c_args : [
    '-fno-inline-functions',
  ] + c_args_tests,
  dependencies : [
    uuid_dep,
    cmocka_dep,
    gcov_dep,
  ],
  link_with : [
    tapdisk_slib,
  ],
  link_args : [
    '-Wl,--wrap=tapdisk_image_check_request',
    '-Wl,--wrap=td_queue_block_status',
    '-Wl,--wrap=send',
    '-Wl,--wrap=tapdisk_server_register_event',
    '-Wl,--wrap=gettimeofday',
  ],
  override_options : [
    'debug=true',
    'optimization=g',
  ],
)
test('test_drivers', test_drivers)
