#include "fm_settings.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {

template <class E, size_t N>
struct Names {
  const char* v[N];
  const char* to(E e) const { return v[static_cast<size_t>(e)]; }
  E from(const std::string& s, E fallback) const {
    for (size_t i = 0; i < N; ++i)
      if (s == v[i]) return static_cast<E>(i);
    return fallback;
  }
};

constexpr Names<OpenIn, 3> kOpenIn{{"same_window", "new_window", "new_tab"}};
constexpr Names<ClickMode, 2> kClick{{"double", "single"}};
constexpr Names<Startup, 4> kStartup{{"this_pc", "home", "last_location", "custom"}};
constexpr Names<NewTabAt, 3> kNewTab{{"home", "current_folder", "this_pc"}};
constexpr Names<MenuBar, 3> kMenu{{"never", "alt", "always"}};
constexpr Names<TypeAhead, 2> kType{{"select", "search"}};
constexpr Names<DeleteMode, 3> kDelete{{"trash", "permanent", "ask"}};
constexpr Names<VerifyWhen, 3> kVerify{{"always", "external_only", "never"}};
constexpr Names<ColorScheme, 4> kScheme{{"style_default", "follow_theme", "light", "dark"}};
constexpr Names<GroupBy, 5> kGroup{{"none", "name", "type", "size", "modified"}};
constexpr Names<SizeFormat, 3> kSize{{"kilobytes", "exact", "auto"}};
constexpr Names<SortKey, 4> kSort{{"name", "size", "modified", "type"}};
constexpr Names<DateStyle, 3> kDate{{"iso", "windows7", "european"}};
constexpr Names<SyncMode, 3> kSync{{"never", "auto", "always"}};

template <class T>
void get(const toml::table& t, const char* section, const char* key, T* out) {
  if (auto v = t[section][key].template value<T>()) *out = *v;
}
template <class E, size_t N>
void get_enum(const toml::table& t, const char* section, const char* key, const Names<E, N>& names, E* out) {
  if (auto v = t[section][key].template value<std::string>()) *out = names.from(*v, *out);
}
void get_int(const toml::table& t, const char* section, const char* key, int* out, int lo, int hi) {
  if (auto v = t[section][key].template value<int64_t>()) *out = static_cast<int>(std::clamp<int64_t>(*v, lo, hi));
}
template <class E, size_t N>
void put_enum(toml::table& sec, const char* key, const Names<E, N>& names, E e) {
  sec.insert_or_assign(key, names.to(e));
}

}  // namespace

std::vector<ColumnSetting> default_columns() {
  return {{"name", 260, true}, {"modified", 150, true}, {"type", 160, true}, {"size", 80, true}, {"permissions", 90, false}, {"path", 220, false}};
}

std::string fm_settings_path() { return (config_internal::config_home() / "fleetwm" / "fleetfm.toml").string(); }

