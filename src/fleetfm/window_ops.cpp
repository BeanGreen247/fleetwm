#include <algorithm>
#include <chrono>
#include <cmath>
#include <toml++/toml.h>

#include <fcntl.h>

#include <cstring>
#include <filesystem>
#include <fstream>

#include "config_paths.hpp"
#include "file_ops.hpp"
#include "window.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

// ---------------------------------------------------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::open_menu(std::vector<MenuItem> items, double x, double y) {
  menu_.items = std::move(items);
  menu_.x = x;
  menu_.y = y;
  menu_.hot = -1;
  schedule_redraw();
}

void FmWindow::close_menu() {
  menu_.items.clear();
  menu_.hot = -1;
  sub_.items.clear();
  sub_.parent = sub_.hot = -1;
  schedule_redraw();
}

void FmWindow::open_menu_at_tool(Cmd c, std::vector<MenuItem> items) {
  for (const Region& r : prev_regions_)
    if (r.kind == R::Tool && r.arg == static_cast<int>(c)) {
      open_menu(std::move(items), r.r.x, r.r.y + r.r.h);
      return;
    }
  open_menu(std::move(items), mx_, my_);
}

namespace {
MenuItem mi(const char* label, Cmd c, int arg = 0, const char* shortcut = "", bool enabled = true, bool checked = false, bool radio = false) {
  MenuItem m;
  m.label = label;
  m.cmd = c;
  m.arg = arg;
  m.shortcut = shortcut;
  m.enabled = enabled;
  m.checked = checked;
  m.radio = radio;
  return m;
}
}  // namespace

std::vector<MenuItem> FmWindow::view_items() {
  const Browser& b = tab();
  std::vector<MenuItem> v;
  struct {
    const char* l;
    ViewMode m;
    const char* k;
  } modes[] = {{"Extra large icons", ViewMode::ExtraLargeIcons, "Ctrl+1"}, {"Large icons", ViewMode::LargeIcons, "Ctrl+2"}, {"Medium icons", ViewMode::MediumIcons, "Ctrl+3"},
               {"Small icons", ViewMode::SmallIcons, "Ctrl+4"}, {"List", ViewMode::List, "Ctrl+5"}, {"Details", ViewMode::Details, "Ctrl+6"}, {"Tiles", ViewMode::Tiles, "Ctrl+7"},
               {"Content", ViewMode::Content, "Ctrl+8"}};
  for (auto& m : modes) v.push_back(mi(m.l, Cmd::SetView, static_cast<int>(m.m), m.k, true, b.mode == m.m, true));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Show hidden files", Cmd::ToggleHidden, 0, "Ctrl+H", true, s_.show_hidden));
  v.push_back(mi("File name extensions", Cmd::ToggleExtensions, 0, "", true, s_.show_extensions));
  return v;
}

std::vector<MenuItem> FmWindow::group_items() {
  const Browser& b = tab();
  std::vector<MenuItem> v;
  v.push_back(mi("(none)", Cmd::SetGroup, static_cast<int>(GroupBy::None), "", true, b.group_by == GroupBy::None, true));
  v.push_back(mi("Name", Cmd::SetGroup, static_cast<int>(GroupBy::Name), "", true, b.group_by == GroupBy::Name, true));
  v.push_back(mi("Type", Cmd::SetGroup, static_cast<int>(GroupBy::Type), "", true, b.group_by == GroupBy::Type, true));
  v.push_back(mi("Size", Cmd::SetGroup, static_cast<int>(GroupBy::Size), "", true, b.group_by == GroupBy::Size, true));
  v.push_back(mi("Date modified", Cmd::SetGroup, static_cast<int>(GroupBy::Modified), "", true, b.group_by == GroupBy::Modified, true));
  return v;
}

std::vector<MenuItem> FmWindow::sort_items() {
  const Browser& b = tab();
  std::vector<MenuItem> v;

  v.push_back(mi("Name", Cmd::SetSort, static_cast<int>(SortKey::Name), "", true, b.sort_key == SortKey::Name, true));
  v.push_back(mi("Date modified", Cmd::SetSort, static_cast<int>(SortKey::Modified), "", true, b.sort_key == SortKey::Modified, true));
  v.push_back(mi("Type", Cmd::SetSort, static_cast<int>(SortKey::Type), "", true, b.sort_key == SortKey::Type, true));
  v.push_back(mi("Size", Cmd::SetSort, static_cast<int>(SortKey::Size), "", true, b.sort_key == SortKey::Size, true));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Ascending", Cmd::ToggleSortDirection, 1, "", true, b.ascending, true));
  v.push_back(mi("Descending", Cmd::ToggleSortDirection, 0, "", true, !b.ascending, true));
  return v;
}

std::vector<MenuItem> FmWindow::style_items() {
  std::vector<MenuItem> v;
  for (const StyleSpec& s : all_styles()) {
    MenuItem m;
    m.label = s.name;
    m.cmd = Cmd::SetStyle;
    m.arg = static_cast<int>(s.id);
    m.checked = s_.style == s.id;
    m.radio = true;
    v.push_back(m);
  }
  return v;
}

std::vector<MenuItem> FmWindow::organize_items() {
  const Browser& b = tab();
  const bool sel = b.selected_count() > 0;
  const bool fs_place = b.place == PlaceKind::Local || b.place == PlaceKind::Remote;
  std::vector<MenuItem> v;
  v.push_back(mi("Undo", Cmd::Undo, 0, "Ctrl+Z", !undo_.empty()));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Cut", Cmd::Cut, 0, "Ctrl+X", sel && fs_place));
  v.push_back(mi("Copy", Cmd::Copy, 0, "Ctrl+C", sel));
  v.push_back(mi("Paste", Cmd::Paste, 0, "Ctrl+V", fs_place));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Select all", Cmd::SelectAll, 0, "Ctrl+A"));
  v.push_back(mi("Invert selection", Cmd::InvertSelection, 0, "Ctrl+I"));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Rename", Cmd::Rename, 0, "F2", b.selected_count() == 1 && fs_place));
  v.push_back(mi("Delete", Cmd::Delete, 0, "Del", sel));
  v.push_back(mi("Properties", Cmd::Properties, 0, "Alt+Enter"));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Navigation pane", Cmd::ToggleNav, 0, "", true, s_.show_navigation_pane));
  v.push_back(mi("Details pane", Cmd::ToggleDetails, 0, "", true, s_.show_details_pane));
  v.push_back(mi("Preview pane", Cmd::TogglePreview, 0, "", true, s_.show_preview_pane));
  v.push_back(mi("Status bar", Cmd::ToggleStatus, 0, "", true, s_.show_status_bar));
  v.push_back(mi("Menu bar", Cmd::ToggleMenuBar, 0, "F10", true, s_.menu_bar == MenuBar::Always));
  v.push_back(MenuItem::sep());
  v.push_back(mi("Folder and search options...", Cmd::Settings, 0, "Ctrl+,"));
  return v;
}

