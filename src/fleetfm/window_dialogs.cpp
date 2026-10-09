#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <filesystem>

#include "file_ops.hpp"
#include "mimeapps.hpp"
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

}  // namespace

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

void FmWindow::dialog_open_with(Ui& ui, double w, double h) {
  ui.set_margins(24, 16, 24);
  ui.set_label_width(90);
  ui.title("Open with");
  if (!apps_loaded_) {
    apps_ = kit::load_desktop_entries();
    apps_loaded_ = true;
  }
  const std::string file = fs::path(open_with_path_).filename().string();
  ui.label(fit(file, w - 60, 13), true);
  ui.newline();
  ui.row("Find");
  const std::string before = open_with_filter_;
  ui.text_entry(&open_with_filter_, w - 140);
  ui.newline();
  if (open_with_filter_ != before) open_with_sel_ = 0;
  // Applications whose name contains what was typed (case blind), alphabetical; the list shows nine at a time, typing narrows it.
  auto lower = [](std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  const std::string needle = lower(open_with_filter_);
  std::vector<int> hits;
  for (size_t i = 0; i < apps_.size(); ++i)
    if (needle.empty() || lower(apps_[i].name).find(needle) != std::string::npos) hits.push_back(static_cast<int>(i));
  std::sort(hits.begin(), hits.end(), [&](int a, int b) { return lower(apps_[static_cast<size_t>(a)].name) < lower(apps_[static_cast<size_t>(b)].name); });
  std::vector<std::string> names;
  const size_t shown = std::min<size_t>(hits.size(), 9);
  for (size_t i = 0; i < shown; ++i) names.push_back(apps_[static_cast<size_t>(hits[i])].name);
  if (open_with_sel_ >= static_cast<int>(names.size())) open_with_sel_ = 0;
  const double top = ui.cursor_y();
  if (names.empty()) {
    ui.paragraph("No application matches.", true);
  } else {
    ui.nav(names, &open_with_sel_, {16, top, w - 32, 9 * 32.0});
  }
  ui.set_cursor_y(top + 9 * 32.0 + 6);
  if (hits.size() > shown) {
    ui.label(std::to_string(hits.size() - shown) + " more: type to narrow the list", true);
    ui.newline();
  }
  const std::string mime = mime_type_for(file, false);
  ui.checkbox("Always open " + mime + " files with this application", &open_with_always_, !names.empty());
  ui.newline();
  const int hit = footer(ui, w, h, {"Cancel", "Open"}, 1);
  if (hit == 0) close_dialog();
  if (hit == 1 && !names.empty()) {
    const kit::DesktopEntry& e = apps_[static_cast<size_t>(hits[static_cast<size_t>(open_with_sel_)])];
    std::vector<std::string> argv = kit::exec_argv(e);
    argv.push_back(open_with_path_);
    if (open_with_always_) kit::mime_set_default(mime, e.id);
    close_dialog();
    if (!spawn_detached(argv)) show_message("Open with", "The program could not be started.", true);
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
