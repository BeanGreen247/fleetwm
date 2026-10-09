#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <filesystem>

#include "file_ops.hpp"
#include "version.hpp"
#include "window.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
using kit::Ui;
using kit::UiRect;

// Places a row of buttons at the bottom right of the dialog. Returns the index pressed or -1.
int footer(Ui& ui, double w, double h, const std::vector<std::string>& labels, int accent = 0) {
  double total = 0;
  std::vector<double> widths;
  for (const std::string& l : labels) {
    widths.push_back(std::max(80.0, static_cast<double>(l.size()) * 8.0 + 28));
    total += widths.back() + 8;
  }
  ui.set_cursor_y(h - 46);
  ui.set_cursor_x(w - total - 8);
  int hit = -1;
  for (size_t i = 0; i < labels.size(); ++i) {
    if (i) ui.same_line();
    if (ui.button(labels[i], true, static_cast<int>(i) == accent)) hit = static_cast<int>(i);
  }
  return hit;
}

std::vector<std::string> style_names() {
  std::vector<std::string> v;
  for (const StyleSpec& s : all_styles()) v.push_back(s.name);
  return v;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::dialog_settings(Ui& ui, double w, double h) {
  static const std::vector<std::string> pages = {"General", "View", "Tabs", "Search", "Copy and verify", "Drives and network", "Appearance", "Privacy"};
  const double side = 170;
  {
    const kit::Palette& p = ui.palette();
    cairo_rectangle(cr_, 0, 0, side, h);
    kit::set_source(cr_, p.bg_secondary);
    cairo_fill(cr_);
    kit::draw_text(cr_, "Folder Options", 16, 34, 15, p.fg_primary, true);
  }
  ui.nav(pages, &settings_page_, {0, 50, side, h - 50});
  bool changed = false;
  bool style_changed = false;
  ui.set_margins(24, 8, 24);
  ui.set_label_width(190);
  ui.begin_scroll({side, 0, w - side, h - 56}, &settings_scroll_);
  ui.space(8);
  ui.title(pages[static_cast<size_t>(settings_page_)]);
  FmSettings& s = s_;
  auto on_off = [&](const char* label, bool* v) {
    if (ui.checkbox(label, v)) changed = true;
    ui.newline();
  };
  auto choice = [&](const char* label, const std::vector<std::string>& opts, int* idx, double wd = 190) {
    ui.row(label);
    if (ui.dropdown(opts, idx, wd)) changed = true;
  };
  switch (settings_page_) {
    case 0: {
      ui.section("Look");
      int st = static_cast<int>(s.style);
      ui.row("Style");
      if (ui.dropdown(style_names(), &st, 220)) {
        apply_style(&s, static_cast<ViewStyle>(st));
        for (Browser& t : tabs_) t.mode = s.default_view;
        style_changed = changed = true;
      }
      ui.newline();
      ui.paragraph("Windows 7 is the default. The other styles change the layout, the toolbar, the navigation pane and how selections are painted.");
      on_off("A style also switches the view mode", &s.style_sets_view);
      ui.section("Browse folders");
      int oi = static_cast<int>(s.open_folders_in);
      choice("Open each folder in", {"The same window", "Its own window", "A new tab"}, &oi);
      s.open_folders_in = static_cast<OpenIn>(oi);
      int cm = static_cast<int>(s.click_mode);
      choice("Click items", {"Double-click to open", "Single-click to open"}, &cm);
      s.click_mode = static_cast<ClickMode>(cm);
      ui.section("Navigation pane");
      on_off("Show the navigation pane", &s.show_navigation_pane);
      on_off("Show all folders", &s.nav_show_all_folders);
      on_off("Automatically expand to the current folder", &s.nav_expand_to_current);
      on_off("Show Favorites", &s.nav_show_favorites);
      on_off("Show Libraries", &s.nav_show_libraries);
      on_off("Show Network", &s.nav_show_network);
      on_off("Show Trash", &s.nav_show_trash);
      ui.section("When File Manager starts");
      int su = static_cast<int>(s.startup);
      choice("Open", {"This PC (Computer)", "My home folder", "Where I left off", "A folder I choose"}, &su);
      s.startup = static_cast<Startup>(su);
      if (s.startup == Startup::Custom) {
        ui.row("Folder");
        if (ui.text_entry(&s.startup_path, 300)) changed = true;
        ui.newline();
      }
      on_off("Reopen the tabs I had open", &s.restore_tabs);
      on_off("Show Recent Places", &s.show_recent_files);
      on_off("Middle-click opens folders in a new tab", &s.middle_click_opens_tab);
      break;
    }
    case 1: {
      ui.section("Files and folders");
      static const std::vector<std::string> views = {"Details", "List", "Small icons", "Medium icons", "Large icons", "Extra large icons", "Tiles", "Content"};
      static const ViewMode order[] = {ViewMode::Details, ViewMode::List, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons, ViewMode::Tiles, ViewMode::Content};
      int vi = 0;
      for (int i = 0; i < 8; ++i)
        if (order[i] == s.default_view) vi = i;
      choice("Default view", views, &vi);
      if (s.default_view != order[vi]) {
        s.default_view = order[vi];
        tab().mode = order[vi];
      }
      on_off("Show hidden files and folders", &s.show_hidden);
      on_off("Show file name extensions", &s.show_extensions);
      on_off("Show the full path in the title bar", &s.show_full_path_in_title);
      on_off("Use check boxes to select items", &s.use_checkboxes);
      on_off("Show thumbnails of pictures", &s.show_thumbnails);
      if (s.show_thumbnails) {
        ui.row("Largest picture to read (MB)");
        if (ui.spin(&s.thumbnail_max_mb, 1, 1024, 1)) changed = true;
        ui.newline();
      }
      on_off("Remember the view of each folder", &s.remember_folder_views);
      on_off("Alternate row shading", &s.row_stripes);
      on_off("Compact rows", &s.compact_rows);
      int ta = static_cast<int>(s.type_ahead);
      choice("When typing in a list", {"Select the typed item", "Type in the search box"}, &ta);
      s.type_ahead = static_cast<TypeAhead>(ta);
      ui.section("Panes and bars");
      on_off("Details pane", &s.show_details_pane);
      on_off("Preview pane (pictures and text files)", &s.show_preview_pane);
      on_off("Status bar", &s.show_status_bar);
      int mb = static_cast<int>(s.menu_bar);
      choice("Menu bar", {"Never shown", "Press Alt to show", "Always shown"}, &mb);
      s.menu_bar = static_cast<MenuBar>(mb);
      ui.section("Sorting");
      int gb = static_cast<int>(s.group_by);
      choice("Group by (Details view)", {"Nothing", "Name", "Type", "Size", "Date modified"}, &gb);
      if (static_cast<GroupBy>(gb) != s.group_by) {
        s.group_by = static_cast<GroupBy>(gb);
        for (Browser& t : tabs_) {
          t.group_by = s.group_by;
          t.rebuild();
        }
      }
      int sk = static_cast<int>(s.sort_key);
      choice("Sort by", {"Name", "Size", "Date modified", "Type"}, &sk);
      s.sort_key = static_cast<SortKey>(sk);
      on_off("Ascending", &s.sort_ascending);
      on_off("Folders before files", &s.folders_first);
      int sf = static_cast<int>(s.size_format);
      choice("File sizes", {"Whole KB (like Explorer)", "Exact bytes", "Automatic"}, &sf);
      s.size_format = static_cast<SizeFormat>(sf);
      int ds = static_cast<int>(s.date_style);
      choice("Dates", {"2026-10-09 04:19", "10/9/2026 4:19 AM", "09.10.2026 04:19"}, &ds);
      s.date_style = static_cast<DateStyle>(ds);
      ui.section("Columns in Details view");
      for (ColumnSetting& c : s.columns) {
        if (c.id == "path") continue;
        static const std::map<std::string, std::string> nice = {{"name", "Name"}, {"modified", "Date modified"}, {"type", "Type"}, {"size", "Size"}, {"permissions", "Permissions"}};
        auto it = nice.find(c.id);
        if (it == nice.end()) continue;
        if (c.id == "name") {
          ui.label("Name (always shown)", true);
          ui.newline();
          continue;
        }
        if (ui.checkbox(it->second, &c.visible)) changed = true;
        ui.newline();
      }
      break;
    }
    case 2: {
      ui.section("Tabs");
      on_off("Show the tab bar even with one tab", &s.always_show_tabs);
      int nt = static_cast<int>(s.new_tab_at);
      choice("A new tab opens", {"My home folder", "The current folder", "This PC (Computer)"}, &nt);
      s.new_tab_at = static_cast<NewTabAt>(nt);
      on_off("Close the window with its last tab", &s.close_window_with_last_tab);
      ui.paragraph("Shortcuts: Ctrl+T new tab, Ctrl+W close, Ctrl+Shift+T reopen, Ctrl+Tab / Ctrl+Shift+Tab switch, middle-click a folder to open it in a tab.");
      break;
    }
    case 3: {
      ui.section("Search");
      on_off("Include subfolders in search results", &s.search_subfolders);
      on_off("Find partial matches (\"rep\" finds \"Report\")", &s.search_partial);
      on_off("Include hidden files", &s.search_hidden);
      on_off("Search inside text files as well", &s.search_in_contents);
      on_off("Match upper and lower case", &s.search_case_sensitive);
      ui.row("Stop after this many results");
      if (ui.spin(&s.search_max_results, 100, 100000, 100)) changed = true;
      ui.newline();
      ui.paragraph("Use * and ? as wildcards: *.jpg, report-??.txt.");
      break;
    }
    case 4: {
      ui.section("Check files after copying");
      int vf = static_cast<int>(s.verify);
      choice("Verify the copy", {"Always", "Only on USB drives, cards and network places", "Never (fastest)"}, &vf, 300);
      s.verify = static_cast<VerifyWhen>(vf);
      ui.paragraph("Each file is written under a temporary name, flushed to the drive, read back from the drive (not from memory) and compared with the original by checksum. Only then does it get its real name. A move deletes the original only after that check passed.");
      static const HashAlgo algos[] = {HashAlgo::Auto, HashAlgo::Sha256, HashAlgo::Sha512, HashAlgo::Blake2b, HashAlgo::Sha1, HashAlgo::Md5};
      int al = 0;
      for (int i = 0; i < 6; ++i)
        if (algos[i] == s.verify_algo) al = i;
      choice("Checksum", {"Automatic (the fastest strong one)", "SHA-256", "SHA-512", "BLAKE2b", "SHA-1 (weaker)", "MD5 (weakest)"}, &al, 280);
      s.verify_algo = algos[al];
      int sy = static_cast<int>(s.sync);
      choice("Flush to the drive", {"Never", "When verifying", "After every file"}, &sy);
      s.sync = static_cast<SyncMode>(sy);
      on_off("Read the copy back from the device itself (O_DIRECT)", &s.direct_verify);
      ui.row("Copy buffer (KiB)");
      if (ui.spin(&s.block_kib, 64, 65536, 64)) changed = true;
      ui.newline();
      ui.section("While copying");
      on_off("Ask what to do when a name already exists", &s.confirm_conflicts);
      on_off("Copy the target of a link instead of the link", &s.follow_symlinks);
      on_off("Show the speed graph (More details)", &s.show_transfer_details);
      ui.section("Deleting");
      int dm = static_cast<int>(s.delete_mode);
      choice("Delete moves files to", {"The Trash", "Nowhere: delete permanently", "Ask me each time"}, &dm);
      s.delete_mode = static_cast<DeleteMode>(dm);
      on_off("Ask before deleting", &s.confirm_delete);
      break;
    }
    case 5: {
      ui.section("Drives");
      on_off("Show drives that are plugged in but not mounted", &s.show_unmounted_removable);
      on_off("Open a drive after mounting it", &s.open_after_mount);
      on_off("Switch the drive off after ejecting it", &s.power_off_after_eject);
      on_off("Show network drives under Computer", &s.show_network_in_this_pc);
      on_off("Free-space bars", &s.show_free_space_bars);
      ui.section("Network places");
      ui.paragraph("Servers are mounted through GVfs, the same layer Nautilus, Nemo, Caja and Thunar use: SMB (Samba, NAS), SFTP/SSH, FTP, WebDAV and Nextcloud, NFS, AFP, phones and cameras. Use Tools > Connect to server in the menu bar, or the buttons under Network.");
      if (ui.button("Connect to server...")) {
        close_dialog();
        run(Cmd::ConnectServer);
        break;
      }
      ui.same_line();
      if (ui.button("Add Nextcloud...")) {
        close_dialog();
        run(Cmd::AddNextcloud);
        break;
      }
      ui.newline();
      break;
    }
    case 6: {
      ui.section("Colours");
      int cs = static_cast<int>(s.colour_scheme);
      choice("Colour scheme", {"The style's own colours", "Follow the Fleetwm theme", "Light", "Dark"}, &cs);
      s.colour_scheme = static_cast<ColorScheme>(cs);
      ui.section("Sizes (0 = the style's own)");
      ui.row("Font size (px)");
      if (ui.spin(&s.font_px, 0, 40, 1)) changed = true;
      ui.newline();
      ui.row("Row height (px)");
      if (ui.spin(&s.row_height, 0, 80, 1)) changed = true;
      ui.newline();
      ui.row("Icon size (px)");
      if (ui.spin(&s.icon_px, 0, 256, 8)) changed = true;
      ui.newline();
      ui.row("Navigation pane width (px)");
      if (ui.spin(&s.nav_width, 0, 600, 10)) changed = true;
      ui.newline();
      if (ui.button("Restore the style's defaults")) {
        apply_style(&s, s.style);
        s.colour_scheme = ColorScheme::StyleDefault;
        style_changed = changed = true;
      }
      ui.newline();
      break;
    }
    default: {
      ui.section("History");
      on_off("Remember the folders I visit", &s.remember_history);
      ui.row("Keep this many entries");
      if (ui.spin(&s.history_size, 0, 1000, 10)) changed = true;
      ui.newline();
      if (ui.button("Clear history now")) {
        for (Browser& t : tabs_) {
          t.back.clear();
          t.forward.clear();
        }
        closed_tabs_.clear();
        folder_views_.clear();
        toast("History cleared");
      }
      ui.newline();
      break;
    }
  }
  ui.space(24);
  ui.end_scroll();
  if (changed) {
    if (style_changed) {
      apply_settings();
    } else {
      apply_settings();
    }
    save_settings();
  }
  if (footer(ui, w, h, {"Close"}) == 0) close_dialog();
}

// ---------------------------------------------------------------------------------------------------------------------
// about
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::dialog_about(Ui& ui, double w, double h) {
  cairo_save(cr_);
  cairo_translate(cr_, 24, 28);
  draw_icon(cr_, IconKind::App, 96);
  cairo_restore(cr_);
  const kit::Palette& p = ui.palette();
  kit::draw_text(cr_, "File Manager", 140, 58, 22, p.fg_primary, true);
  kit::draw_text(cr_, "fleetwm-fm  " + fleetwm::version_string(), 140, 82, 13, p.fg_secondary);
  kit::draw_text(cr_, "Native C++ file manager for Fleetwm.", 140, 108, 13, p.fg_primary);
  kit::draw_text(cr_, "Verified copies, safe eject, tabs, network places.", 140, 128, 13, p.fg_secondary);
  ui.set_margins(24, 150, 24);
  ui.set_cursor_y(150);
  ui.set_cursor_x(24);
  ui.section("Credits");
  ui.label("Copyright (c) 2026 Thomas Mozdren");
  ui.newline();
  if (ui.link("https://github.com/BeanGreen247/fleetwm")) fleetwm::fm::open_default("https://github.com/BeanGreen247/fleetwm");
  ui.newline();
  ui.label("License: MIT", true);
  ui.newline();
  ui.label("Icons are drawn in code; there are no image files.", true);
  ui.newline();
  if (footer(ui, w, h, {"Close"}) == 0) close_dialog();
}

// ---------------------------------------------------------------------------------------------------------------------
// properties
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::dialog_properties(Ui& ui, double w, double h) {
  const kit::Palette& p = ui.palette();
  std::lock_guard<std::mutex> lk(props_mu_);
  const bool multi = props_.type == "Multiple selection";
  cairo_save(cr_);
  cairo_translate(cr_, 20, 18);
  draw_icon(cr_, multi ? IconKind::File : (props_.is_dir ? IconKind::Folder : icon_for_file(props_.name, false)), 48);
  cairo_restore(cr_);
  kit::draw_text(cr_, fit(props_.name, w - 110, 16, true), 82, 42, 16, p.fg_primary, true);
  ui.set_margins(20, 80, 20);
  ui.set_label_width(130);
  ui.set_cursor_y(78);
  ui.set_cursor_x(20);
  auto kv = [&](const char* k, const std::string& v) {
    ui.row(k);
    ui.label(fit(v, w - 190, 13));
    ui.newline();
  };
  kv("Type", props_.type);
  if (!multi) kv("Location", props_.is_dir ? fs::path(props_.path).parent_path().string() : fs::path(props_.path).parent_path().string());
  if (props_.is_link) kv("Link target", props_.link_target);
  if (props_.is_dir || multi) {
    kv("Size", format_size_exact(props_size_.bytes));
    kv("Size on disk", format_size_exact(props_size_.on_disk));
    kv("Contains", std::to_string(props_size_.files) + " files, " + std::to_string(props_size_.folders) + " folders");
  } else {
    kv("Size", format_size_exact(props_.size));
    kv("Size on disk", format_size_exact(props_.on_disk));
  }
  if (!multi) {
    kv("Modified", format_date(static_cast<time_t>(props_.modified), s_.date_style));
    kv("Accessed", format_date(static_cast<time_t>(props_.accessed), s_.date_style));
    kv("Permissions", props_.mode_text);
    kv("Owner", props_.owner + " : " + props_.group);
    if (!props_.is_dir) {
      ui.space(8);
      if (props_hashes_.empty()) {
        if (ui.button("Calculate checksums")) {
          const std::string path = props_.path;
          props_hashes_.emplace_back("", "calculating...");
          spawn_loader([this, path] {
            std::vector<std::pair<std::string, std::string>> rows;
            for (HashAlgo a : {HashAlgo::Sha256, HashAlgo::Sha1, HashAlgo::Md5}) rows.emplace_back(hash_name(a), hash_file(path, a));
            post_([this, rows] {
              std::lock_guard<std::mutex> l(props_mu_);
              props_hashes_ = rows;
              schedule_redraw();
            });
          });
        }
        ui.newline();
      } else {
        for (const auto& r : props_hashes_) {
          ui.row(r.first.empty() ? "" : r.first.c_str());
          ui.label(fit(r.second, w - 190, 11));
          ui.newline();
        }
      }
    }
  }
  if (footer(ui, w, h, {"Close"}) == 0) close_dialog();
}

// ---------------------------------------------------------------------------------------------------------------------
// connect to server, Nextcloud
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::dialog_connect(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.set_label_width(130);
  ui.title("Connect to server");
  std::vector<std::string> names;
  static const std::vector<const char*> schemes = {"smb", "sftp", "ftp", "ftps", "davs", "dav", "nfs", "afp", "mtp"};
  for (const char* s : schemes) names.push_back(find_protocol(s)->name);
  ui.row("Type");
  ui.dropdown(names, &connect_proto_, 300);
  ui.newline();
  const std::string scheme = schemes[static_cast<size_t>(std::clamp(connect_proto_, 0, static_cast<int>(schemes.size()) - 1))];
  const Protocol* proto = find_protocol(scheme);
  ui.row("Server address");
  ui.text_entry(&connect_uri_, 300);
  ui.newline();
  ui.label(std::string("Example: ") + (scheme == "smb" ? "smb://nas/media" : (scheme == "sftp" ? "sftp://me@host/home/me" : (scheme == "davs" ? "davs://cloud.example.com/remote.php/dav/files/me/" : scheme + "://host/path"))), true);
  ui.newline();
  if (proto && proto->login) {
    ui.row("User name");
    ui.text_entry(&connect_user_, 220);
    ui.newline();
    ui.row("Password");
    ui.text_entry(&connect_pass_, 220, true, !connect_reveal_, &connect_reveal_);
    ui.newline();
    if (scheme == "smb") {
      ui.row("Domain");
      ui.text_entry(&connect_domain_, 220);
      ui.newline();
    }
    ui.checkbox("Connect anonymously (guest)", &connect_anon_);
    ui.newline();
  }
  ui.checkbox("Remember this place under Network", &connect_save_);
  ui.newline();
  if (connect_save_) {
    ui.row("Name");
    ui.text_entry(&connect_name_, 220);
    ui.newline();
  }
  if (!connect_error_.empty()) {
    ui.paragraph(fit(connect_error_, w - 60, 12), true);
  }
  if (connecting_) {
    ui.label("Connecting...", true);
    ui.newline();
  }
  const int hit = footer(ui, w, h, {"Cancel", "Connect"}, 1);
  if (hit == 0) close_dialog();
  if (hit == 1 && !connecting_) {
    std::string text = connect_uri_;
    if (text.find("://") == std::string::npos) text = scheme + "://" + text;
    Uri u = parse_uri(text);
    if (!u.valid || u.host.empty()) {
      connect_error_ = "That address cannot be understood. Use the form " + scheme + "://server/path.";
    } else {
      Credentials c;
      c.user = connect_user_.empty() ? u.user : connect_user_;
      c.password = connect_pass_;
      c.domain = connect_domain_;
      c.anonymous = connect_anon_;
      if (!c.user.empty()) u.user = c.user;
      connect_to(u, c, connect_name_, connect_save_);
    }
  }
}

void FmWindow::dialog_nextcloud(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.set_label_width(130);
  ui.title("Add a Nextcloud account");
  ui.paragraph("Nextcloud and ownCloud are reached over WebDAV. Create an app password in Nextcloud (Settings > Security) and use it here instead of your main password.");
  ui.row("Server");
  ui.text_entry(&nc_server_, 300);
  ui.newline();
  ui.row("User name");
  ui.text_entry(&nc_user_, 220);
  ui.newline();
  ui.row("App password");
  ui.text_entry(&nc_pass_, 220, true, !nc_reveal_, &nc_reveal_);
  ui.newline();
  const std::string preview = nextcloud_webdav_uri(nc_server_, nc_user_);
  ui.label(preview.empty() ? "Enter the server (cloud.example.com) and your user name." : fit(preview, w - 60, 12), true);
  ui.newline();
  if (!connect_error_.empty()) ui.paragraph(fit(connect_error_, w - 60, 12), true);
  if (connecting_) {
    ui.label("Connecting...", true);
    ui.newline();
  }
  const int hit = footer(ui, w, h, {"Cancel", "Add account"}, 1);
  if (hit == 0) close_dialog();
  if (hit == 1 && !connecting_ && !preview.empty()) {
    Uri u = parse_uri(preview);
    Credentials c;
    c.user = nc_user_;
    c.password = nc_pass_;
    connect_to(u, c, nc_user_ + " on " + u.host + " (Nextcloud)", true);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// conflicts, deleting, messages
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::dialog_conflict(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.title(conflict_.dest_is_dir ? "This location already has a folder with this name" : "There is already a file with the same name");
  const std::string name = fs::path(conflict_.destination).filename().string();
  ui.label(fit("'" + name + "' in " + fs::path(conflict_.destination).parent_path().filename().string(), w - 50, 13));
  ui.newline();
  ui.space(6);
  ui.label("Existing:  " + format_size(conflict_.dest_size) + ",  modified " + format_date(static_cast<time_t>(conflict_.dest_mtime), s_.date_style), true);
  ui.newline();
  ui.label("Copying:  " + format_size(conflict_.source_size) + ",  modified " + format_date(static_cast<time_t>(conflict_.source_mtime), s_.date_style), true);
  ui.newline();
  ui.space(6);
  ui.checkbox("Do this for all conflicts", &conflict_all_);
  ui.newline();
  const int hit = footer(ui, w, h, {"Replace", "Skip", "Keep both", "Cancel"}, 2);
  if (conflict_job_) {
    if (hit == 0) answer_conflict(conflict_job_, Conflict::Replace, conflict_all_);
    else if (hit == 1) answer_conflict(conflict_job_, Conflict::Skip, conflict_all_);
    else if (hit == 2) answer_conflict(conflict_job_, Conflict::KeepBoth, conflict_all_);
    else if (hit == 3) answer_conflict(conflict_job_, Conflict::Cancel, false);
  }
}

void FmWindow::dialog_confirm_delete(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  const size_t n = delete_paths_.size();
  const bool in_trash = tab().place == PlaceKind::Trash;
  ui.title(delete_permanent_ ? "Delete permanently?" : "Move to the Trash?");
  if (!dlg_text_.empty() && delete_permanent_) {
    ui.paragraph(dlg_text_, false);
  } else {
    std::string what = n == 1 ? "'" + fs::path(delete_paths_[0]).filename().string() + "'" : std::to_string(n) + " items";
    ui.paragraph(delete_permanent_ ? "Are you sure you want to permanently delete " + what + (in_trash ? " from the Trash" : "") + "? This cannot be undone." : "Are you sure you want to move " + what + " to the Trash?", false);
  }
  const int hit = footer(ui, w, h, {"Cancel", delete_permanent_ ? "Delete" : "Move to Trash"}, 1);
  if (hit == 0) {
    dlg_text_.clear();
    close_dialog();
  } else if (hit == 1) {
    dlg_text_.clear();
    close_dialog();
    do_delete(delete_paths_, delete_permanent_);
  }
}

void FmWindow::dialog_confirm_empty_trash(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.title("Empty the Trash?");
  ui.paragraph("All the items in the Trash will be deleted permanently. This cannot be undone.", false);
  const int hit = footer(ui, w, h, {"Cancel", "Empty the Trash"}, 1);
  if (hit == 0) close_dialog();
  if (hit == 1) {
    std::vector<std::string> mounts;
    for (const Volume& v : volumes_)
      if (v.mounted && v.kind != DriveKind::Network) mounts.push_back(v.mountpoint);
    const size_t n = trash_.empty(mounts);
    close_dialog();
    toast(std::to_string(n) + (n == 1 ? " item" : " items") + " deleted");
    if (tab().place == PlaceKind::Trash) load_current(true);
  }
}

void FmWindow::dialog_message(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.title(dlg_title_);
  ui.paragraph(dlg_text_, false);
  if (footer(ui, w, h, {"OK"}) == 0) close_dialog();
}

void FmWindow::dialog_checksums(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.title(checksum_running_ ? "Calculating checksums..." : "Checksums");
  ui.label(std::string(hash_label(resolve_algo(s_.verify_algo))) + " of each file, read from disk", true);
  ui.newline();
  ui.space(6);
  ui.begin_scroll({0, 90, w, h - 160}, &checksum_scroll_);
  for (const auto& r : checksum_rows_) {
    ui.label(fit(fs::path(r.second).filename().string(), w - 60, 13));
    ui.newline();
    ui.label(r.first, true);
    ui.newline();
    ui.space(4);
  }
  ui.end_scroll();
  ui.set_cursor_y(h - 100);
  ui.set_cursor_x(24);
  ui.row("Compare with");
  ui.text_entry(&compare_hash_, w - 200);
  ui.newline();
  if (!compare_hash_.empty()) {
    bool match = false;
    std::string want = compare_hash_;
    for (char& c : want)
      if (c >= 'A' && c <= 'F') c += 32;
    want.erase(std::remove(want.begin(), want.end(), ' '), want.end());
    for (const auto& r : checksum_rows_) match = match || r.first == want;
    ui.label(match ? "Matches one of the files above." : "Does not match any file above.", true);
  }
  if (footer(ui, w, h, {"Close"}) == 0) close_dialog();
}

void FmWindow::dialog_errors(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.title("Problems while copying");
  ui.begin_scroll({0, 70, w, h - 130}, &checksum_scroll_);
  for (const TransferError& e : err_rows_) {
    ui.label(fit(e.path, w - 60, 13));
    ui.newline();
    ui.label(fit(e.message, w - 60, 12), true);
    ui.newline();
    ui.space(4);
  }
  ui.end_scroll();
  if (footer(ui, w, h, {"Close"}) == 0) close_dialog();
}

}  // namespace fleetwm::fm
