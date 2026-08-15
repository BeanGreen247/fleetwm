pam_dep = cpp.find_library('pam', required: get_option('greeter'))

executable(
  'fleetwm-greet',
  files(
    'main.cpp',
    'tty.cpp',
    'auth.cpp',
    'session.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [pam_dep],
  install: true,
)
