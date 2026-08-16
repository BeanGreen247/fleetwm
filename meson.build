project(
  'fleetwm',
  'c', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++20',
    'c_std=c11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

cpp = meson.get_compiler('cpp')

# ---------------------------------------------------------------------------
# Shared dependency lookups (used by more than one subproject)
# ---------------------------------------------------------------------------

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_protocols_dep = dependency('wayland-protocols')
wayland_scanner_dep = dependency('wayland-scanner', native: true)

wayland_scanner = find_program(
  wayland_scanner_dep.get_variable(pkgconfig: 'wayland_scanner'),
  native: true,
)
wl_protocol_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

subdir('protocols')

# ---------------------------------------------------------------------------
# Subprojects: each produces one installable binary
# ---------------------------------------------------------------------------

subdir('src/common')
subdir('src/compositor')
subdir('src/bar')
subdir('src/settings')
subdir('src/launcher')
subdir('src/wallpaper')
if get_option('greeter')
  subdir('src/greeter')
endif

install_data(
  'packaging/fleetwm.desktop',
  install_dir: get_option('sysconfdir') / '..' / 'share' / 'wayland-sessions',
)

# fleetwm-settings only -- fleetwm-bar/fleetwm-wallpaper are background
# renderers with no standalone user-facing action (same reason
# fleetwm-launcher itself has no .desktop entry: GDesktopAppInfo only
# enumerates apps that have one at all, so simply not installing an
# entry for those is what keeps them out of fleetwm-launcher's own
# search results, no special-casing needed in app_index.cpp).
install_data(
  'packaging/fleetwm-settings.desktop',
  install_dir: get_option('prefix') / 'share' / 'applications',
)

install_data(
  'packaging/fleetwm-update',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
  rename: 'fleetwm-update',
)

install_subdir(
  'themes',
  install_dir: get_option('sysconfdir') / 'fleetwm',
  strip_directory: false,
)

install_data(
  'config/theme.toml',
  install_dir: get_option('sysconfdir') / 'fleetwm',
)
