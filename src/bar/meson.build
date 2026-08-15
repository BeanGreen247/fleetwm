gtk4_dep = dependency('gtk4')
gtk4_layer_shell_dep = dependency('gtk4-layer-shell-0', required: false)
if not gtk4_layer_shell_dep.found()
  gtk4_layer_shell_dep = dependency('gtk4-layer-shell')
endif

# workspace_widget.cpp / clock_widget.cpp / stats_widget.cpp / power_icon.cpp
# land in Phase 3 (see docs/adr and the project plan roadmap). main.cpp is a
# placeholder stub until then so the tree builds end-to-end.
executable(
  'fleetwm-bar',
  files(
    'main.cpp',
  ),
  include_directories: include_directories('.'),
  dependencies: [gtk4_dep, gtk4_layer_shell_dep, libcommon_dep],
  install: true,
)
