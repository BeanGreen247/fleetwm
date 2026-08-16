# wlroots_dep, wlroots_cpp_args, patched_wlroots_dir, libinput_dep,
# xkbcommon_dep, pixman_dep, drm_dep, egl_dep, gles2_dep all come from the
# top-level meson.build (shared with src/compositor).

pam_dep = cpp.find_library('pam', required: get_option('greeter'))

executable(
  'fleetwm-greet',
  files(
    'main.cpp',
    'compositor.cpp',
    'login_ipc.cpp',
    'auth.cpp',
    'session.cpp',
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
    pam_dep,
    protocols_dep,
  ],
  install: true,
)
