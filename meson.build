project(
  'tapdisk',
  'c',
  default_options : [
    'c_std=gnu11',
    'warning_level=1',
    'werror=true',
    'buildtype=plain',
  ],
)

enable_tests = false
c_args_tests = []
gcov = get_option('GCOV')
if gcov == true
  enable_tests = true
  c_args_tests = ['-fprofile-arcs', '-ftest-coverage']
endif

incdir = include_directories('include')

uuid_dep = dependency('uuid')

subdir('cbt')
subdir('control')
subdir('cpumond')
subdir('lvm')
subdir('vhd')
subdir('drivers')
subdir('tapback')
subdir('mockatests')

#doc = README README.md LICENSE CONTRIB MAINTAINERS

#TODO: uuid dep in drivers, debug, tests, install
