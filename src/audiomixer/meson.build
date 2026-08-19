# Same gtk4-layer-shell-0 fallback dance as src/powermenu/meson.build.
gtk4_dep_audiomixer = dependency('gtk4')
gtk4_layer_shell_dep_audiomixer = dependency('gtk4-layer-shell-0', required: false)
if not gtk4_layer_shell_dep_audiomixer.found()
  gtk4_layer_shell_dep_audiomixer = dependency('gtk4-layer-shell')
endif

executable(
  'fleetwm-audiomixer',
  files(
    'main.cpp',
    'audio_mixer_window.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep_audiomixer, gtk4_layer_shell_dep_audiomixer, libcommon_dep,
                 libaudiomixer_dep],
  install: true,
)
