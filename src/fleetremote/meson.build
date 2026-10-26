# fleetwm-remote: the built-in remote desktop server (VNC and RDP). Captures the screen with wlr-screencopy and injects
# input through the virtual pointer and keyboard protocols, so it is a normal Wayland client of the compositor.
# Needs zlib (VNC ZRLE), OpenSSL (RDP's TLS, the VNC login's DES) and xkbcommon (keyboard layout).
openssl_dep_fleetremote = dependency('openssl', required: false)
zlib_dep_fleetremote = dependency('zlib', required: false)

if openssl_dep_fleetremote.found() and zlib_dep_fleetremote.found()
  fleetremote_protocols = [
    ['wlr-screencopy-unstable-v1', files('protocols/wlr-screencopy-unstable-v1.xml')],
    ['wlr-virtual-pointer-unstable-v1', files('protocols/wlr-virtual-pointer-unstable-v1.xml')],
    ['virtual-keyboard-unstable-v1', files('protocols/virtual-keyboard-unstable-v1.xml')],
  ]
  fleetremote_generated = []
  foreach p : fleetremote_protocols
    fleetremote_generated += custom_target(
      p[0] + '-fleetremote-client-header',
      input: p[1],
      output: p[0] + '-client-protocol.h',
      command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    fleetremote_generated += custom_target(
      p[0] + '-fleetremote-client-source',
      input: p[1],
      output: p[0] + '-client-protocol.c',
      command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
  endforeach

  executable(
    'fleetwm-remote',
    files('main.cpp', 'hub.cpp', 'capture.cpp', 'vnc.cpp', 'rdp.cpp', 'tls.cpp'),
    fleetremote_generated,
    include_directories: include_directories('.'),
    dependencies: [wayland_client, xkbcommon_dep, openssl_dep_fleetremote, zlib_dep_fleetremote, libcommon_core_dep, dependency('threads')],
    install: true,
  )
endif
