if enable_tests == true
  subdir('test')
endif

vhd_sources = [
  'libvhd.c',
  'libvhd-journal.c',
  'libvhd-index.c',
  'vhd-util-coalesce.c',
  'vhd-util-copy.c',
  'vhd-util-create.c',
  'vhd-util-fill.c',
  'vhd-util-modify.c',
  'vhd-util-query.c',
  'vhd-util-read.c',
  'vhd-util-repair.c',
  'vhd-util-resize.c',
  'vhd-util-revert.c',
  'vhd-util-set-field.c',
  'vhd-util-snapshot.c',
  'vhd-util-scan.c',
  'vhd-util-check.c',
  'vhd-util-key.c',
  'io-util.c',
  'io-util.h',
  'relative-path.c',
  'relative-path.h',
  'canonpath.c',
  'canonpath.h',
  'xattr.c',
  'xattr.h',
  ]

vhd_libs = both_libraries(
  'vhd',
  vhd_sources,
  include_directories : [
    incdir,
    lvm_incdir,
  ],
  dependencies : [
    uuid_dep,
  ],
  link_with : lvmutil_slib,
  c_args : [
    '-D_GNU_SOURCE',
  ],
  version : '0.1.1',
)
