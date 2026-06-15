
tapback_sources = [
  'xenstore.c',
  'frontend.c',
  'backend.c',
  'tapback.h',
  'tapback.c',
  ]

# 'tapback.service',

xenstore_dep = dependency('xenstore')

executable('tapback', tapback_sources,
  include_directories : incdir,
  dependencies : xenstore_dep,
  link_with : [
    blktapctl_libs.get_shared_lib(),
  ],
  c_args : [
    '-D_GNU_SOURCE', # required by vasprintf
  ],
  override_options : 'warning_level=2',
)

# Have "exec" in name to ensure it's done trough the install-exec route
# (and before install-exec-local)
#SYSTEMD_SERVICE_DIR = /usr/lib/systemd/system
#
#install-exec-local:
#	mkdir -p $(DESTDIR)$(SYSTEMD_SERVICE_DIR)
#	install -m 644 tapback.service \
#	  $(DESTDIR)$(SYSTEMD_SERVICE_DIR)
