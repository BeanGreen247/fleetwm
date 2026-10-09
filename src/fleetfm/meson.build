# fleetfm core: everything the file manager does that needs no window -- directory reading, sorting, checksums, the
# verified copy engine, mounts and safe removal, network places, settings, view styles. Pure C++ on POSIX plus libcrypto
# (checksums); no GTK, no GLib, no GVfs. The window (apps/fleetfm) and the unit tests link this.
libcrypto_dep = dependency('libcrypto')

libfleetfm_core = static_library(
  'fleetfm-core',
  files(
    'format.cpp',
    'natural_sort.cpp',
    'hasher.cpp',
    'dir_listing.cpp',
    'speed_meter.cpp',
    'transfer.cpp',
    'process.cpp',
    'mounts.cpp',
    'locations.cpp',
    'view_style.cpp',
    'fm_settings.cpp',
    'trash.cpp',
    'search.cpp',
    'view_metrics.cpp',
    'browser.cpp',
    'grouping.cpp',
    'line_edit.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [libcrypto_dep, libcommon_core_dep],
  cpp_args: ['-D_GNU_SOURCE'],
)

libfleetfm_core_dep = declare_dependency(
  link_with: libfleetfm_core,
  include_directories: include_directories('.'),
  dependencies: [libcrypto_dep, libcommon_core_dep],
)

# The cairo side: drawn icons. Split from the core so the transfer / mount / settings code links without cairo.
cairo_for_fm = dependency('cairo')
libfleetfm_icons = static_library(
  'fleetfm-icons',
  files('icons.cpp', 'file_ops.cpp', 'nav_tree.cpp', 'desktop.cpp'),
  include_directories: include_directories('.'),
  dependencies: [cairo_for_fm, libfleetfm_core_dep],
)
libfleetfm_icons_dep = declare_dependency(
  link_with: libfleetfm_icons,
  include_directories: include_directories('.'),
  dependencies: [cairo_for_fm, libfleetfm_core_dep],
)

# The settings widgets (file manager and desktop), used by the Settings app and by the file manager's fallback Folder Options window.
libfleetfm_settings_ui = static_library(
  'fleetfm-settings-ui',
  files('fm_settings_ui.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libfleetfm_icons_dep, libfleetfm_core_dep],
)
libfleetfm_settings_ui_dep = declare_dependency(
  link_with: libfleetfm_settings_ui,
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libfleetfm_icons_dep, libfleetfm_core_dep],
)


# The window: tabs, panes, views, dialogs, transfers. Depends on the fleetkit toolkit (cairo text, the immediate-mode widgets used
# in the dialogs) but not on Wayland itself, so it renders headless for screenshots and tests.
libfleetfm_window = static_library(
  'fleetfm-window',
  files(
    'ui_colors.cpp',
    'window.cpp',
    'window_draw.cpp',
    'window_overlay.cpp',
    'window_input.cpp',
    'window_ops.cpp',
    'window_extra.cpp',
    'window_dialogs.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libfleetfm_icons_dep, libfleetfm_core_dep, libfleetfm_settings_ui_dep],
  cpp_args: ['-D_GNU_SOURCE'],
)
libfleetfm_window_dep = declare_dependency(
  link_with: libfleetfm_window,
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libfleetfm_icons_dep, libfleetfm_core_dep],
)
