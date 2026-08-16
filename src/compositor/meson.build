# wlroots_dep, wlroots_cpp_args, patched_wlroots_dir come from the
# top-level meson.build (shared with src/greeter's greeter compositor).

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
    'layer_surface.cpp',
    'input.cpp',
    'workspace.cpp',
    'ipc_server.cpp',
  ),
  include_directories: include_directories('.'),
  cpp_args: wlroots_cpp_args,
  c_args: wlroots_cpp_args,
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
