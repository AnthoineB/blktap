lvm_incdir = include_directories('.')

lvmutil_sources = 'lvm-util.c'

lvmutil_slib = static_library(
  'lvmutil', lvmutil_sources,
  cpp_args : [
    '-D_GNU_SOURCE',
  ],
)

lvm_util_sources = 'main.c'

executable('lvm-util', lvm_util_sources,
  link_with : lvmutil_slib,
  c_args : [
    '-D_GNU_SOURCE',
  ],
)