std::vector<MenuItem> FmWindow::menu_bar_items(int which) {
  const bool fs_place = tab().place == PlaceKind::Local || tab().place == PlaceKind::Remote;
  std::vector<MenuItem> v;
  switch (which) {
    case 0:
      v.push_back(mi("New tab", Cmd::NewTab, 0, "Ctrl+T"));
      v.push_back(mi("New window", Cmd::NewWindow, 0, "Ctrl+N"));
      v.push_back(mi("New folder", Cmd::NewFolder, 0, "Ctrl+Shift+N", fs_place));
      v.push_back(MenuItem::sep());
      v.push_back(mi("Properties", Cmd::Properties, 0, "Alt+Enter"));
      v.push_back(MenuItem::sep());
      v.push_back(mi("Close tab", Cmd::CloseTab, static_cast<int>(cur_), "Ctrl+W"));
      v.push_back(mi("Quit", Cmd::Quit, 0, "Ctrl+Q"));
      break;
    case 1:
      v.push_back(mi("Cut", Cmd::Cut, 0, "Ctrl+X"));
      v.push_back(mi("Copy", Cmd::Copy, 0, "Ctrl+C"));
      v.push_back(mi("Paste", Cmd::Paste, 0, "Ctrl+V"));
      v.push_back(MenuItem::sep());
      v.push_back(mi("Select all", Cmd::SelectAll, 0, "Ctrl+A"));
      v.push_back(mi("Invert selection", Cmd::InvertSelection, 0, "Ctrl+I"));
      break;
    case 2:
      v = view_items();
      v.push_back(MenuItem::sep());
      {
        MenuItem so, gr, st;
        so.label = "Sort by";
        so.sub = sort_items();
        gr.label = "Group by";
        gr.sub = group_items();
        st.label = "Style";
        st.sub = style_items();
        v.push_back(so);
        v.push_back(gr);
        v.push_back(st);
      }
      break;
    case 3:
      v.push_back(mi("Connect to server...", Cmd::ConnectServer));
      v.push_back(mi("Add a Nextcloud account...", Cmd::AddNextcloud));
      v.push_back(MenuItem::sep());
      v.push_back(mi("Folder and search options...", Cmd::Settings, 0, "Ctrl+,"));
      break;
    default: v.push_back(mi("About File Manager", Cmd::About, 0, "F1")); break;
  }
  return v;
}

void FmWindow::menu_for_nav(int row, double x, double y) {
  if (row < 0 || row >= static_cast<int>(nav_.rows().size())) return;
  const NavRow& r = nav_.rows()[row];
  std::vector<MenuItem> v;
  if (!r.address.empty()) {
    MenuItem o = mi("Open", Cmd::Open, -1);
    o.sarg = r.address;
    v.push_back(o);
    MenuItem t = mi("Open in new tab", Cmd::OpenInNewTab, -1);
    t.sarg = r.address;
    v.push_back(t);
    MenuItem w = mi("Open in new window", Cmd::OpenInNewWindow, -1);
    w.sarg = r.address;
    v.push_back(w);
  }
  if (r.has_volume) {
    v.push_back(MenuItem::sep());
    MenuItem e = mi(r.volume.kind == DriveKind::Network ? "Disconnect" : "Eject", Cmd::Eject, 0, "", (r.volume.ejectable || r.volume.kind == DriveKind::Network) && !r.volume.system);
    e.sarg = r.volume.mounted ? r.volume.mountpoint : r.volume.device;
    v.push_back(e);
    if (!r.volume.mounted) {
      MenuItem m = mi("Mount", Cmd::Mount);
      m.sarg = r.volume.device;
      v.push_back(m);
    }
  }
  if (r.address == "trash:///") {
    v.push_back(MenuItem::sep());
    v.push_back(mi("Empty the Trash", Cmd::EmptyTrash));
  }
  for (size_t i = 0; i < places_.size(); ++i)
    if (places_[i].uri == r.address) {
      v.push_back(MenuItem::sep());
      MenuItem rm = mi("Remove this network location", Cmd::Disconnect, 1);
      rm.sarg = r.address;
      v.push_back(rm);
    }
  if (!v.empty()) open_menu(std::move(v), x, y);
}

