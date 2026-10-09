#include "fm_settings_ui.hpp"

#include <map>

namespace fleetwm::fm {

namespace {
std::vector<std::string> style_names() {
  std::vector<std::string> v;
  for (const StyleSpec& s : all_styles()) v.push_back(s.name);
  return v;
}
}  // namespace

const std::vector<std::string>& fm_settings_pages() {
  static const std::vector<std::string> pages = {"General", "View", "Tabs", "Search", "Copy and verify", "Drives and network", "Appearance", "Privacy"};
  return pages;
}

// Rows in the manner of the Settings app: the label at the left, the control in the column to its right.
FmSettingsResult draw_fm_settings_page(kit::Ui& ui, FmSettings& s, int page) {
  FmSettingsResult r;
  bool changed = false;
  auto on_off = [&](const char* label, bool* v) {
    if (ui.checkbox(label, v)) changed = true;
    ui.newline();
  };
  auto choice = [&](const char* label, const std::vector<std::string>& opts, int* idx, double wd = 220) {
    ui.row(label);
    if (ui.dropdown(opts, idx, wd)) changed = true;
    ui.newline();
  };
  switch (page) {
    case 0: {
      ui.section("Look");
      int st = static_cast<int>(s.style);
      ui.row("Style");
      if (ui.dropdown(style_names(), &st, 220)) {
        apply_style(&s, static_cast<ViewStyle>(st));
        r.style_changed = changed = true;
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
        r.view_changed = true;
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
        r.regroup = changed = true;
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
        on_off(it->second.c_str(), &c.visible);
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
      if (ui.button("Connect to server...")) r.connect = true;
      ui.same_line();
      if (ui.button("Add Nextcloud...")) r.nextcloud = true;
      ui.newline();
      break;
    }
    case 6: {
      ui.section("Colours");
      ui.paragraph("The file manager follows these live. 'Follow the Fleetwm theme' uses the colours of Settings > Theme.", true);
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
        r.style_changed = changed = true;
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
        r.clear_history = true;
      }
      ui.newline();
      break;
    }
  }
  r.changed = changed;
  return r;
}

bool draw_desktop_settings(kit::Ui& ui, DesktopConfig& c) {
  bool changed = false;
  auto on_off = [&](const char* label, bool* v) {
    if (ui.checkbox(label, v)) changed = true;
    ui.newline();
  };
  ui.section("Desktop icons (Desktop layout)");
  on_off("Show desktop icons", &c.show_icons);
  int sz = static_cast<int>(c.icon_size);
  ui.row("Icon size");
  if (ui.dropdown({"Small", "Medium", "Large"}, &sz, 180)) {
    c.icon_size = static_cast<DesktopIconSize>(sz);
    changed = true;
  }
  ui.newline();
  on_off("Auto arrange icons", &c.auto_arrange);
  on_off("Align icons to grid", &c.align_to_grid);
  int sk = c.sort_key == SortKey::Name ? 0 : (c.sort_key == SortKey::Size ? 1 : (c.sort_key == SortKey::Type ? 2 : 3));
  ui.row("Sort icons by");
  if (ui.dropdown({"Name", "Size", "Item type", "Date modified"}, &sk, 180)) {
    c.sort_key = sk == 0 ? SortKey::Name : (sk == 1 ? SortKey::Size : (sk == 2 ? SortKey::Type : SortKey::Modified));
    c.cells.clear();
    changed = true;
  }
  ui.newline();
  ui.section("Icons shown");
  on_off("Computer", &c.show_computer);
  on_off("Home folder", &c.show_home);
  on_off("Trash", &c.show_trash);
  ui.section("Tiling layout");
  on_off("Show the shortcut hint card", &c.show_hint);
  ui.paragraph("In the Tiling layout the desktop has no icons and no menu; a small card in the corner shows which keys open the shortcut list.", true);
  return changed;
}

}  // namespace fleetwm::fm
