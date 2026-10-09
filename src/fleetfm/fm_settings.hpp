#pragma once

#include <string>
#include <vector>

#include "dir_listing.hpp"
#include "format.hpp"
#include "hasher.hpp"
#include "transfer.hpp"
#include "view_style.hpp"

// Every option of the file manager, saved in ~/.config/fleetwm/fleetfm.toml and changed live from Settings. It covers all
// of Windows' Folder Options (General, View, Search) and Windows 10's Explorer options, plus what a Linux file manager
// offers (tabs, type-ahead, open with, trash policy) and what is specific here (transfer verification, safe removal).

namespace fleetwm::fm {

enum class OpenIn { SameWindow, NewWindow, NewTab };
enum class ClickMode { Double, Single };
enum class Startup { ThisPc, Home, LastLocation, Custom };
enum class NewTabAt { Home, CurrentFolder, ThisPc };
enum class MenuBar { Never, Alt, Always };  // "Always show menus"
enum class TypeAhead { Select, Search };
enum class DeleteMode { Trash, Permanent, Ask };
enum class VerifyWhen { Always, ExternalOnly, Never };
enum class ColorScheme { StyleDefault, FollowTheme, Light, Dark };
enum class SizeFormat { Kilobytes, Exact, Auto };

struct ColumnSetting {
  std::string id;  // "name", "modified", "type", "size", "permissions", "path"
  int width = 0;
  bool visible = true;
  bool operator==(const ColumnSetting&) const = default;
};

struct FmSettings {
  // ---- General (Folder Options -> General, Windows 10 General) ----
  ViewStyle style = ViewStyle::Windows7;
  OpenIn open_folders_in = OpenIn::SameWindow;
  ClickMode click_mode = ClickMode::Double;
  bool show_navigation_pane = true;
  bool nav_show_all_folders = false;
  bool nav_expand_to_current = false;
  bool nav_show_libraries = true, nav_show_favorites = true, nav_show_network = true, nav_show_trash = true;
  Startup startup = Startup::ThisPc;
  std::string startup_path;                         // for Startup::Custom
  std::string last_location;                        // for Startup::LastLocation (written by the window)
  bool show_recent_files = true, show_frequent_folders = true;
  bool middle_click_opens_tab = true;
  // ---- Tabs ----
  bool always_show_tabs = false;                    // hide the strip with a single tab
  NewTabAt new_tab_at = NewTabAt::Home;
  bool close_window_with_last_tab = true;
  bool restore_tabs = false;
  std::vector<std::string> open_tabs;               // written by the window when restore_tabs is on
  // ---- View (Folder Options -> View, Advanced settings) ----
  ViewMode default_view = ViewMode::Details;
  bool style_sets_view = true;                      // a style change also switches the view mode
  bool show_hidden = false;
  bool show_extensions = true;                      // off = "Hide extensions for known file types"
  bool show_full_path_in_title = false;
  bool show_free_space_bars = true;
  bool show_status_bar = false;                     // Windows 7 has the details pane instead
  bool show_details_pane = true;
  bool show_preview_pane = false;
  MenuBar menu_bar = MenuBar::Alt;
  bool use_checkboxes = false;
  bool row_stripes = false;
  bool compact_rows = false;
  TypeAhead type_ahead = TypeAhead::Select;
  bool show_thumbnails = true;                      // false = "Always show icons, never thumbnails"
  int thumbnail_max_mb = 16;
  bool remember_folder_views = true;                // each folder keeps its own view mode and sort
  // ---- Sorting and grouping ----
  SortKey sort_key = SortKey::Name;
  bool sort_ascending = true;
  bool folders_first = true;
  GroupBy group_by = GroupBy::None;
  SizeFormat size_format = SizeFormat::Kilobytes;
  DateStyle date_style = DateStyle::Windows7;
  std::vector<ColumnSetting> columns;               // empty = the defaults
  // ---- Appearance ----
  ColorScheme colour_scheme = ColorScheme::StyleDefault;
  int font_px = 0;                                  // 0 = the style's
  int row_height = 0;                               // 0 = the style's
  int icon_px = 0;                                  // 0 = the view mode's
  int nav_width = 0;                                // 0 = the style's; saved when the splitter is dragged
  int window_w = 1000, window_h = 640;
  // ---- Search (Folder Options -> Search) ----
  bool search_subfolders = true;
  bool search_partial = true;
  bool search_hidden = false;
  bool search_in_contents = false;
  bool search_case_sensitive = false;
  int search_max_results = 5000;
  // ---- Copying and moving ----
  VerifyWhen verify = VerifyWhen::Always;
  HashAlgo verify_algo = HashAlgo::Auto;
  SyncMode sync = SyncMode::Auto;
  int block_kib = 1024;
  bool direct_verify = true;
  bool follow_symlinks = false;
  bool show_transfer_details = false;               // the speed graph and file list ("More details")
  bool confirm_conflicts = true;                    // false = keep both silently
  DeleteMode delete_mode = DeleteMode::Trash;
  bool confirm_delete = true;
  // ---- Drives ----
  bool show_unmounted_removable = true;
  bool open_after_mount = true;
  bool power_off_after_eject = true;
  bool show_network_in_this_pc = true;
  // ---- Privacy ----
  bool remember_history = true;
  int history_size = 200;

  bool operator==(const FmSettings&) const = default;
};

std::string fm_settings_path();
FmSettings load_fm_settings();
void save_fm_settings(const FmSettings& s);  // throws std::runtime_error on I/O failure
// Default columns for the details view.
std::vector<ColumnSetting> default_columns();
// A style change: sets the style and, when `style_sets_view`, the matching view mode and clears size overrides.
void apply_style(FmSettings* s, ViewStyle style);
// Whether this copy or move gets a verification pass, given where it goes.
bool should_verify(const FmSettings& s, bool destination_is_external);
TransferOptions transfer_options(const FmSettings& s, bool destination_is_external, bool move);

}  // namespace fleetwm::fm
