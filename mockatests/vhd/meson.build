
test_vhd_util_sources = [
  'test-vhd-util.c',
  'test-vhd-util-snapshot.c',
  'test-canonpath.c',
  'test-bitops.c',
  'test-vhd-util-utilities.c',
  'vhd-wrappers.c',
  ]

test_vhd_util = executable('test-vhd-util', test_vhd_util_sources,
  include_directories : [
    incdir,
    mockatests_include,
  ],
  c_args : [
    '-D_GNU_SOURCE',
    '-Doff64_t=__off64_t',
    '-fno-inline-functions',
  ] + c_args_tests,
  dependencies : [
    cmocka_dep,
    gcov_dep,
  ],
  link_with : [
    vhd_libs.get_static_lib(),
  ],
  link_args : [
    '-Wl,--wrap=malloc,--wrap=free,--wrap=realloc',
    '-Wl,--wrap=canonpath',
    '-Wl,--wrap=vhd_flag_test',
    '-Wl,--wrap=vhd_snapshot',
    '-Wl,--wrap=vhd_open',
    '-Wl,--wrap=vhd_get_keyhash',
    '-Wl,--wrap=vhd_set_keyhash',
    #'-Wl,--wrap=close',
    '-Wl,--wrap=get_current_dir_name,--wrap=realpath',
  ],
  override_options : [
    'debug=true',
    'optimization=g',
  ],
)
test('test_vhd_util', test_vhd_util)
