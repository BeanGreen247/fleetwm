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

# wlroots detection + the C++-incompatible-header patching it needs -- lives
# here (not in src/compositor/meson.build) because src/greeter's greeter
# compositor (src/greeter/compositor.{hpp,cpp}) also links wlroots directly
# and needs the exact same patched_wlroots_dir/cpp_args, not a second
# divergent copy of this ~40-line block.
wlroots_pkgconfig_name = 'wlroots-0.18'
wlroots_dep = dependency(wlroots_pkgconfig_name, required: false)
if not wlroots_dep.found()
  # Some distros (and older wlroots releases) publish the .pc file under the
  # bare 'wlroots' name instead of version-suffixed. Fall back rather than
  # hard-failing configure, and let the version check below catch anything
  # actually too old.
  wlroots_pkgconfig_name = 'wlroots'
  wlroots_dep = dependency(wlroots_pkgconfig_name)
endif

wlroots_version = wlroots_dep.version()
if wlroots_version.version_compare('<0.17.0')
  error(
    'fleetwm requires wlroots >= 0.17 (targeting the 0.18 series shipped by ' +
    'Debian 13.6 / Ubuntu 26.04); found ' + wlroots_version + '. See ' +
    'docs/adr/0001-wlroots-vs-smithay.md for the version-skew rationale.'
  )
endif

# wlroots-0.18 ships two headers (wlr_scene.h, xwayland/xwayland.h) that use
# C99-only syntax (a `const T x[static N]` array parameter, and a struct
# field literally named `class`) which is a hard parse error under a C++
# compiler and aborts parsing partway through each file. Neither construct
# has ABI significance (see scripts/patch-wlroots-cpp-headers.py), so we
# generate C++-safe copies into the build dir and put that directory ahead
# of the real wlroots include path -- every other header still resolves
# against the genuine system copy.
# wlroots-0.18.pc's own 'includedir' pkg-config variable resolves to
# /usr/include (the common prefix), not /usr/include/wlroots-0.18 where the
# wlr/ tree actually lives -- that version-suffixed path only shows up in
# Cflags, so pull it from there instead of trusting the variable.
wlroots_cflags = run_command(
  'pkg-config', '--cflags-only-I', wlroots_pkgconfig_name, check: true
).stdout().strip()
wlroots_includedir = ''
foreach flag : wlroots_cflags.split(' ')
  if flag.startswith('-I') and flag.contains('wlroots')
    wlroots_includedir = flag.substring(2)
  endif
endforeach
if wlroots_includedir == ''
  error(
    'could not determine the wlroots include directory from ' +
    '`pkg-config --cflags-only-I ' + wlroots_pkgconfig_name + '`'
  )
endif
patched_wlroots_dir = meson.current_build_dir() / 'patched-wlroots-include'
run_command(
  find_program('python3'),
  meson.project_source_root() / 'scripts' / 'patch-wlroots-cpp-headers.py',
  wlroots_includedir,
  patched_wlroots_dir,
  check: true,
)

# Common cpp_args/c_args for anything linking wlroots_dep directly
# (src/compositor, src/greeter's greeter compositor): -I patched_wlroots_dir
# first so the two C++-incompatible headers resolve to the patched copies
# generated above, ahead of the real wlroots-0.18 include path wlroots_dep
# itself contributes; -DWLR_USE_UNSTABLE is required by every wlroots-based
# compositor (wlroots headers gate their still-evolving API behind it).
wlroots_cpp_args = ['-DWLR_USE_UNSTABLE', '-I' + patched_wlroots_dir]

libinput_dep = dependency('libinput')
xkbcommon_dep = dependency('xkbcommon')
pixman_dep = dependency('pixman-1')
drm_dep = dependency('libdrm')
egl_dep = dependency('egl')
gles2_dep = dependency('glesv2')

# ---------------------------------------------------------------------------
# Subprojects: each produces one installable binary
# ---------------------------------------------------------------------------

subdir('src/common')
subdir('src/compositor')
subdir('src/bar')
subdir('src/settings')
subdir('src/launcher')
subdir('src/wallpaper')
subdir('src/powermenu')
if get_option('greeter')
  subdir('src/greeter')
  subdir('src/greeter-login')
  subdir('src/locker')
endif

if get_option('tests')
  subdir('tests')
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
