# power_menu.cpp / accent_extract.cpp land later in Phase 6 (see docs/adr
# and the project plan roadmap). settings_window.cpp is the first real
# piece: a GTK4 front end over src/common/theme.{hpp,cpp}'s already-
# complete load/save logic.
#
# gio-unix-2.0 (not plain gio-2.0) is needed for gio/gdesktopappinfo.h --
# same rationale as launcher/meson.build's own gio_dep: GDesktopAppInfo is
# Unix-specific and Debian splits it out of the base gio-2.0 pkg-config
# file. Used by the Default Apps tab's terminal-emulator filtering.
gio_dep_settings = dependency('gio-unix-2.0')

executable(
  'fleetwm-settings',
  files(
    'main.cpp',
    'settings_window.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep, libcommon_dep, gio_dep_settings, libaudiomixer_dep],
  install: true,
)
