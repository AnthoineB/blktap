
#CFLAGS:=$(filter-out -fsanitize=leak,$(CFLAGS))
#LDFLAGS:=$(filter-out -static-liblsan,$(LDFLAGS))

mockatests_include = include_directories('include')

cmocka_dep = dependency('cmocka')

subdir('wrappers')
if enable_tests == true
  compiler = meson.get_compiler('c')
  gcov_dep = compiler.find_library('gcov')

  subdir('drivers')
  subdir('cbt')
  subdir('control')
  subdir('vhd')
endif
