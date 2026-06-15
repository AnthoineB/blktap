subdir('lib')

vhd_util_sources = [
  'vhd-util.c',
  ]

executable('vhd-util', vhd_util_sources,
  include_directories : incdir,
  dependencies : uuid_dep,
  link_with : [
    vhd_libs.get_static_lib(),
  ],
  c_args : [
    '-D_GNU_SOURCE',
  ],
)

vhd_index_sources = [
  'vhd-index.c',
  ]

executable('vhd-index', vhd_index_sources,
  include_directories : incdir,
  dependencies : uuid_dep,
  link_with : [
    vhd_libs.get_static_lib(),
  ],
  c_args : [
    '-D_GNU_SOURCE',
  ],
)
