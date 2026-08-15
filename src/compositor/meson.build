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

libinput_dep = dependency('libinput')
xkbcommon_dep = dependency('xkbcommon')
pixman_dep = dependency('pixman-1')
drm_dep = dependency('libdrm')
egl_dep = dependency('egl')
gles2_dep = dependency('glesv2')

xwayland_enabled = get_option('xwayland')
xcb_dep = []
if xwayland_enabled
  xcb_dep = dependency('xcb', required: false)
  if not xcb_dep.found()
    warning('xwayland option is enabled but libxcb-dev was not found; disabling XWayland support')
    xwayland_enabled = false
  endif
endif

compositor_conf = configuration_data()
compositor_conf.set10('FLEETWM_XWAYLAND', xwayland_enabled)
configure_file(output: 'config.h', configuration: compositor_conf)

executable(
  'fleetwm',
  files(
    'main.cpp',
    'server.cpp',
    'output.cpp',
    'view.cpp',
    'input.cpp',
    'workspace.cpp',
    'ipc_server.cpp',
  ),
  include_directories: include_directories('.'),
  # wlroots headers gate their (still-evolving) API behind this define and
  # #error out otherwise -- required by every wlroots-based compositor.
  # -I patched_wlroots_dir comes first so the two C++-incompatible headers
  # resolve to the patched copies generated above, ahead of the real
  # wlroots-0.18 include path that wlroots_dep itself contributes.
  cpp_args: ['-DWLR_USE_UNSTABLE', '-I' + patched_wlroots_dir],
  c_args: ['-DWLR_USE_UNSTABLE', '-I' + patched_wlroots_dir],
  dependencies: [
    wayland_server,
    wlroots_dep,
    libinput_dep,
    xkbcommon_dep,
    pixman_dep,
    drm_dep,
    egl_dep,
    gles2_dep,
    xcb_dep,
    protocols_dep,
    libcommon_dep,
  ],
  install: true,
)