FmSettings load_fm_settings() {
  FmSettings s;
  const fs::path path = fm_settings_path();
  std::error_code ec;
  if (!fs::exists(path, ec)) return s;
  try {
    toml::table t = toml::parse_file(path.string());
    if (auto v = t["general"]["style"].value<std::string>()) parse_style(*v, &s.style);
    get_enum(t, "general", "open_folders_in", kOpenIn, &s.open_folders_in);
    get_enum(t, "general", "click_mode", kClick, &s.click_mode);
    get(t, "general", "show_navigation_pane", &s.show_navigation_pane);
    get(t, "general", "nav_show_all_folders", &s.nav_show_all_folders);
    get(t, "general", "nav_expand_to_current", &s.nav_expand_to_current);
    get(t, "general", "nav_show_libraries", &s.nav_show_libraries);
    get(t, "general", "nav_show_favorites", &s.nav_show_favorites);
    get(t, "general", "nav_show_network", &s.nav_show_network);
    get(t, "general", "nav_show_trash", &s.nav_show_trash);
    get_enum(t, "general", "startup", kStartup, &s.startup);
    get(t, "general", "startup_path", &s.startup_path);
    get(t, "general", "last_location", &s.last_location);
    get(t, "general", "show_recent_files", &s.show_recent_files);
    get(t, "general", "middle_click_opens_tab", &s.middle_click_opens_tab);

    get(t, "tabs", "always_show_tabs", &s.always_show_tabs);
    get_enum(t, "tabs", "new_tab_at", kNewTab, &s.new_tab_at);
    get(t, "tabs", "close_window_with_last_tab", &s.close_window_with_last_tab);
    get(t, "tabs", "restore_tabs", &s.restore_tabs);
    if (auto* arr = t["tabs"]["open_tabs"].as_array())
      for (auto& el : *arr)
        if (auto v = el.value<std::string>()) s.open_tabs.push_back(*v);

    if (auto v = t["view"]["default_view"].value<std::string>()) parse_view_mode(*v, &s.default_view);
    else s.default_view = style_spec(s.style).default_view;
    get(t, "view", "style_sets_view", &s.style_sets_view);
    get(t, "view", "show_hidden", &s.show_hidden);
    get(t, "view", "show_extensions", &s.show_extensions);
    get(t, "view", "show_full_path_in_title", &s.show_full_path_in_title);
    get(t, "view", "show_free_space_bars", &s.show_free_space_bars);
    get(t, "view", "show_status_bar", &s.show_status_bar);
    get(t, "view", "show_details_pane", &s.show_details_pane);
    get(t, "view", "show_preview_pane", &s.show_preview_pane);
    get_enum(t, "view", "menu_bar", kMenu, &s.menu_bar);
    get(t, "view", "use_checkboxes", &s.use_checkboxes);
    get(t, "view", "row_stripes", &s.row_stripes);
    get(t, "view", "compact_rows", &s.compact_rows);
    get_enum(t, "view", "type_ahead", kType, &s.type_ahead);
    get(t, "view", "show_thumbnails", &s.show_thumbnails);
    get_int(t, "view", "thumbnail_max_mb", &s.thumbnail_max_mb, 1, 1024);
    get(t, "view", "remember_folder_views", &s.remember_folder_views);

    get_enum(t, "sort", "key", kSort, &s.sort_key);
    get(t, "sort", "ascending", &s.sort_ascending);
    get(t, "sort", "folders_first", &s.folders_first);
    get_enum(t, "sort", "group_by", kGroup, &s.group_by);
    get_enum(t, "sort", "size_format", kSize, &s.size_format);
    get_enum(t, "sort", "date_style", kDate, &s.date_style);
    if (auto* arr = t["columns"]["list"].as_array())
      for (auto& el : *arr)
        if (auto* ct = el.as_table()) {
          ColumnSetting c;
          if (auto v = (*ct)["id"].value<std::string>()) c.id = *v;
          if (auto v = (*ct)["width"].value<int64_t>()) c.width = static_cast<int>(std::clamp<int64_t>(*v, 40, 2000));
          if (auto v = (*ct)["visible"].value<bool>()) c.visible = *v;
          if (!c.id.empty()) s.columns.push_back(c);
        }

    get_enum(t, "appearance", "colour_scheme", kScheme, &s.colour_scheme);
    get_int(t, "appearance", "font_px", &s.font_px, 0, 40);
    get_int(t, "appearance", "row_height", &s.row_height, 0, 80);
    get_int(t, "appearance", "icon_px", &s.icon_px, 0, 256);
    get_int(t, "appearance", "nav_width", &s.nav_width, 0, 800);
    get_int(t, "appearance", "window_w", &s.window_w, 640, 8000);
    get_int(t, "appearance", "window_h", &s.window_h, 480, 8000);

    get(t, "search", "subfolders", &s.search_subfolders);
    get(t, "search", "partial", &s.search_partial);
    get(t, "search", "hidden", &s.search_hidden);
    get(t, "search", "contents", &s.search_in_contents);
    get(t, "search", "case_sensitive", &s.search_case_sensitive);
    get_int(t, "search", "max_results", &s.search_max_results, 10, 1000000);

    get_enum(t, "transfer", "verify", kVerify, &s.verify);
    if (auto v = t["transfer"]["algorithm"].value<std::string>()) parse_hash_name(*v, &s.verify_algo);
    get_enum(t, "transfer", "sync", kSync, &s.sync);
    get_int(t, "transfer", "block_kib", &s.block_kib, 64, 65536);
    get(t, "transfer", "direct_verify", &s.direct_verify);
    get(t, "transfer", "follow_symlinks", &s.follow_symlinks);
    get(t, "transfer", "show_details", &s.show_transfer_details);
    get(t, "transfer", "confirm_conflicts", &s.confirm_conflicts);
    get_enum(t, "transfer", "delete_mode", kDelete, &s.delete_mode);
    get(t, "transfer", "confirm_delete", &s.confirm_delete);

    get(t, "drives", "show_unmounted_removable", &s.show_unmounted_removable);
    get(t, "drives", "open_after_mount", &s.open_after_mount);
    get(t, "drives", "power_off_after_eject", &s.power_off_after_eject);
    get(t, "drives", "show_network_in_this_pc", &s.show_network_in_this_pc);

    get(t, "privacy", "remember_history", &s.remember_history);
    get_int(t, "privacy", "history_size", &s.history_size, 0, 100000);
  } catch (const toml::parse_error&) {
    return FmSettings{};
  }
  return s;
}

