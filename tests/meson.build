# Pure unit tests for the config-parsing modules in src/common/ -- no
# compositor, Wayland session, or fleetwm-dev VM hardware required, so
# these can run anywhere (including plain `meson test` in a throwaway
# build dir). Intentionally does NOT cover anything that needs a live
# wlroots scene tree, a real window, or spawns real system commands
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
  'test_fleetkit.cpp',
  'test_output_config.cpp',
  'test_window_geometry.cpp',
  'test_window_list.cpp',
  'test_shortcut_list.cpp',
  'test_version.cpp',
  'test_terminal_launch.cpp',
  'test_workspace_list.cpp',
  'test_power_config.cpp',
  'test_lock_tooltip.cpp',
  'test_network.cpp',
  'test_app_appearance.cpp',
  'test_settings_pages.cpp',
)

fleetwm_unit_tests = executable(
  'fleetwm-unit-tests',
  fleetwm_test_sources,
  dependencies: [gtest_dep, libcommon_core_dep, libfleetkit_dep, libnetmgr_dep],
)

test('fleetwm-unit-tests', fleetwm_unit_tests, protocol: 'gtest')

# One meson test per area, so `meson test geometry` runs just that slice (the same
# slices scripts/test.sh offers). The full binary above still runs everything.
test_areas = {
  'theme': 'ParseHexColor.*:ThemeName.*:ThemeTest.*:TitlebarNames.*:WindowLayoutNames.*:ThemeHome.*',
  'bar': 'PowerMode.*:BarLayout.*:TaskbarPosition.*:BarConfigTest.*:BatterySourceReadingTest.*',
  'keybinds': 'KeybindsConfigTest.*',
  'geometry': 'ResizeEdges.*:ResizedBox.*:Cascade.*:TitlebarLayout.*:TitlebarHit.*:TitleX.*:TaskbarSlots.*:SnapZoneAt.*:SnapBox.*',
  'windows': 'WindowList.*',
  'shortcuts': 'FormatAltCombo.*:FormatAltShiftCombo.*:ShortcutList.*',
  'ipc': 'ExtractJsonStringField.*:IpcSocketPathTest.*:IpcClient.*:IpcClientWithServerTest.*',
  'config': 'WallpaperConfigTest.*:DefaultAppsTest.*:OutputConfigTest.*',
  'fleetkit': 'DesktopEntryExec.*:FleetkitColor.*',
  'network': 'NetworkParse.*:SystemDevices.*:WpaFixture.*',
}
foreach area, filter : test_areas
  test(area, fleetwm_unit_tests, args: ['--gtest_filter=' + filter], suite: 'areas', protocol: 'gtest')
endforeach

