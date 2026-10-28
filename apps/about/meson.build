# About Fleetwm (the winver of this desktop): exact version, build, runtime versions, project and license links.
executable(
  'fleetwm-about',
  files('main.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetkit_dep, libcommon_core_dep],
  install: true,
)

# `winver`, as on Windows.
install_symlink('winver', install_dir: get_option('bindir'), pointing_to: 'fleetwm-about')