void save_fm_settings(const FmSettings& s) {
  const fs::path path = fm_settings_path();
  fs::create_directories(path.parent_path());
  toml::table root, general, tabs, view, sort, appearance, search, transfer, drives, privacy, columns;
  general.insert_or_assign("style", style_key(s.style));
  put_enum(general, "open_folders_in", kOpenIn, s.open_folders_in);
  put_enum(general, "click_mode", kClick, s.click_mode);
  general.insert_or_assign("show_navigation_pane", s.show_navigation_pane);
  general.insert_or_assign("nav_show_all_folders", s.nav_show_all_folders);
  general.insert_or_assign("nav_expand_to_current", s.nav_expand_to_current);
  general.insert_or_assign("nav_show_libraries", s.nav_show_libraries);
  general.insert_or_assign("nav_show_favorites", s.nav_show_favorites);
  general.insert_or_assign("nav_show_network", s.nav_show_network);
  general.insert_or_assign("nav_show_trash", s.nav_show_trash);
  put_enum(general, "startup", kStartup, s.startup);
  general.insert_or_assign("startup_path", s.startup_path);
  general.insert_or_assign("last_location", s.last_location);
  general.insert_or_assign("show_recent_files", s.show_recent_files);
  general.insert_or_assign("middle_click_opens_tab", s.middle_click_opens_tab);

  tabs.insert_or_assign("always_show_tabs", s.always_show_tabs);
  put_enum(tabs, "new_tab_at", kNewTab, s.new_tab_at);
  tabs.insert_or_assign("close_window_with_last_tab", s.close_window_with_last_tab);
  tabs.insert_or_assign("restore_tabs", s.restore_tabs);
  toml::array ot;
  for (const std::string& p : s.open_tabs) ot.push_back(p);
  tabs.insert_or_assign("open_tabs", std::move(ot));

  view.insert_or_assign("default_view", view_mode_key(s.default_view));
  view.insert_or_assign("style_sets_view", s.style_sets_view);
  view.insert_or_assign("show_hidden", s.show_hidden);
  view.insert_or_assign("show_extensions", s.show_extensions);
  view.insert_or_assign("show_full_path_in_title", s.show_full_path_in_title);
  view.insert_or_assign("show_free_space_bars", s.show_free_space_bars);
  view.insert_or_assign("show_status_bar", s.show_status_bar);
  view.insert_or_assign("show_details_pane", s.show_details_pane);
  view.insert_or_assign("show_preview_pane", s.show_preview_pane);
  put_enum(view, "menu_bar", kMenu, s.menu_bar);
  view.insert_or_assign("use_checkboxes", s.use_checkboxes);
  view.insert_or_assign("row_stripes", s.row_stripes);
  view.insert_or_assign("compact_rows", s.compact_rows);
  put_enum(view, "type_ahead", kType, s.type_ahead);
  view.insert_or_assign("show_thumbnails", s.show_thumbnails);
  view.insert_or_assign("thumbnail_max_mb", static_cast<int64_t>(s.thumbnail_max_mb));
  view.insert_or_assign("remember_folder_views", s.remember_folder_views);

  put_enum(sort, "key", kSort, s.sort_key);
  sort.insert_or_assign("ascending", s.sort_ascending);
  sort.insert_or_assign("folders_first", s.folders_first);
  put_enum(sort, "group_by", kGroup, s.group_by);
  put_enum(sort, "size_format", kSize, s.size_format);
  put_enum(sort, "date_style", kDate, s.date_style);
  toml::array cols;
  for (const ColumnSetting& c : s.columns) {
    toml::table ct;
    ct.insert_or_assign("id", c.id);
    ct.insert_or_assign("width", static_cast<int64_t>(c.width));
    ct.insert_or_assign("visible", c.visible);
    cols.push_back(std::move(ct));
  }
  columns.insert_or_assign("list", std::move(cols));

  put_enum(appearance, "colour_scheme", kScheme, s.colour_scheme);
  appearance.insert_or_assign("font_px", static_cast<int64_t>(s.font_px));
  appearance.insert_or_assign("row_height", static_cast<int64_t>(s.row_height));
  appearance.insert_or_assign("icon_px", static_cast<int64_t>(s.icon_px));
  appearance.insert_or_assign("nav_width", static_cast<int64_t>(s.nav_width));
  appearance.insert_or_assign("window_w", static_cast<int64_t>(s.window_w));
  appearance.insert_or_assign("window_h", static_cast<int64_t>(s.window_h));

  search.insert_or_assign("subfolders", s.search_subfolders);
  search.insert_or_assign("partial", s.search_partial);
  search.insert_or_assign("hidden", s.search_hidden);
  search.insert_or_assign("contents", s.search_in_contents);
  search.insert_or_assign("case_sensitive", s.search_case_sensitive);
  search.insert_or_assign("max_results", static_cast<int64_t>(s.search_max_results));

  put_enum(transfer, "verify", kVerify, s.verify);
  transfer.insert_or_assign("algorithm", hash_name(s.verify_algo));
  put_enum(transfer, "sync", kSync, s.sync);
  transfer.insert_or_assign("block_kib", static_cast<int64_t>(s.block_kib));
  transfer.insert_or_assign("direct_verify", s.direct_verify);
  transfer.insert_or_assign("follow_symlinks", s.follow_symlinks);
  transfer.insert_or_assign("show_details", s.show_transfer_details);
  transfer.insert_or_assign("confirm_conflicts", s.confirm_conflicts);
  put_enum(transfer, "delete_mode", kDelete, s.delete_mode);
  transfer.insert_or_assign("confirm_delete", s.confirm_delete);

  drives.insert_or_assign("show_unmounted_removable", s.show_unmounted_removable);
  drives.insert_or_assign("open_after_mount", s.open_after_mount);
  drives.insert_or_assign("power_off_after_eject", s.power_off_after_eject);
  drives.insert_or_assign("show_network_in_this_pc", s.show_network_in_this_pc);

  privacy.insert_or_assign("remember_history", s.remember_history);
  privacy.insert_or_assign("history_size", static_cast<int64_t>(s.history_size));

  root.insert_or_assign("general", std::move(general));
  root.insert_or_assign("tabs", std::move(tabs));
  root.insert_or_assign("view", std::move(view));
  root.insert_or_assign("sort", std::move(sort));
  root.insert_or_assign("columns", std::move(columns));
  root.insert_or_assign("appearance", std::move(appearance));
  root.insert_or_assign("search", std::move(search));
  root.insert_or_assign("transfer", std::move(transfer));
  root.insert_or_assign("drives", std::move(drives));
  root.insert_or_assign("privacy", std::move(privacy));
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
    out << root;
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
}

