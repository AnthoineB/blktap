cbt_include = include_directories('.')

cbtutil_sources = 'cbt-util.c'

cbtutil_slib = static_library('cbtutil',
  cbtutil_sources,
  include_directories : incdir,
)

cbt_util_sources = 'main.c'

executable('cbt-util', cbt_util_sources,
  dependencies : uuid_dep,
  link_with : cbtutil_slib,
)
