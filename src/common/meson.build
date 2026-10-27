# Configuration files are read and written by fleettoml.cpp (our own small TOML reader/writer): no external TOML library, no download.

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

# libcommon_core: everything that needs no GUI toolkit -- config parsing, paths,
# IPC client, malloc tuning. GTK-free so fleetkit (non-GTK) clients such as
# fleetwm-wallpaper link it without pulling GTK/GLib/Pango into the process.
# The git revision, regenerated on every build so About always shows what is installed.
version_h = vcs_tag(
  command: ['git', '-C', meson.project_source_root(), 'describe', '--always', '--dirty', '--tags'],
  fallback: 'unknown',
  input: 'version_config.h.in',
  output: 'version_config.h',
)

libcommon_core = static_library(
  'fleetwm-common-core',
  files(
    'config_paths.cpp',
    'fleettoml.cpp',
    'theme.cpp',
    'ipc_client.cpp',
    'bar_config.cpp',
    'taskbar_layout.cpp',
    'ctx_menu.cpp',
    'taskbar_layout.cpp',
    'wallpaper_config.cpp',
    'output_config.cpp',
    'default_apps.cpp',
    'keybinds_config.cpp',
    'battery_reading.cpp',
    'hw_stats.cpp',
    'osd_logic.cpp',
    'gpu_monitor.cpp',
    'proc_stats.cpp',
    'prewarm.cpp',
    'malloc_tuning.cpp',
    'pipewire_json.cpp',
    'window_geometry.cpp',
    'window_list.cpp',
    'shortcut_list.cpp',
    'power_config.cpp',
    'remote_config.cpp',
    'sessions.cpp',
    'settings_catalog.cpp',
    'remote_usage.cpp',
    'keyboard_config.cpp',
    'mouse_config.cpp',
    'game_config.cpp',
    'tuxthrottle.cpp',
    'autostart.cpp',
    'gpu_policy.cpp',
    'user_profile.cpp',
    'xkb_rules.cpp',
    'lock_tooltip.cpp',
    'app_appearance.cpp',
    'spectrum.cpp',
    'config_backup.cpp',
  ),
  include_directories: include_directories('.'),
  sources: [version_h],
)

libcommon_core_dep = declare_dependency(
  link_with: libcommon_core,
  include_directories: include_directories('.'),
  sources: [version_h],
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
