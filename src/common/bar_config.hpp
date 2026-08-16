#pragma once

#include <string>

namespace fleetwm {

// Bar-only preferences, kept in a separate file from theme.toml (which
// covers window/compositor theming -- accent color and corner_style are
// still read from there and reused as-is for the bar's border/shape, per
// explicit user choice not to duplicate them here). This file is purely
// for settings that only ever mean something to fleetwm-bar, starting
// with clock display toggles.
struct ClockFormat {
  bool show_seconds = true;
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
  std::string inactive_bg = "#3c3c3c";
  std::string inactive_fg = "#ffffff";
  std::string active_bg = "#ff7800";
  std::string active_fg = "#000000";
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

// Full: existing behavior -- anchored to both screen edges, spans the
// whole output width (rectangle or full-width pill depending on
// corner_style). Island: detached from the left/right edges, floating
// horizontally centered with a small gap from the top edge -- the same
// visual (still governed by corner_style/theme) just narrower and not
// edge-to-edge. Only offered/applied on outputs >= kIslandMinOutputWidthPx
// wide (bar_window.cpp) -- a floating pill on a narrow/small display has
// no room to breathe and just looks cramped, per explicit user request to
// gate it on screen width.
enum class BarMode {
  Full,
  Island,
};

// Minimum output width (px) island mode is offered/applied at -- explicit
// user-specified threshold, matching common 1366x768 laptop panels as the
// cutoff.
constexpr int kIslandMinOutputWidthPx = 1366;

struct BarConfig {
  ClockFormat clock;
  WorkspaceColors workspace_colors;
  PowerMode power_mode = PowerMode::Normal;
  BarMode bar_mode = BarMode::Full;
};

std::string bar_mode_to_string(BarMode mode);
BarMode bar_mode_from_string(const std::string& s);

std::string power_mode_to_string(PowerMode mode);
PowerMode power_mode_from_string(const std::string& s);

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
