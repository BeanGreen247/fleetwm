tomlplusplus_dep = dependency('tomlplusplus', required: false)
if not tomlplusplus_dep.found()
  # Not subproject('tomlplusplus').get_variable('tomlplusplus_dep'): that
  # subproject's own meson.build built a compiled libtomlplusplus.so
  # despite build_lib defaulting (and being explicitly passed) false --
  # unclear why, not worth fighting further -- and per its own
  # `install: not is_subproject` it's never installed by `ninja install`
  # either way. fleetwm-settings (the first binary to actually run
  # against libcommon at runtime) failed at startup with
  # "libtomlplusplus.so.3: cannot open shared object file", confirmed
  # via real testing. toml++ is header-only capable by design, so
  # sidestep its build system entirely and point straight at the
  # checked-out include/ directory instead.
  tomlplusplus_dep = declare_dependency(
    include_directories: include_directories('../../subprojects/tomlplusplus/include'),
  )
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
