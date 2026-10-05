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

# Real installed system config dir (e.g. /usr/local/etc/fleetwm with this
# project's default --prefix=/usr/local), baked in at compile time so
# system_default_config_path()/themes_dir() actually find what
# `ninja install` puts there -- get_option('sysconfdir') alone
# (previously hardcoded as the unrelated "/etc/xdg/fleetwm" string in
# theme.cpp) never matched the real install path on this project's
# default prefix, a latent bug independent of this dependency now being
# fixed alongside adding themes_dir().
common_conf = configuration_data()
common_conf.set_quoted(
  'FLEETWM_SYSCONF_DIR',
  get_option('prefix') / get_option('sysconfdir') / 'fleetwm',
)
common_conf.set_quoted(
  'FLEETWM_BINDIR',
  get_option('prefix') / get_option('bindir'),
)
configure_file(output: 'paths_config.h', configuration: common_conf)

gtk4_dep_common = dependency('gtk4')

# libcommon_core: everything that needs only toml++ -- config parsing, paths,
# IPC client, malloc tuning. GTK-free so lean (non-GTK) clients such as
# fleetwm-wallpaper link it without pulling GTK/GLib/Pango into the process.
libcommon_core = static_library(
  'fleetwm-common-core',
  files(
    'config_paths.cpp',
    'theme.cpp',
    'ipc_client.cpp',
    'bar_config.cpp',
    'wallpaper_config.cpp',
    'default_apps.cpp',
    'keybinds_config.cpp',
    'battery_reading.cpp',
    'malloc_tuning.cpp',
    'pipewire_json.cpp',
  ),
  dependencies: [tomlplusplus_dep],
  include_directories: include_directories('.'),
)

libcommon_core_dep = declare_dependency(
  link_with: libcommon_core,
  include_directories: include_directories('.'),
  dependencies: [tomlplusplus_dep],
)

# libcommon: core + the GTK/GLib-specific helpers (CSS styling, clean-quit
# signal glue, battery source). Linked by every GTK client.
libcommon = static_library(
  'fleetwm-common',
  files(
    'app_style.cpp',
    'battery_source.cpp',
    'clean_quit.cpp',
  ),
  dependencies: [tomlplusplus_dep, gtk4_dep_common],
  link_with: libcommon_core,
  include_directories: include_directories('.'),
)

libcommon_dep = declare_dependency(
  link_with: [libcommon, libcommon_core],
  include_directories: include_directories('.'),
  dependencies: [tomlplusplus_dep, gtk4_dep_common],
)

# AudioMixer (audio_mixer.hpp/.cpp) is its own static library, not part of
# libcommon above -- it's the only common/ module that needs libpipewire,
# and libcommon is linked into every fleetwm binary (settings, launcher,
# greeter, ...), most of which have no business pulling in PipeWire. Only
# fleetwm-audiomixer and fleetwm-settings' Audio tab link this. Same
# required:false + FLEETWM_HAVE_PIPEWIRE pattern as src/bar/meson.build's
# read-only VolumeSource -- kept as a second, independent PipeWire
# connection rather than sharing bar's, since the mixer needs write access
# (set volume) and per-app stream enumeration that VolumeSource never
# needed.
libpipewire_dep_mixer = dependency('libpipewire-0.3', required: false)

audiomixer_cpp_args = []
audiomixer_extra_deps = []
if libpipewire_dep_mixer.found()
  audiomixer_extra_deps += libpipewire_dep_mixer
  audiomixer_cpp_args += '-DFLEETWM_HAVE_PIPEWIRE=1'
else
  audiomixer_cpp_args += '-DFLEETWM_HAVE_PIPEWIRE=0'
endif

libaudiomixer = static_library(
  'fleetwm-audiomixer-backend',
  files('audio_mixer.cpp'),
  dependencies: [libcommon_core_dep] + audiomixer_extra_deps,
  cpp_args: audiomixer_cpp_args,
  include_directories: include_directories('.'),
)

libaudiomixer_dep = declare_dependency(
  link_with: libaudiomixer,
  include_directories: include_directories('.'),
  dependencies: [libcommon_core_dep] + audiomixer_extra_deps,
  compile_args: audiomixer_cpp_args,
)
