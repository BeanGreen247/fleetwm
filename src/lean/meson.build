# fleetwm "lean" toolkit: GTK-free Wayland client helpers (layer-shell
# surfaces on wl_shm + cairo, xkbcommon input, timers). Links cairo (which
# pulls fontconfig/freetype/pixman) but never GTK, GLib or Pango.
cairo_dep = dependency('cairo')
xkbcommon_client_dep = dependency('xkbcommon')

# Image decoding (PNG/JPEG/WebP via libpng/libjpeg/libwebp, SVG via vendored
# nanosvg) -- no GTK, GdkPixbuf or librsvg. Shared by the wallpaper (which
# does not need cairo) and the icon loader.
libpng_dep = dependency('libpng')
libjpeg_dep = dependency('libjpeg')
libwebp_dep = dependency('libwebp')

libleanimage = static_library(
  'fleetwm-lean-image',
  files('image.cpp', 'svg.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libpng_dep, libjpeg_dep, libwebp_dep],
)

libleanimage_dep = declare_dependency(
  link_with: libleanimage,
  include_directories: include_directories('.'),
  dependencies: [libpng_dep, libjpeg_dep, libwebp_dep],
)

liblean = static_library(
  'fleetwm-lean',
  files('lean.cpp', 'icon_theme.cpp', 'desktop_entry.cpp'),
  include_directories: include_directories('.'),
  dependencies: [wayland_client, client_protocols_dep, cairo_dep, xkbcommon_client_dep, libcommon_core_dep, libleanimage_dep],
)

liblean_dep = declare_dependency(
  link_with: liblean,
  include_directories: include_directories('.'),
  dependencies: [wayland_client, client_protocols_dep, cairo_dep, xkbcommon_client_dep, libcommon_core_dep, libleanimage_dep],
)
