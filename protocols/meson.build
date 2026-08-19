# Wayland protocol XML -> generated C headers/sources, following the
# standard wlroots/Sway/Wayfire idiom for Meson-based protocol codegen.
#
# wlr-layer-shell-unstable-v1 is vendored here (not in wayland-protocols
# upstream — it's a wlroots-maintained protocol). wlroots' own installed
# wlr_layer_shell_v1.h unconditionally #includes
# "wlr-layer-shell-unstable-v1-protocol.h", which our -Iprotocols include
# path resolves to this generated header -- so it must exist and be
# discoverable even though libwlroots itself links its own copy of the
# protocol *implementation* internally (this generated copy only supplies
# the type/constant declarations wlr_layer_shell_v1.h needs).
#
# wayland-scanner's generated header for this specific protocol declares a
# `const char *namespace` parameter (from the XML's <arg name="namespace">)
# -- valid C, a hard parse error under a C++ compiler. Patched below via
# scripts/patch-layer-shell-namespace.py, run as a second custom_target
# chained on the first, same C-vs-C++ workaround already used for
# wlroots' own headers in scripts/patch-wlroots-cpp-headers.py.

wl_protocol_dir_local = meson.current_source_dir()

protocols = [
  # [name, xml relative path]
  ['xdg-shell', wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml'],
  ['wlr-layer-shell-unstable-v1', wl_protocol_dir_local / 'wlr-layer-shell-unstable-v1.xml'],
  ['xdg-output-unstable-v1', wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
  ['pointer-constraints-unstable-v1', wl_protocol_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  ['ext-session-lock-v1', wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml'],
  ['tearing-control-v1', wl_protocol_dir / 'staging/tearing-control/tearing-control-v1.xml'],
]

protocol_headers = []
protocol_sources = []

foreach p : protocols
  name = p[0]
  xml = p[1]

  if name == 'wlr-layer-shell-unstable-v1'
    # Generate to a temp name, then patch `namespace` (a C++ keyword used
    # as a plain identifier by this protocol's `namespace` argument) to
    # `namespace_` before it lands at the real output path -- see the
    # rationale comment above this protocols array.
    raw_header = custom_target(
      name + '-protocol-header-raw',
      input: xml,
      output: name + '-protocol-raw.h',
      command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )
    protocol_headers += custom_target(
      name + '-protocol-header',
      input: raw_header,
      output: name + '-protocol.h',
      command: [
        find_program('python3'),
        meson.project_source_root() / 'scripts' / 'patch-layer-shell-namespace.py',
        '@INPUT@', '@OUTPUT@',
      ],
    )
  else
    protocol_headers += custom_target(
      name + '-protocol-header',
      input: xml,
      output: name + '-protocol.h',
      command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )
  endif

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
