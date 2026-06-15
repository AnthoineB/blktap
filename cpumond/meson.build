#AM_CFLAGS += $(if $(GCOV),-fprofile-arcs -ftest-coverage)

# -I$(top_srcdir)/include

cpumond_sources = [
    'cpumond.c',
    'cpumond.h',
    ]

# 'cpumond.service',
#SYSTEMD_SERVICE_DIR = /usr/lib/systemd/system

executable('cpumond', cpumond_sources,
  link_with : blktapctl_libs.get_static_lib(),
  include_directories : incdir,
  c_args : [
    '-D_GNU_SOURCE',
  ],
)
