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
  'test_keyboard.cpp',
  'test_app_appearance.cpp',
  'test_settings_pages.cpp',
  'test_power_menu.cpp',
  'test_pgo_training.cpp',
  'test_popups.cpp',
  'test_caption_buttons.cpp',
)

fleetwm_unit_tests = executable(
  'fleetwm-unit-tests',
  fleetwm_test_sources,
  dependencies: [gtest_dep, libcommon_core_dep, libfleetkit_dep, libnetmgr_dep],
  # the power menu's header-only parts, and the source tree for the tests that read the polkit rule and install.sh
  include_directories: include_directories('../apps/powermenu'),
  cpp_args: ['-DFLEETWM_SOURCE_DIR="' + meson.project_source_root() + '"'],
)

test('fleetwm-unit-tests', fleetwm_unit_tests, protocol: 'gtest')

# One meson test per area, so `meson test geometry` runs just that slice (the same
# slices scripts/test.sh offers). The full binary above still runs everything.
test_areas = {
  'theme': 'ParseHexColor.*:ThemeName.*:ThemeTest.*:TitlebarNames.*:WindowLayoutNames.*:ThemeHome.*',
  'bar': 'PowerMode.*:BarLayout.*:TaskbarPosition.*:BarConfigTest.*:BatterySourceReadingTest.*',
  'keybinds': 'KeybindsConfigTest.*',
  'geometry': 'ResizeEdges.*:ResizedBox.*:Cascade.*:TitlebarLayout.*:TitlebarStrip.*:TitlebarHit.*:TitleX.*:TaskbarSlots.*:SnapZoneAt.*:SnapBox.*:CaptionButtons.*',
  'windows': 'WindowList.*',
  'shortcuts': 'FormatAltCombo.*:FormatAltShiftCombo.*:ShortcutList.*',
  'ipc': 'ExtractJsonStringField.*:IpcSocketPathTest.*:IpcClient.*:IpcClientWithServerTest.*',
  'config': 'WallpaperConfigTest.*:DefaultAppsTest.*:OutputConfigTest.*',
  'fleetkit': 'DesktopEntryExec.*:FleetkitColor.*:GlassCache.*',
  'network': 'NetworkParse.*:SystemDevices.*:WpaFixture.*',
  'keyboard': 'KeyboardConfigTest.*:KeyboardNames.*:XkbRules.*:Locales.*',
  'popups': 'PopupSpot.*:SingleInstance.*:AudioInstaller.*:PopupDismissal.*',
  'build': 'PgoTraining.*:PgoTrainingShim.*',
  'power': 'PowerActions.*:PowerPolkitRule.*:PowerInstaller.*:PowerIcons.*:PolkitRules.*:PolkitRuleBehaviour.*',
}
foreach area, filter : test_areas
  test(area, fleetwm_unit_tests, args: ['--gtest_filter=' + filter], suite: 'areas', protocol: 'gtest')
endforeach

# Static check for the installer's unity build: no two files of one directory may define the same
# file-scope name (scripts/check-unity-collisions.py), plus the checker's own self-test.
python3 = find_program('python3', required: false)
if python3.found()
  unity_check = meson.project_source_root() / 'scripts' / 'check-unity-collisions.py'
  test('unity-name-clashes', python3, args: [unity_check, meson.project_source_root()], suite: 'static')
  test('unity-check-self-test', python3, args: [unity_check, '--self-test'], suite: 'static')
endif