std::vector<MenuItem> FmWindow::toolbar_menu(Cmd c) {
  switch (c) {
    case Cmd::MenuOrganize: return organize_items();
    case Cmd::MenuView: return view_items();
    case Cmd::MenuSort: return sort_items();
    case Cmd::MenuStyle: return style_items();
    default: return {};
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::toast(const std::string& t, double seconds) {
  toast_ = t;
  toast_until_ = now_() + seconds;
  schedule_redraw();
  if (host_.after) {
    auto alive = alive_;
    host_.after(static_cast<int>(seconds * 1000) + 50, [this, alive] {
      if (*alive) schedule_redraw();
    });
  }
}

void FmWindow::save_settings() {
  try {
    save_fm_settings(s_);
  } catch (const std::exception&) {
  }
}

std::string FmWindow::folder_views_path() { return (config_internal::config_home() / "fleetwm" / "fleetfm-views.toml").string(); }

// The view mode and order each folder was last shown with, kept between runs (Windows' "Remember each folder's view settings").
void FmWindow::load_folder_views() {
  if (!s_.remember_folder_views) return;
  std::error_code ec;
  if (!fs::exists(folder_views_path(), ec)) return;
  try {
    toml::table t = toml::parse_file(folder_views_path());
    if (auto* views = t["view"].as_table())
      for (auto&& [k, v] : *views) {
        auto* e = v.as_table();
        if (!e) continue;
        FolderView fv{s_.default_view, SortKey::Name, true};
        if (auto m = (*e)["mode"].value<std::string>()) parse_view_mode(*m, &fv.mode);
        if (auto sk = (*e)["sort"].value<std::string>()) {
          if (*sk == "size") fv.key = SortKey::Size;
          else if (*sk == "modified") fv.key = SortKey::Modified;
          else if (*sk == "type") fv.key = SortKey::Type;
        }
        if (auto a = (*e)["ascending"].value<bool>()) fv.ascending = *a;
        folder_views_[std::string(k.str())] = fv;
      }
  } catch (const toml::parse_error&) {
  }
}

void FmWindow::save_folder_views() const {
  if (!s_.remember_folder_views || folder_views_.empty()) return;
  toml::table views;
  size_t n = 0;
  for (const auto& [path, v] : folder_views_) {
    if (++n > 500) break;  // the newest few hundred are plenty
    toml::table e;
    e.insert_or_assign("mode", view_mode_key(v.mode));
    e.insert_or_assign("sort", v.key == SortKey::Size ? "size" : (v.key == SortKey::Modified ? "modified" : (v.key == SortKey::Type ? "type" : "name")));
    e.insert_or_assign("ascending", v.ascending);
    views.insert_or_assign(path, std::move(e));
  }
  toml::table root;
  root.insert_or_assign("view", std::move(views));
  try {
    fs::create_directories(fs::path(folder_views_path()).parent_path());
    const std::string tmp = folder_views_path() + ".tmp";
    {
      std::ofstream out(tmp);
      out << root;
    }
    fs::rename(tmp, folder_views_path());
  } catch (const std::exception&) {
  }
}

void FmWindow::remember_folder_view() {
  if (!s_.remember_folder_views) return;
  const Browser& b = tab();
  if (b.place != PlaceKind::Local && b.place != PlaceKind::Remote) return;
  folder_views_[b.path] = {b.mode, b.sort_key, b.ascending};
}

void FmWindow::apply_folder_view() {
  if (!s_.remember_folder_views) return;
  Browser& b = tab();
  auto it = folder_views_.find(b.path);
  if (it == folder_views_.end()) return;
  const FolderView v = it->second;
  if (b.mode != v.mode || b.sort_key != v.key || b.ascending != v.ascending) {
    b.mode = v.mode;
    b.sort_key = v.key;
    b.ascending = v.ascending;
    b.rebuild();
  }
}

void FmWindow::set_view_mode(ViewMode m) {
  tab().mode = m;
  s_.default_view = m;
  tab().scroll_x = tab().scroll_y = 0;
  remember_folder_view();
  save_settings();
  schedule_redraw();
}

void FmWindow::toggle_sort(SortKey k) {
  Browser& b = tab();
  if (b.sort_key == k) b.ascending = !b.ascending;
  else {
    b.sort_key = k;
    b.ascending = true;
  }
  if ((k == SortKey::Size || k == SortKey::Modified) && !b.stat_complete) request_full_stat();
  b.rebuild();
  remember_folder_view();
  s_.sort_key = b.sort_key;
  s_.sort_ascending = b.ascending;
  save_settings();
}

void FmWindow::run(Cmd c, int arg, const std::string& sarg) {
  Browser& b = tab();
  const bool fs_place = b.place == PlaceKind::Local || b.place == PlaceKind::Remote;
  switch (c) {
    case Cmd::None: break;
    case Cmd::Back: {
      remember_folder_view();
      const std::string a = b.go_back();
      if (!a.empty()) open_address(a, false, false);
      break;
    }
    case Cmd::Forward: {
      remember_folder_view();
      const std::string a = b.go_forward();
      if (!a.empty()) open_address(a, false, false);
      break;
    }
    case Cmd::Up: {
      const std::string a = b.up_address();
      if (!a.empty()) {
        remember_folder_view();
        open_address(a);
      }
      break;
    }
    case Cmd::Reload:
      refresh_volumes();
      if (!b.search.empty()) start_search(b.search);
      else load_current(true);
      break;
    case Cmd::Home: open_address(home_dir()); break;
    case Cmd::ThisPc: open_address("computer:///"); break;
    case Cmd::NewTab: {
      std::string where = home_dir();
      if (s_.new_tab_at == NewTabAt::CurrentFolder) where = b.address();
      else if (s_.new_tab_at == NewTabAt::ThisPc) where = "computer:///";
      add_tab(where);
      break;
    }
    case Cmd::CloseTab: close_tab(arg >= 0 ? static_cast<size_t>(arg) : cur_); break;
    case Cmd::NextTab: select_tab((cur_ + 1) % tabs_.size()); break;
    case Cmd::PrevTab: select_tab((cur_ + tabs_.size() - 1) % tabs_.size()); break;
    case Cmd::ReopenTab:
      if (!closed_tabs_.empty()) {
        const std::string a = closed_tabs_.back();
        closed_tabs_.pop_back();
        add_tab(a);
      }
      break;
    case Cmd::NewWindow:
      if (host_.new_window) host_.new_window(b.address());
      break;
    case Cmd::NewFolder:
      if (fs_place) new_folder(arg == 1);
      break;
    case Cmd::Cut: copy_selection(true); break;
    case Cmd::Copy: copy_selection(false); break;
    case Cmd::Paste: paste_here(arg == 1); break;
    case Cmd::Delete: delete_selection(s_.delete_mode == DeleteMode::Permanent || b.place == PlaceKind::Trash); break;
    case Cmd::DeletePermanent: delete_selection(true); break;
    case Cmd::Rename: begin_rename(); break;
    case Cmd::Properties: show_properties(); break;
    case Cmd::SelectAll: b.select_all(); break;
    case Cmd::SelectNone: b.clear_selection(); break;
    case Cmd::InvertSelection: b.invert_selection(); break;
    case Cmd::Open:
      if (arg == -1 && !sarg.empty()) {
        open_address(sarg);
      } else {
        const std::vector<int> sel = b.selected();
        for (size_t k = 0; k < sel.size(); ++k) {
          if (sel.size() == 1 || !b.is_dir_at(sel[k])) item_activate(sel[k]);
        }
      }
      break;
    case Cmd::OpenInNewTab:
      if (!sarg.empty()) add_tab(sarg);
      else
        for (int i : b.selected())
          if (b.is_dir_at(i)) add_tab(b.path_at(i));
      break;
    case Cmd::OpenInNewWindow:
      if (host_.new_window) {
        if (!sarg.empty()) host_.new_window(sarg);
        else
          for (int i : b.selected())
            if (b.is_dir_at(i)) host_.new_window(b.path_at(i));
      }
      break;
    case Cmd::Eject:
      if (!sarg.empty()) eject_volume_at(sarg);
      else if (b.place == PlaceKind::Computer && b.focus >= 0 && b.focus < static_cast<int>(volumes_.size())) eject_volume_at(volumes_[b.focus].mounted ? volumes_[b.focus].mountpoint : volumes_[b.focus].device);
      else if (const Volume* v = volume_for_path(b.path)) eject_volume_at(v->mountpoint);
      break;
    case Cmd::Mount:
      mount_device(sarg);
      break;
    case Cmd::ConnectServer:
      connect_uri_.clear();
      connect_error_.clear();
      connect_user_.clear();
      connect_pass_.clear();
      open_dialog(Dlg::Connect);
      break;
    case Cmd::AddNextcloud:
      nc_server_.clear();
      nc_user_.clear();
      nc_pass_.clear();
      connect_error_.clear();
      open_dialog(Dlg::Nextcloud);
      break;
    case Cmd::Disconnect:
      if (arg == 1) {  // remove a saved place
        places_.erase(std::remove_if(places_.begin(), places_.end(), [&](const SavedPlace& p) { return p.uri == sarg; }), places_.end());
        try {
          save_places(places_);
        } catch (const std::exception&) {
        }
        nav_dirty_ = true;
      } else {
        eject_volume_at(sarg);
      }
      break;
    case Cmd::Settings: open_dialog(Dlg::Settings); break;
    case Cmd::About: open_dialog(Dlg::About); break;
    case Cmd::SetView: set_view_mode(static_cast<ViewMode>(arg)); break;
    case Cmd::SetSort:
      b.sort_key = static_cast<SortKey>(arg);
      if ((b.sort_key == SortKey::Size || b.sort_key == SortKey::Modified) && !b.stat_complete) request_full_stat();
      b.rebuild();
      remember_folder_view();
      break;
    case Cmd::SetGroup:
      b.group_by = static_cast<GroupBy>(arg);
      s_.group_by = b.group_by;
      if ((b.group_by == GroupBy::Size || b.group_by == GroupBy::Modified) && !b.stat_complete) request_full_stat();
      b.rebuild();
      b.scroll_y = 0;
      save_settings();
      break;
    case Cmd::ToggleSortDirection:
      b.ascending = arg == 1;
      b.rebuild();
      remember_folder_view();
      break;
    case Cmd::ToggleHidden:
      s_.show_hidden = !s_.show_hidden;
      apply_settings();
      save_settings();
      break;
    case Cmd::ToggleExtensions:
      s_.show_extensions = !s_.show_extensions;
      save_settings();
      break;
    case Cmd::ToggleNav:
      s_.show_navigation_pane = !s_.show_navigation_pane;
      save_settings();
      break;
    case Cmd::ToggleDetails:
      s_.show_details_pane = !s_.show_details_pane;
      save_settings();
      break;
    case Cmd::TogglePreview:
      s_.show_preview_pane = !s_.show_preview_pane;
      save_settings();
      break;
    case Cmd::ToggleStatus:
      s_.show_status_bar = !s_.show_status_bar;
      save_settings();
      break;
    case Cmd::ToggleMenuBar:
      s_.menu_bar = s_.menu_bar == MenuBar::Always ? MenuBar::Alt : MenuBar::Always;
      save_settings();
      break;
    case Cmd::EmptyTrash: open_dialog(Dlg::ConfirmEmptyTrash); break;
    case Cmd::Restore: {
      if (b.place != PlaceKind::Trash) break;
      int n = 0;
      std::string err;
      for (int i : b.selected())
        for (const TrashItem& it : trash_items_)
          if (it.name == b.name_at(i)) {
            if (trash_.restore(it, &err)) ++n;
            else show_message("Restore", err, true);
          }
      load_current(true);
      if (n) toast(std::to_string(n) + (n == 1 ? " item restored" : " items restored"));
      break;
    }
    case Cmd::Checksums: compute_checksums(); break;
    case Cmd::SetStyle: {
      ViewStyle st = static_cast<ViewStyle>(arg);
      apply_style(&s_, st);
      for (Browser& t : tabs_) t.mode = s_.default_view;
      save_settings();
      apply_settings();
      break;
    }
    case Cmd::FocusAddress:
      addr_focus_ = true;
      search_focus_ = false;
      addr_edit_.set(b.address());
      break;
    case Cmd::FocusSearch:
      search_focus_ = true;
      addr_focus_ = false;
      search_box_open_ = true;
      break;
    case Cmd::ClearSearch:
      search_edit_.set("", false);
      start_search("");
      break;
    case Cmd::MenuOrganize:
    case Cmd::MenuView:
    case Cmd::MenuSort:
    case Cmd::MenuStyle: {
      std::vector<MenuItem> items = toolbar_menu(c);
      auto sub = [](const char* label, std::vector<MenuItem> v) {
        MenuItem m;
        m.label = label;
        m.sub = std::move(v);
        return m;
      };
      if (c == Cmd::MenuView) {
        items.push_back(MenuItem::sep());
        items.push_back(sub("Sort by", sort_items()));
        items.push_back(sub("Group by", group_items()));
        items.push_back(sub("Style", style_items()));
      }
      if (c == Cmd::MenuOrganize && (style_->toolbar == ToolbarKind::HeaderBar || style_->toolbar == ToolbarKind::Finder || style_->toolbar == ToolbarKind::Icons || style_->toolbar == ToolbarKind::Compact)) {
        items.push_back(MenuItem::sep());
        MenuItem st;
        st.label = "Style";
        st.sub = style_items();
        items.push_back(st);
        items.push_back(MenuItem::sep());
        items.push_back(mi("Connect to server...", Cmd::ConnectServer));
        items.push_back(mi("Add a Nextcloud account...", Cmd::AddNextcloud));
        items.push_back(mi("About File Manager", Cmd::About, 0, "F1"));
      }
      open_menu_at_tool(c, std::move(items));
      break;
    }
    case Cmd::DropMove:
    case Cmd::DropCopy:
      start_transfer(drop_paths_, sarg, c == Cmd::DropMove);
      drop_paths_.clear();
      break;
    case Cmd::DropSymlink:
    case Cmd::DropHardlink: {
      int made_n = 0;
      std::string err, made;
      std::vector<std::string> made_links;
      for (const std::string& p : drop_paths_) {
        const bool ok = c == Cmd::DropSymlink ? make_symlink_to(p, sarg, &made, &err) : make_hardlink_to(p, sarg, &made, &err);
        if (ok) {
          ++made_n;
          made_links.push_back(made);
        }
        else {
          show_message("Create link", "'" + fs::path(p).filename().string() + "': " + err, true);
          break;
        }
      }
      drop_paths_.clear();
      if (made_n) {
        push_undo("Create link", [made_links]() -> std::string {
          for (const std::string& m : made_links) ::unlink(m.c_str());
          return "";
        });
        toast(std::to_string(made_n) + (made_n == 1 ? " link created" : " links created"));
        load_current(true);
      }
      break;
    }
    case Cmd::Undo: undo_last(); break;
    case Cmd::OpenTerminal: open_terminal_in(sarg.empty() ? b.path : sarg); break;
    case Cmd::CopyLocation: {
      std::string t;
      for (int i : b.selected()) t += (t.empty() ? "" : "\n") + b.path_at(i);
      if (t.empty()) t = b.path;
      if (host_.set_clipboard_text) host_.set_clipboard_text(t);
      toast("Location copied");
      break;
    }
    case Cmd::OpenWithApp:
      if (arg >= 0 && arg < static_cast<int>(open_with_list_.size())) {
        std::vector<std::string> argv = kit::exec_argv(open_with_list_[arg]);
        argv.push_back(sarg);
        if (!spawn_detached(argv)) show_message("Open with", "The program could not be started.", true);
      }
      break;
    case Cmd::SendTo: {
      std::vector<std::string> paths;
      for (int i : b.selected()) paths.push_back(b.path_at(i));
      start_transfer(paths, sarg, false);
      break;
    }
    case Cmd::CreateShortcut: {
      int n = 0;
      std::string made, err;
      std::vector<std::string> made_all;
      for (int i : b.selected())
        if (make_symlink_to(b.path_at(i), b.path, &made, &err)) {
          ++n;
          made_all.push_back(made);
        } else {
          show_message("Create shortcut", err, true);
          break;
        }
      if (n) {
        push_undo("Create shortcut", [made_all]() -> std::string {
          for (const std::string& m : made_all) ::unlink(m.c_str());
          return "";
        });
        load_current(true);
      }
      break;
    }
    case Cmd::Compress: {
      std::vector<std::string> paths;
      for (int i : b.selected()) paths.push_back(b.path_at(i));
      run_archive(false, arg, paths);
      break;
    }
    case Cmd::Extract: {
      std::vector<std::string> paths;
      for (int i : b.selected()) paths.push_back(b.path_at(i));
      run_archive(true, arg, paths);
      break;
    }
    case Cmd::ScanNetwork: scan_network(); break;
    case Cmd::Quit:
      if (host_.quit) host_.quit();
      break;
    default: break;
  }
  schedule_redraw();
}

// ---------------------------------------------------------------------------------------------------------------------
// rename, new folder
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::begin_rename() {
  Browser& b = tab();
  if (b.selected_count() != 1 || (b.place != PlaceKind::Local && b.place != PlaceKind::Remote)) return;
  const int i = b.selected()[0];
  renaming_ = true;
  rename_index_ = i;
  rename_path_ = b.path_at(i);
  rename_edit_.set(b.label_at(i));
  if (b.is_dir_at(i)) rename_edit_.select_all();
  else rename_edit_.select_stem();
  const ViewMetrics m = metrics();
  scroll_to_show(m, i, &b.scroll_x, &b.scroll_y);
  schedule_redraw();
}

void FmWindow::cancel_rename() {
  renaming_ = false;
  rename_index_ = -1;
  schedule_redraw();
}

void FmWindow::commit_rename() {
  if (!renaming_) return;
  renaming_ = false;
  const std::string neu = rename_edit_.text;
  const std::string path = rename_path_;
  rename_index_ = -1;
  if (fs::path(path).filename() == neu) return;
  std::string err;
  if (!rename_in_place(path, neu, &err)) {
    show_message("Rename", err, true);
    return;
  }
  {
    const std::string old_name = fs::path(path).filename().string(), dir = fs::path(path).parent_path().string();
    push_undo("Rename " + old_name, [dir, old_name, neu]() -> std::string {
      std::string e;
      return rename_in_place(dir + "/" + neu, old_name, &e) ? "" : e;
    });
  }
  select_after_load_ = neu;
  load_current(true);
}

void FmWindow::new_folder(bool text_document) {
  Browser& b = tab();
  std::string err;
  std::string name;
  if (text_document) {
    name = unique_name(b.path, "New Text Document.txt");
    const int fd = ::open((b.path + "/" + name).c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
      show_message("New text document", std::string("The file could not be created: ") + std::strerror(errno), true);
      return;
    }
    ::close(fd);
  } else {
    name = new_folder_name(b.path);
    if (!make_dir(b.path + "/" + name, &err)) {
      show_message("New folder", "The folder could not be created: " + err, true);
      return;
    }
  }
  {
    const std::string created = b.path + "/" + name;
    push_undo(text_document ? "New text document" : "New folder", [this, created]() -> std::string {
      std::string err2;
      return trash_.put(created, &err2) ? "" : err2;
    });
  }
  select_after_load_ = name;
  rename_after_load_ = true;
  load_current(true);
}

// ---------------------------------------------------------------------------------------------------------------------
// clipboard and transfers
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::copy_selection(bool cut) {
  Browser& b = tab();
  if (b.selected_count() == 0) return;
  if (cut && b.place != PlaceKind::Local && b.place != PlaceKind::Remote) cut = false;
  clip_.clear();
  for (int i : b.selected()) clip_.push_back(b.place == PlaceKind::Trash ? std::string() : b.path_at(i));
  clip_.erase(std::remove(clip_.begin(), clip_.end(), std::string()), clip_.end());
  clip_cut_ = cut;
  if (host_.set_clipboard_files) host_.set_clipboard_files(clip_, cut);
  toast(std::to_string(clip_.size()) + (clip_.size() == 1 ? " item " : " items ") + (cut ? "cut" : "copied"), 2.0);
}

void FmWindow::paste_here(bool into_selected_folder) {
  Browser& b = tab();
  if (b.place != PlaceKind::Local && b.place != PlaceKind::Remote) return;
  std::string dest = b.path;
  if (into_selected_folder && b.selected_count() == 1 && b.is_dir_at(b.selected()[0])) dest = b.path_at(b.selected()[0]);
  // The system clipboard first: it carries what other programs copied, and what this one copied (it was put there). The internal list is
  // the fallback for a compositor without a clipboard.
  if (host_.read_clipboard_files) {
    host_.read_clipboard_files([this, dest](const std::vector<std::string>& paths, bool cut) {
      if (!paths.empty()) {
        start_transfer(paths, dest, cut);
        return;
      }
      if (!clip_.empty()) {
        start_transfer(clip_, dest, clip_cut_);
        if (clip_cut_) clip_.clear();
      }
    });
    return;
  }
  if (!clip_.empty()) {
    start_transfer(clip_, dest, clip_cut_);
    if (clip_cut_) clip_.clear();
    return;
  }
  // nothing copied here: ask the system clipboard for file paths other programs put there
  if (host_.paste_text) {
    paste_target_ = 0;
    host_.paste_text([this](const std::string& t) { on_paste_text(t); });
  }
}

void FmWindow::start_transfer(std::vector<std::string> sources, const std::string& dest, bool move) {
  if (sources.empty()) return;
  auto j = std::make_unique<Job>();
  j->id = next_job_++;
  j->move = move;
  j->dest_path = dest;
  j->sources = sources;
  j->to = fs::path(dest).filename().empty() ? dest : fs::path(dest).filename().string();
  j->from = sources.size() == 1 ? fs::path(sources[0]).parent_path().filename().string() : std::to_string(sources.size()) + " items";
  if (j->from.empty()) j->from = "/";
  const bool external = is_external_destination(dest);
  TransferOptions o = transfer_options(s_, external, move);
  j->verify_algo = o.algo;
  j->verifying = o.verify;
  j->started = now_();
  j->details = s_.show_transfer_details;
  Job* jp = j.get();
  if (s_.confirm_conflicts) {
    o.on_conflict = [this, jp](const ConflictInfo& ci) {
      std::unique_lock<std::mutex> lk(jp->mu);
      if (jp->have_all) return jp->all_answer;
      jp->question = ci;
      jp->answered = false;
      jp->asking = true;
      post_([this, jp] {
        conflict_ = jp->question;
        conflict_job_ = jp;
        conflict_all_ = false;
        open_dialog(Dlg::Conflict);
      });
      jp->cv.wait(lk, [&] { return jp->answered; });
      jp->asking = false;
      if (jp->apply_all) {
        jp->have_all = true;
        jp->all_answer = jp->answer;
      }
      return jp->answer;
    };
  }
  j->t = std::make_unique<Transfer>(std::move(sources), dest, std::move(o));
  j->th = std::thread([this, jp] {
    TransferResult r = jp->t->run();
    post_([this, jp, r = std::move(r)]() mutable {
      jp->result = std::move(r);
      finish_job(jp);
    });
  });
  jobs_.push_back(std::move(j));
  if (!job_poll_ && host_.after) {
    job_poll_ = true;
    auto alive = alive_;
    auto tick = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = tick;
    *tick = [this, weak, alive] {
      if (!*alive) return;
      bool any = false;
      for (auto& jj : jobs_) {
        if (!jj->done) {
          any = true;
          jj->speeds.push_back(jj->t->progress().speed);
          if (jj->speeds.size() > 60) jj->speeds.pop_front();
        }
      }
      schedule_redraw();
      if (any || !jobs_.empty()) {
        if (auto t = weak.lock()) host_.after(250, [t] { (*t)(); });
      } else {
        job_poll_ = false;
      }
    };
    job_tick_keep_ = tick;
    host_.after(250, [tick] { (*tick)(); });
  }
  schedule_redraw();
}

void FmWindow::finish_job(Job* j) {
  j->done = true;
  j->done_at = now_();
  if (!j->result.cancelled && !j->result.top_level.empty()) {
    const auto made = j->result.top_level;
    if (j->move) {
      push_undo("Move of " + std::to_string(made.size()) + (made.size() == 1 ? " item" : " items"), [this, made]() -> std::string {
        // back where they came from: one transfer per source folder
        for (const auto& [src, dst] : made) {
          std::error_code ec;
          if (!fs::exists(fs::symlink_status(dst, ec))) continue;
          if (fs::exists(fs::symlink_status(src, ec))) return "'" + fs::path(src).filename().string() + "' exists again at its old place";
          start_transfer({dst}, fs::path(src).parent_path().string(), true);
        }
        return "";
      });
    } else {
      push_undo("Copy of " + std::to_string(made.size()) + (made.size() == 1 ? " item" : " items"), [this, made]() -> std::string {
        std::string err;
        for (const auto& [src, dst] : made) {
          std::error_code ec;
          if (fs::exists(fs::symlink_status(dst, ec)) && !trash_.put(dst, &err)) return err;
        }
        return "";
      });
    }
  }
  if (j->th.joinable()) j->th.join();
  const Progress p = j->t->progress();
  (void)p;
  // The listing of the destination changed.
  Browser& b = tab();
  if (b.place == PlaceKind::Local && !b.loading && (b.path == j->to || true)) load_current(true);
  if (conflict_job_ == j) conflict_job_ = nullptr;
  if (dlg_ == Dlg::Conflict && !conflict_job_) close_dialog();
  schedule_redraw();
}

void FmWindow::reap_jobs() {
  const double now = now_();
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    Job& j = **it;
    // a clean copy fades from the screen on its own; problems stay until dismissed
    if (j.done && j.result.ok() && now - j.done_at > 8.0 && now > 0) j.finished_shown = true;
    if (j.done && j.finished_shown) {
      if (j.th.joinable()) j.th.join();
      it = jobs_.erase(it);
    } else {
      ++it;
    }
  }
}

void FmWindow::answer_conflict(Job* j, Conflict c, bool all) {
  {
    std::lock_guard<std::mutex> l(j->mu);
    j->answer = c;
    j->apply_all = all;
    j->answered = true;
    j->cv.notify_all();
  }
  conflict_job_ = nullptr;
  close_dialog();
}

// ---------------------------------------------------------------------------------------------------------------------
// delete
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::delete_selection(bool permanent) {
  Browser& b = tab();
  if (b.selected_count() == 0) return;
  std::vector<std::string> paths;
  for (int i : b.selected()) paths.push_back(b.place == PlaceKind::Trash ? b.name_at(i) : b.path_at(i));
  delete_paths_ = paths;
  delete_permanent_ = permanent || b.place == PlaceKind::Trash;
  if (s_.confirm_delete || s_.delete_mode == DeleteMode::Ask) {
    open_dialog(Dlg::ConfirmDelete);
    return;
  }
  do_delete(delete_paths_, delete_permanent_);
}

void FmWindow::do_delete(std::vector<std::string> paths, bool permanent) {
  const bool in_trash = tab().place == PlaceKind::Trash;
  std::vector<TrashItem> trash_snapshot = trash_items_;
  spawn_loader([this, paths, permanent, in_trash, trash_snapshot] {
    std::vector<std::string> failed, errs;
    int done = 0;
    Trash tr = trash_;
    auto trashed = std::make_shared<std::vector<TrashItem>>();
    for (const std::string& p : paths) {
      std::string err;
      bool ok;
      if (in_trash) {
        ok = false;
        for (const TrashItem& it : trash_snapshot)
          if (it.name == p) ok = tr.remove(it, &err);
      } else if (permanent) {
        ok = delete_tree(p, &err);
      } else {
        TrashItem where;
        ok = tr.put(p, &err, &where);
        if (ok) trashed->push_back(where);
      }
      if (ok) ++done;
      else {
        failed.push_back(p);
        errs.push_back(err);
      }
    }
    post_([this, done, failed, errs, permanent, in_trash, trashed] {
      load_current(true);
      if (!trashed->empty())
        push_undo("Delete of " + std::to_string(trashed->size()) + (trashed->size() == 1 ? " item" : " items"), [this, trashed]() -> std::string {
          for (const TrashItem& it : *trashed) {
            std::string err;
            if (!trash_.restore(it, &err)) return err;
          }
          return "";
        });
      if (!failed.empty()) {
        if (!permanent && !in_trash) {
          // The trash cannot take it (another file system, no permission): offer to delete for good.
          delete_paths_ = failed;
          delete_permanent_ = true;
          dlg_text_ = "These items cannot be moved to the Trash (" + errs[0] + "). Delete them permanently?";
          open_dialog(Dlg::ConfirmDelete);
        } else {
          show_message("Delete", "Could not delete " + std::to_string(failed.size()) + (failed.size() == 1 ? " item: " : " items: ") + errs[0], true);
        }
      } else if (done) {
        toast(std::to_string(done) + (done == 1 ? " item " : " items ") + (permanent ? "deleted" : "moved to the Trash"));
      }
    });
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// drives and servers
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::eject_volume_at(const std::string& id) {
  const Volume* found = nullptr;
  for (const Volume& v : volumes_)
    if (v.mountpoint == id || v.device == id) found = &v;
  if (!found) {
    show_message("Eject", "That drive is not mounted.", true);
    return;
  }
  Volume v = *found;
  // never pull a drive out from under a running copy
  auto inside = [&](const std::string& p) { return p == v.mountpoint || p.compare(0, v.mountpoint.size() + 1, v.mountpoint + "/") == 0; };
  for (const auto& j : jobs_) {
    bool uses = inside(j->dest_path);
    for (const std::string& src : j->sources) uses = uses || inside(src);
    if (!j->done && uses) {
      show_message("Problem Ejecting " + volume_display_name(v), "A file transfer to or from this drive is still running. Wait for it to finish, or cancel it, then try again.", true);
      return;
    }
  }
  // leave the drive first, or this window is what holds it
  for (Browser& t : tabs_) {
    if (t.place == PlaceKind::Local && (t.path == v.mountpoint || t.path.compare(0, v.mountpoint.size() + 1, v.mountpoint + "/") == 0)) {
      t.path = "/";
      t.place = PlaceKind::Computer;
      t.set_listing(DirListing{});
    }
  }
  toast("Ejecting " + volume_display_name(v) + "...", 30);
  CommandRunner* r = runner ? runner : &system_runner();
  std::vector<Volume> all = volumes_;
  spawn_loader([this, v, all, r] {
    EjectOptions o;
    o.power_off = s_.power_off_after_eject;
    EjectResult res = eject_volume(v, all, *r, o);
    post_([this, res, v] {
      toast_.clear();
      refresh_volumes();
      std::string msg = res.message;
      if (res.busy && !res.users.empty()) {
        msg += "\n\nIn use by:";
        for (const BusyUse& u : res.users) msg += "\n  " + u.comm + " (process " + std::to_string(u.pid) + ")";
      }
      show_message(res.title, msg, !res.ok);
      if (res.ok) toast("It is safe to remove " + volume_display_name(v), 6);
    });
  });
}

void FmWindow::mount_device(const std::string& device) {
  const Volume* found = nullptr;
  for (const Volume& v : volumes_)
    if (v.device == device) found = &v;
  if (!found) return;
  Volume v = *found;
  toast("Mounting " + volume_display_name(v) + "...", 20);
  CommandRunner* r = runner ? runner : &system_runner();
  spawn_loader([this, v, r] {
    std::string mp, err;
    const bool ok = mount_volume(v, *r, &mp, &err);
    post_([this, ok, mp, err] {
      toast_.clear();
      refresh_volumes();
      if (!ok) show_message("Mount", err.empty() ? "The drive could not be mounted." : err, true);
      else if (s_.open_after_mount && !mp.empty()) open_address(mp);
    });
  });
}

void FmWindow::connect_to(const Uri& u, const Credentials& c, const std::string& name, bool save) {
  connecting_ = true;
  connect_error_.clear();
  CommandRunner* r = runner ? runner : &system_runner();
  spawn_loader([this, u, c, name, save, r] {
    const MountOutcome o = mount_location(u, c, *r, gvfs_root(static_cast<unsigned>(::getuid())));
    post_([this, u, o, name, save] {
      connecting_ = false;
      if (!o.ok) {
        connect_error_ = o.error;
        schedule_redraw();
        return;
      }
      Uri clean = u;
      clean.password.clear();
      const std::string uri = uri_to_string(clean);
      if (save) {
        bool have = false;
        for (const SavedPlace& p : places_) have = have || p.uri == uri;
        if (!have) {
          places_.push_back({name.empty() ? clean.host : name, uri});
          try {
            save_places(places_);
          } catch (const std::exception&) {
          }
        }
      }
      close_dialog();
      refresh_volumes();
      nav_dirty_ = true;
      open_address(uri);
    });
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// messages, dialogs, properties
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::show_message(const std::string& title, const std::string& text, bool error) {
  dlg_title_ = title;
  dlg_text_ = text;
  dlg_error_ = error;
  last_message_ = text;
  open_dialog(Dlg::Message);
}

void FmWindow::open_dialog(Dlg d) {
  dlg_ = d;
  if (d == Dlg::Settings) settings_page_ = 0;
  if (!ui_) {
    kit::Palette pal;
    ui_ = std::make_unique<kit::Ui>(pal);
  }
  addr_focus_ = search_focus_ = false;
  schedule_redraw();
}

void FmWindow::close_dialog() {
  if (dlg_ == Dlg::Properties) {
    props_cancel_ = true;
    if (props_thread_.joinable()) props_thread_.join();
  }
  if (dlg_ == Dlg::Conflict && conflict_job_) {
    Job* j = conflict_job_;
    conflict_job_ = nullptr;
    dlg_ = Dlg::None;
    answer_conflict(j, Conflict::Cancel, false);
    return;
  }
  dlg_ = Dlg::None;
  schedule_redraw();
}

void FmWindow::show_properties() {
  Browser& b = tab();
  std::string path = b.path;
  if (b.place == PlaceKind::Trash || b.place == PlaceKind::Computer || b.place == PlaceKind::Network || b.place == PlaceKind::Recent) {
    if (b.selected_count() == 1 && b.place == PlaceKind::Recent) path = b.path_at(b.selected()[0]);
    else return;
  } else if (b.selected_count() == 1) {
    path = b.path_at(b.selected()[0]);
  } else if (b.selected_count() > 1) {
    // several: the common folder, with the total of the selection
    props_ = FileProps{};
    props_.name = std::to_string(b.selected_count()) + " items";
    props_.type = "Multiple selection";
    props_.path = b.path;
    props_size_ = {};
    for (int i : b.selected()) {
      const TreeSize t = measure_tree(b.path_at(i));
      props_size_.bytes += t.bytes;
      props_size_.on_disk += t.on_disk;
      props_size_.files += t.files;
      props_size_.folders += t.folders;
    }
    props_hashes_.clear();
    open_dialog(Dlg::Properties);
    return;
  }
  if (!read_props(path, &props_)) return;
  props_size_ = {};
  props_hashes_.clear();
  props_cancel_ = true;
  if (props_thread_.joinable()) props_thread_.join();
  props_cancel_ = false;
  if (props_.is_dir) {
    props_thread_ = std::thread([this, path] {
      const TreeSize t = measure_tree(path, &props_cancel_);
      post_([this, t] {
        std::lock_guard<std::mutex> l(props_mu_);
        props_size_ = t;
        schedule_redraw();
      });
    });
  } else {
    props_size_.bytes = props_.size;
    props_size_.on_disk = props_.on_disk;
    props_size_.files = 1;
  }
  open_dialog(Dlg::Properties);
}

Rect FmWindow::item_screen_rect(int index) const {
  const Browser& b = tab();
  const ViewMetrics m = metrics();
  const ItemRect r = item_rect(m, index, b.scroll_x, b.scroll_y);
  return {lay_.content.x + r.x, lay_.content.y + r.y, r.w, r.h};
}

std::string FmWindow::dialog_name() const {
  switch (dlg_) {
    case Dlg::None: return "";
    case Dlg::Settings: return "settings";
    case Dlg::About: return "about";
    case Dlg::Properties: return "properties";
    case Dlg::Connect: return "connect";
    case Dlg::Nextcloud: return "nextcloud";
    case Dlg::Conflict: return "conflict";
    case Dlg::ConfirmDelete: return "confirm-delete";
    case Dlg::Message: return "message";
    case Dlg::Checksums: return "checksums";
    case Dlg::Errors: return "errors";
    case Dlg::ConfirmEmptyTrash: return "confirm-empty-trash";
  }
  return "";
}

void FmWindow::show_settings_page(int page) {
  open_dialog(Dlg::Settings);
  settings_page_ = page;
}

void FmWindow::save_session() {
  const Browser& b = tab();
  remember_folder_view();
  save_folder_views();
  if (b.place == PlaceKind::Local) s_.last_location = b.path;
  if (s_.restore_tabs) {
    s_.open_tabs.clear();
    for (const Browser& t : tabs_) s_.open_tabs.push_back(t.address());
  }
  save_settings();
}

void FmWindow::screenshot_setup(const std::string& what) {
  if (what.empty()) return;
  Browser& b = tab();
  if (what == "settings") run(Cmd::Settings);
  else if (what == "about") run(Cmd::About);
  else if (what == "connect") run(Cmd::ConnectServer);
  else if (what == "nextcloud") run(Cmd::AddNextcloud);
  else if (what == "properties") {
    if (!b.shown.empty() && b.selected_count() == 0) b.select_only(0);
    run(Cmd::Properties);
  } else if (what == "menu-view") {
    open_menu(view_items(), 600, lay_.toolbar.y + lay_.toolbar.h);
  } else if (what == "menu-organize") {
    open_menu(organize_items(), 8, lay_.toolbar.y + lay_.toolbar.h);
  } else if (what == "menu-context") {
    if (!b.shown.empty()) b.select_only(std::min<int>(2, static_cast<int>(b.shown.size()) - 1));
    open_menu({}, 0, 0);
    menu_for_selection(lay_.content.x + 160, lay_.content.y + 90);
  } else if (what == "tabs") {
    add_tab(home_dir());
    add_tab("/usr");
  } else if (what == "select") {
    for (int i = 0; i < std::min<int>(3, static_cast<int>(b.shown.size())); ++i) b.sel[i] = 1;
    b.focus = 1;
  }
}

void FmWindow::compute_checksums() {
  Browser& b = tab();
  std::vector<std::string> files;
  for (int i : b.selected())
    if (!b.is_dir_at(i)) files.push_back(b.path_at(i));
  if (files.empty()) {
    show_message("Checksums", "Select one or more files first.", false);
    return;
  }
  checksum_rows_.clear();
  checksum_running_ = true;
  open_dialog(Dlg::Checksums);
  const HashAlgo algo = s_.verify_algo;
  spawn_loader([this, files, algo] {
    for (const std::string& f : files) {
      const std::string h = hash_file(f, algo);
      post_([this, h, f] {
        checksum_rows_.emplace_back(h.empty() ? "(could not be read)" : h, f);
        schedule_redraw();
      });
    }
    post_([this] {
      checksum_running_ = false;
      schedule_redraw();
    });
  });
}

}  // namespace fleetwm::fm
