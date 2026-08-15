# Wayland protocol XML -> generated C headers/sources, following the
# standard wlroots/Sway/Wayfire idiom for Meson-based protocol codegen.
#
# wlr-layer-shell-unstable-v1 is vendored here (not in wayland-protocols
# upstream — it's a wlroots-maintained protocol) since the bar and
# settings-app popovers are layer-shell clients.

wl_protocol_dir_local = meson.current_source_dir()

protocols = [
  # [name, xml relative path]
  ['xdg-shell', wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml'],
  ['wlr-layer-shell-unstable-v1', wl_protocol_dir_local / 'wlr-layer-shell-unstable-v1.xml'],
  ['xdg-output-unstable-v1', wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
  ['pointer-constraints-unstable-v1', wl_protocol_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  ['ext-session-lock-v1', wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml'],
]

protocol_headers = []
protocol_sources = []

foreach p : protocols
  name = p[0]
  xml = p[1]

  protocol_headers += custom_target(
    name + '-protocol-header',
    input: xml,
    output: name + '-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )

  protocol_sources += custom_target(
    name + '-protocol-source',
    input: xml,
    output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

protocols_lib = static_library(
  'fleetwm-protocols',
  protocol_sources,
  dependencies: [wayland_server],
)

protocols_dep = declare_dependency(
  link_with: protocols_lib,
  sources: protocol_headers,
)
