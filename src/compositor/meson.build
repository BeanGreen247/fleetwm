wlroots_dep = dependency('wlroots-0.18', required: false)
if not wlroots_dep.found()
  # Some distros (and older wlroots releases) publish the .pc file under the
  # bare 'wlroots' name instead of version-suffixed. Fall back rather than
  # hard-failing configure, and let the version check below catch anything
  # actually too old.
  wlroots_dep = dependency('wlroots')
endif

wlroots_version = wlroots_dep.version()
if wlroots_version.version_compare('<0.17.0')
  error(
    'fleetwm requires wlroots >= 0.17 (targeting the 0.18 series shipped by ' +
    'Debian 13.6 / Ubuntu 26.04); found ' + wlroots_version + '. See ' +
    'docs/adr/0001-wlroots-vs-smithay.md for the version-skew rationale.'
  )
endif

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
