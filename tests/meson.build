# Pure unit tests for the config-parsing modules in src/common/ -- no
# compositor, Wayland session, or fleetwm-dev VM hardware required, so
# these can run anywhere (including plain `meson test` in a throwaway
# build dir). Intentionally does NOT cover anything that needs a live
# wlroots scene tree, a real GTK4 window, or spawns real system commands
# (systemd/loginctl/g_spawn_async) -- those need the fleetwm-dev VM and
# are out of scope for a unit test.
gtest_dep = dependency('gtest', main: true, required: true, fallback: ['gtest', 'gtest_main_dep'])

fleetwm_test_sources = files(
  'test_theme.cpp',
  'test_wallpaper_config.cpp',
  'test_default_apps.cpp',
  'test_keybinds_config.cpp',
  'test_bar_config.cpp',
  'test_pipewire_json.cpp',
  'test_ipc_client.cpp',
  'test_battery_source.cpp',
)

fleetwm_unit_tests = executable(
  'fleetwm-unit-tests',
  fleetwm_test_sources,
  dependencies: [gtest_dep, libcommon_dep],
)

test('fleetwm-unit-tests', fleetwm_unit_tests, protocol: 'gtest')
