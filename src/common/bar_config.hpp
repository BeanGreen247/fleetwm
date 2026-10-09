#pragma once

#include <string>
#include <vector>

namespace fleetwm {

// Bar-only preferences, kept in a separate file from theme.toml (which
// covers window/compositor theming -- accent color and corner_style are
// still read from there and reused as-is for the bar's border/shape, per
// explicit user choice not to duplicate them here). This file is purely
// for settings that only ever mean something to fleetwm-bar, starting
// with clock display toggles.
struct ClockFormat {
  bool use_24h = true;       // false = 12-hour clock with AM/PM
  // IANA zone name for the bar clock ("Europe/Prague"). Empty = follow the
  // system time zone. fleetwm-settings sets the system zone when it is
  // allowed to and keeps this override when it is not.
  std::string timezone;
  bool show_seconds = false;  // off by default: one redraw a minute instead of one a second
  bool show_date = false;
  bool show_year = true;
  bool show_month = true;
  bool show_day = true;
};

// Workspace-switcher button colors -- kept in bar.toml (not theme.toml)
// per the same "bar-only setting" rationale as ClockFormat: these only
// ever mean something to fleetwm-bar's workspace buttons, distinct from
// the compositor's own window/focus-border theming.
struct WorkspaceColors {
  // Empty = follow the theme: inactive buttons are transparent with the
  // secondary text colour, the active one is a pill in the accent colour.
  // Any "#rrggbb" or "#rrggbbaa" value overrides.
  std::string inactive_bg;
  std::string inactive_fg;
  std::string active_bg;
  std::string active_fg;
  // Independent of theme.toml's own corner_style (which governs window/
  // launcher/panel corners) -- explicit user request to be able to pick
  // a rectangle workspace-switcher even while windows stay rounded, or
  // vice versa, rather than the two being tied together.
  bool buttons_rounded = true;
};

// Power profile the user has selected in Settings (only surfaced there
// when a battery is detected -- see SettingsWindow). Mirrors
// power-profiles-daemon's own three profiles 1:1 so applying a change is
// just "powerprofilesctl set <name>"; kept in bar.toml (not theme.toml)
// since the bar is what renders the corresponding mode icon next to the
// battery indicator.
enum class PowerMode {
  Normal,       // power-profiles-daemon "balanced"
  Performance,  // power-profiles-daemon "performance"
  BatterySaver,  // power-profiles-daemon "power-saver"
};

// Where the bar's power-mode gauge needle sits (0 = idle .. 1 = pegged in the red zone).
inline double power_mode_gauge(PowerMode m) {
  return m == PowerMode::Performance ? 1.0 : m == PowerMode::BatterySaver ? 0.12 : 0.5;
}

// Full: the existing edge-to-edge bar, anchored to both the left and
// right screen edges (default, unchanged behavior).
// Island: a smaller floating pill detached from the screen edges with a
// top gap, sized to its own content's natural width and capped at the
// monitor's width so it can never grow past the screen -- see
// BarWindow::apply_layout() for the actual sizing. Only ever applied on
// displays at least kIslandMinMonitorWidthPx wide (bar_window.cpp);
// falls back to Full at runtime on anything narrower even if this is
// set, so a bar.toml written on a wide display doesn't leave a
// too-narrow display with an unusable sliver bar.
enum class BarLayout {
  Full,      // classic edge-to-edge strip
  Island,    // one floating pill, centred (needs a >= 1366 px output)
  Capsules,  // three floating pills: workspaces | clock | status (default)
};

// Which screen edge the Desktop-layout taskbar sits on (the Tiling layout keeps
// the top bar styles above). Left and Right make a vertical taskbar.
enum class TaskbarPosition {
  Bottom,  // default
  Top,
  Left,
  Right,
};

// Thickness of the Desktop-layout taskbar in px: its height when horizontal and
// its width when vertical. Shared by the bar (which draws it) and the start menu
// (which places itself beside it).
constexpr int kTaskbarThickness = 44;
constexpr int kTaskbarWidth = 76;

// The Tiling-layout bar: its height and, for the Capsules and Island styles, the gap above it and beside it.
// Shared by the bar and by programs that open a popup next to it (the volume mixer).
constexpr int kBarHeight = 30;
constexpr int kCapsuleTopMargin = 6, kCapsuleSideMargin = 8;
constexpr int kIslandTopMargin = 5, kIslandSideInset = 8;

struct BarConfig {
  ClockFormat clock;
  WorkspaceColors workspace_colors;
  PowerMode power_mode = PowerMode::Normal;
  BarLayout layout = BarLayout::Capsules;
  TaskbarPosition taskbar_position = TaskbarPosition::Bottom;
  // The workspaces always shown (1-10, default 4). More appear while they hold windows or
  // while you are on them, in numerical order, and go away when empty.
  int taskbar_workspaces = 4;
  // Desktop taskbar: rounded window buttons. Sharp corners in theme.toml still win.
  bool taskbar_rounded = true;
  // The Desktop taskbar's elements: the saved order and the hidden ones, by name (see taskbar_layout.hpp: unknown names are dropped on use,
  // missing ones are added, so an older file keeps working). Empty order = the default one.
  std::vector<std::string> taskbar_order;
  // CPU, RAM, GPU and disk are off by default: the Task Manager (Ctrl+Shift+Esc, or right-click the taskbar) shows them, and the bar costs less.
  // A file that lists `taskbar_hidden = []` shows them again.
  std::vector<std::string> taskbar_hidden = {"metrics"};
  bool taskbar_autohide = false;   // slides away until the pointer touches its screen edge
  bool start_centered = false;     // Windows 11 style: start button, pinned apps and window buttons centred
  bool taskbar_labels = true;      // window buttons show the title beside the icon; off: icon only
  std::vector<std::string> pinned_apps;  // desktop entry ids ("firefox.desktop") with an icon button on the taskbar
};

std::string power_mode_to_string(PowerMode mode);
PowerMode power_mode_from_string(const std::string& s);

std::string taskbar_position_to_string(TaskbarPosition position);
TaskbarPosition taskbar_position_from_string(const std::string& s);

std::string bar_layout_to_string(BarLayout layout);
BarLayout bar_layout_from_string(const std::string& s);

// Name powerprofilesctl itself expects ("balanced" | "performance" |
// "power-saver"), distinct from the bar.toml string above.
std::string power_mode_to_profiles_daemon_name(PowerMode mode);

// Path helpers, mirroring theme.hpp's user_config_path()/
// system_default_config_path() but for bar.toml instead of theme.toml.
std::string bar_user_config_path();
std::string bar_system_default_config_path();

// Loads bar.toml. Returns BarConfig{} defaults if no config file exists
// anywhere yet, same fresh-install contract as load_theme_config().
BarConfig load_bar_config();

// Writes `config` to the user config path, creating parent directories as
// needed. Throws std::runtime_error on I/O failure.
void save_bar_config(const BarConfig& config);

}  // namespace fleetwm
