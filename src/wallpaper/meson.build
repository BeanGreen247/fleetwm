# GTK-free: only wayland-client + the image codecs + libcommon_core (toml++
# config parsing). No GTK/GLib/Pango/Cairo is linked or mapped.
executable(
  'fleetwm-wallpaper',
  files(
    'main.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [
    wayland_client,
    client_protocols_dep,
    libleanimage_dep,
    libcommon_core_dep,
  ],
  install: true,
)
