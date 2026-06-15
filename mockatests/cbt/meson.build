
test_cbt_util_sources = [
  'test-cbt-util.c',
  'test-cbt-util-commands.c',
  'test-cbt-util-set.c',
  'test-cbt-util-get.c',
  'test-cbt-util-create.c',
  'test-cbt-util-coalesce.c',
  ]

test_cbt_util = executable('test-cbt-util', test_cbt_util_sources,
  include_directories : [
    incdir,
    mockatests_include,
    cbt_include,
  ],
  c_args : [
    '-fno-inline-functions',
  ] + c_args_tests,
  dependencies : [
    cmocka_dep,
    uuid_dep,
    gcov_dep,
  ],
  link_with : [
    cbtutil_slib,
    wrappers_slib,
  ],
  link_args : [
    '-Wl,--wrap=fopen,--wrap=fclose',
    '-Wl,--wrap=malloc,--wrap=free,--wrap=calloc',
    # Need to wrap both of these as rpmbuild/mock set -D_FORTIFY_SOURCE=2
    '-Wl,--wrap=printf,--wrap=__printf_chk',
    # GCC will "optimise" printf calls - http://www.ciselant.de/projects/gcc_printf/gcc_printf.html
    '-Wl,--wrap=puts',
    '-Wl,--wrap=fwrite',
    '-Wl,--wrap=fseek',
  ],
  override_options : [
    'debug=true',
    'optimization=g',
  ],
)
test('test_cbt_util', test_cbt_util)