void apply_style(FmSettings* s, ViewStyle style) {
  s->style = style;
  const StyleSpec& sp = style_spec(style);
  if (s->style_sets_view) s->default_view = sp.default_view;
  s->font_px = 0;
  s->row_height = 0;
  s->icon_px = 0;
  s->nav_width = 0;
  s->show_details_pane = sp.details_pane;
  s->show_status_bar = sp.status_bar;
  s->menu_bar = sp.menu_bar ? MenuBar::Always : MenuBar::Alt;
  s->show_free_space_bars = true;
}

bool should_verify(const FmSettings& s, bool external) {
  switch (s.verify) {
    case VerifyWhen::Always: return true;
    case VerifyWhen::ExternalOnly: return external;
    case VerifyWhen::Never: return false;
  }
  return true;
}

TransferOptions transfer_options(const FmSettings& s, bool external, bool move) {
  TransferOptions o;
  o.move = move;
  o.verify = should_verify(s, external);
  o.algo = s.verify_algo;
  o.sync = s.sync;
  o.block_bytes = static_cast<size_t>(s.block_kib) * 1024;
  o.direct_verify = s.direct_verify;
  o.follow_symlinks = s.follow_symlinks;
  if (!s.confirm_conflicts) o.on_conflict = [](const ConflictInfo&) { return Conflict::KeepBoth; };
  return o;
}

}  // namespace fleetwm::fm
