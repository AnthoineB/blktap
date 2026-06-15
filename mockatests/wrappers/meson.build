
#CFLAGS:=$(filter-out -fsanitize=leak,$(CFLAGS))
#LDFLAGS:=$(filter-out -static-liblsan,$(LDFLAGS))

#AM_CFLAGS = -Wall -g -Og
#AM_CFLAGS += -Werror -fno-inline

#AM_CPPFLAGS = -I../include

wrappers_slib = static_library(
  'wrappers',
  'wrappers.c',
  include_directories : mockatests_include,
  c_args : [
    '-fno-inline',
  ],
  override_options : [
    'debug=true',
    'optimization=g',
  ],
)
