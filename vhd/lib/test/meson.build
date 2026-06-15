random_copy_sources = 'random-copy.c'

executable('random-copy', random_copy_sources,
  include_directories : incdir,
  c_args : [
    '-D_GNU_SOURCE',
  ],
)

test_snapshot_sources = 'test-snapshot.c'

executable('test-snapshot', test_snapshot_sources,
  include_directories : incdir,
  c_args : [
    '-D_GNU_SOURCE',
  ],
)
