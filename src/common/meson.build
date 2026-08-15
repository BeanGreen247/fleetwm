tomlplusplus_dep = dependency('tomlplusplus', required: false)
if not tomlplusplus_dep.found()
  tomlplusplus_dep = subproject('tomlplusplus').get_variable('tomlplusplus_dep')
endif

libcommon = static_library(
  'fleetwm-common',
  files(
    'theme.cpp',
    'ipc_client.cpp',
  ),
  dependencies: [tomlplusplus_dep],
  include_directories: include_directories('.'),
)

libcommon_dep = declare_dependency(
  link_with: libcommon,
  include_directories: include_directories('.'),
  dependencies: [tomlplusplus_dep],
)
