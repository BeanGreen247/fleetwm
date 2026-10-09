# fleetwm-fm: the file manager. A regular xdg_toplevel drawn with cairo through fleetkit; the window itself (src/fleetfm) has no
# Wayland dependency and also renders headless: `fleetwm-fm --screenshot out.png`.
fleetfm_exe = executable(
  'fleetwm-fm',
  files('main.cpp', 'train.cpp'),
  include_directories: include_directories('.'),
  dependencies: [libfleetfm_window_dep, libfleetkit_dep, libcommon_core_dep],
  install: true,
)

# The application icon is drawn in code (src/fleetfm/icons.cpp); `ninja install` has the program write its own PNGs into hicolor.
meson.add_install_script(
  find_program('python3'),
  meson.project_source_root() / 'scripts' / 'install-fm-icons.py',
  fleetfm_exe,
  '48', '64', '128', '256',
)

# Benchmarks (perflog/2026-10-09, docs/FILE_MANAGER_PERFORMANCE.md): meson setup -Dbenchmarks=true. Not installed.
if get_option('benchmarks')
  bench_dir = meson.project_source_root() / 'perflog' / '2026-10-09'
  executable('fleetwm-fm-dirbench', bench_dir / 'dirbench.cpp', dependencies: [libfleetfm_core_dep])
  executable('fleetwm-fm-copybench', bench_dir / 'copybench.cpp', dependencies: [libfleetfm_core_dep])
  executable('fleetwm-fm-uibench', bench_dir / 'uibench.cpp', dependencies: [libfleetfm_window_dep, libfleetkit_dep, libcommon_core_dep])
endif
