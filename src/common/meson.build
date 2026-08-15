# build_lib=false: toml++ is header-only by design and that's its own
# default, but without pinning it explicitly here its subproject build
# compiled a shared libtomlplusplus.so that `ninja install` never
# installs (toml++'s own `install: not is_subproject` correctly disables
# installing it as a subproject) -- fleetwm-settings, the first binary to
# actually link libcommon at runtime, failed at startup with
# "libtomlplusplus.so.3: cannot open shared object file", confirmed via
# real testing. default_options here (not a separate subproject() call)
# is what actually reaches the implicit subproject invocation that
# dependency()'s fallback-via-the-wrap's [provide] section triggers --
# an explicit subproject('tomlplusplus', ...) call site is a second,
# divergent invocation of the same subproject and does not share this
# fallback's cached configuration, which is why an earlier attempt at
# this exact fix silently didn't take effect.
tomlplusplus_dep = dependency(
  'tomlplusplus',
  required: false,
  default_options: [
    'build_lib=false',
    'compile_library=false',
    'build_examples=false',
    'build_tests=false',
    'build_tt=false',
  ],
)
if not tomlplusplus_dep.found()
  tomlplusplus_dep = subproject(
    'tomlplusplus',
    default_options: [
      'build_lib=false',
      'compile_library=false',
      'build_examples=false',
      'build_tests=false',
      'build_tt=false',
    ],
  ).get_variable('tomlplusplus_dep')
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
