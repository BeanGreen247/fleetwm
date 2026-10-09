// Context menus in the Windows 7 manner (Open, Open with >, Send to >, Cut, Copy, Create shortcut, Delete, Rename, Properties) carrying what
// the Linux file managers add: open in a new tab or window, open a terminal here, copy the location, compress and extract, new
// document, undo. Also: opening files with a chosen program, Send to, archives, undo, and the list of computers on the network.

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "default_apps.hpp"
#include "mimeapps.hpp"
#include "terminal_launch.hpp"
#include "window.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
MenuItem item(const std::string& label, Cmd c, int arg = 0, const std::string& sarg = "", const char* shortcut = "", bool enabled = true) {
  MenuItem m;
  m.label = label;
  m.cmd = c;
  m.arg = arg;
  m.sarg = sarg;
  m.shortcut = shortcut;
  m.enabled = enabled;
  return m;
}
MenuItem submenu(const std::string& label, std::vector<MenuItem> sub, bool enabled = true) {
  MenuItem m;
  m.label = label;
  m.sub = std::move(sub);
  m.enabled = enabled && !m.sub.empty();
  return m;
}
bool is_archive_name(const std::string& n) {
  const std::string e = extension_of(n);
  return e == "zip" || e == "tar" || e == "gz" || e == "tgz" || e == "xz" || e == "bz2" || e == "zst" || e == "7z" || e == "rar";
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::push_undo(std::string label, std::function<std::string()> fn) {
  undo_.push_back({std::move(label), std::move(fn)});
  if (undo_.size() > 50) undo_.erase(undo_.begin());
}

void FmWindow::undo_last() {
  if (undo_.empty()) {
    toast("Nothing to undo");
    return;
  }
  UndoOp op = std::move(undo_.back());
  undo_.pop_back();
  const std::string why = op.undo();
  if (why.empty()) {
    toast("Undid: " + op.label);
    load_current(true);
  } else {
    show_message("Undo", "Could not undo '" + op.label + "': " + why, true);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// open with, send to, terminal
// ---------------------------------------------------------------------------------------------------------------------

std::vector<MenuItem> FmWindow::open_with_items(const std::string& path, bool is_dir) {
  if (!apps_loaded_) {
    apps_ = kit::load_desktop_entries();
    apps_loaded_ = true;
  }
  open_with_list_.clear();
  std::vector<MenuItem> out;
  const std::string mime = mime_type_for(fs::path(path).filename().string(), is_dir);
  std::vector<std::string> ids = kit::mime_apps_for(mime);
  const std::string def = kit::mime_default_for(mime);
  if (!def.empty()) ids.insert(ids.begin(), def);
  std::vector<std::string> seen;
  for (const std::string& id : ids) {
    if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
    seen.push_back(id);
    for (const kit::DesktopEntry& e : apps_)
      if (e.id == id && open_with_list_.size() < 12) {
        out.push_back(item(e.name + (id == def ? " (default)" : ""), Cmd::OpenWithApp, static_cast<int>(open_with_list_.size()), path));
        open_with_list_.push_back(e);
      }
  }
  if (!out.empty()) out.push_back(MenuItem::sep());
  out.push_back(item("Other application...", Cmd::OpenWith, 0, path));
  return out;
}

std::vector<MenuItem> FmWindow::send_to_items() {
  std::vector<MenuItem> v;
  const std::string home = home_dir();
  for (const char* d : {"Desktop", "Documents", "Downloads"}) {
    std::error_code ec;
    if (fs::is_directory(fs::path(home) / d, ec)) v.push_back(item(d, Cmd::SendTo, 0, home + "/" + d));
  }
  for (const Volume& vol : volumes_)
    if (vol.mounted && vol.kind != DriveKind::Internal && !vol.mountpoint.empty()) v.push_back(item(volume_display_name(vol), Cmd::SendTo, 0, vol.mountpoint));
  return v;
}

void FmWindow::open_terminal_in(const std::string& dir) {
  const std::string command = load_default_apps_config().terminal_command;
  std::vector<std::string> words = split_command(command);
  if (words.empty()) words.push_back("foot");
  std::vector<std::string> argv = {"sh", "-c", "cd \"$1\" && shift && exec \"$@\"", "sh", dir};
  argv.insert(argv.end(), words.begin(), words.end());
  if (!spawn_detached(argv)) show_message("Open terminal", "The terminal '" + command + "' could not be started.", true);
}

// ---------------------------------------------------------------------------------------------------------------------
// archives: zip and tar through the usual command line tools, in the background
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::run_archive(bool extract, int kind, const std::vector<std::string>& paths) {
  if (paths.empty()) return;
  CommandRunner* r = runner ? runner : &system_runner();
  const std::string dir = fs::path(paths[0]).parent_path().string();
  std::vector<std::string> argv;
  std::string made;
  if (!extract) {
    const std::string stem = paths.size() == 1 ? fs::path(paths[0]).filename().string() : fs::path(dir).filename().string();
    const char* ext = kind == 0 ? ".zip" : (kind == 1 ? ".tar.gz" : ".tar.xz");
    made = dir + "/" + unique_name(dir, (stem.empty() ? "archive" : stem) + ext);
    if (kind == 0) {
      argv = {"sh", "-c", "d=$1; o=$2; shift 2; cd \"$d\" && exec zip -qr \"$o\" -- \"$@\"", "sh", dir, made};
    } else {
      argv = {"tar", kind == 1 ? "-czf" : "-cJf", made, "-C", dir};
    }
    for (const std::string& p : paths) argv.push_back(fs::path(p).filename().string());
    if (kind == 0) {
      // zip wants names relative to the folder; the sh wrapper changed into it
    }
  } else {
    const std::string name = fs::path(paths[0]).filename().string();
    std::string stem = name;
    for (const char* e : {".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tgz", ".zip", ".tar", ".7z", ".rar", ".gz", ".xz", ".bz2"})
      if (stem.size() > std::strlen(e) && stem.compare(stem.size() - std::strlen(e), std::strlen(e), e) == 0) {
        stem.resize(stem.size() - std::strlen(e));
        break;
      }
    made = kind == 0 ? dir : dir + "/" + unique_name(dir, stem);
    const std::string ext = extension_of(name);
    if (kind != 0) fs::create_directories(made);
    if (ext == "zip") argv = {"unzip", "-q", "-n", paths[0], "-d", made};
    else if (ext == "7z" || ext == "rar") argv = {"7z", "x", "-y", "-o" + made, paths[0]};
    else argv = {"tar", "-xf", paths[0], "-C", made};
  }
  toast(extract ? "Extracting..." : "Compressing...", 60);
  const std::string label = extract ? "Extract " + fs::path(paths[0]).filename().string() : "Compress " + std::to_string(paths.size()) + (paths.size() == 1 ? " item" : " items");
  spawn_loader([this, argv, r, made, extract, kind, label] {
    const RunResult rr = r->run(argv, "", 600000);
    post_([this, rr, made, extract, kind, label] {
      toast_.clear();
      if (rr.status != 0) {
        std::string why = rr.output;
        while (!why.empty() && (why.back() == '\n' || why.back() == ' ')) why.pop_back();
        show_message(extract ? "Extract" : "Compress", why.empty() ? "The tool failed (is it installed?)." : why, true);
        return;
      }
      toast(extract ? "Extracted" : "Compressed: " + fs::path(made).filename().string());
      if (!(extract && kind == 0)) {
        push_undo(label, [this, made]() -> std::string {
          std::string err;
          return trash_.put(made, &err) ? "" : err;
        });
      }
      load_current(true);
    });
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// the network: computers other machines announce (GVfs: mDNS and WS-Discovery), listed with `gio list network://`
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::scan_network() {
  if (lan_scanning_) return;
  lan_scanning_ = true;
  CommandRunner* r = runner ? runner : &system_runner();
  spawn_loader([this, r] {
    const RunResult rr = r->run({"gio", "list", "-u", "network:///"}, "", 12000);
    std::vector<std::pair<std::string, std::string>> hosts;
    std::stringstream ss(rr.output);
    std::string line;
    while (std::getline(ss, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line.empty() || line.find("://") == std::string::npos) continue;
      // network:// children are smb-server / dnssd links; their uri, with the scheme's host as the name
      const Uri u = parse_uri(line);
      hosts.emplace_back(u.valid && !u.host.empty() ? u.host : line, line);
    }
    post_([this, hosts = std::move(hosts), rr] {
      lan_scanning_ = false;
      lan_hosts_ = hosts;
      schedule_redraw();
    });
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// the menus
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::menu_for_selection(double x, double y) {
  Browser& b = tab();
  const int n = b.selected_count();
  std::vector<MenuItem> v;
  if (b.place == PlaceKind::Trash) {
    v.push_back(item("Restore", Cmd::Restore, 0, "", "", n > 0));
    v.push_back(MenuItem::sep());
    v.push_back(item("Delete permanently", Cmd::DeletePermanent, 0, "", "Shift+Del", n > 0));
    open_menu(std::move(v), x, y);
    return;
  }
  const std::vector<int> sel = b.selected();
  const bool one = n == 1;
  const bool one_dir = one && b.is_dir_at(sel[0]);
  const bool all_files = [&] {
    for (int i : sel)
      if (b.is_dir_at(i)) return false;
    return true;
  }();
  const std::string first_path = n ? b.path_at(sel[0]) : std::string();
  const bool writable = b.place == PlaceKind::Local || b.place == PlaceKind::Remote;
  MenuItem open = item(one_dir ? "Open" : "Open", Cmd::Open, 0, "", "Enter");
  open.bold = true;
  v.push_back(open);
  if (one_dir) {
    v.push_back(item("Open in new tab", Cmd::OpenInNewTab));
    v.push_back(item("Open in new window", Cmd::OpenInNewWindow));
    v.push_back(item("Open in terminal", Cmd::OpenTerminal, 0, first_path));
  } else if (one) {
    std::vector<MenuItem> ow = open_with_items(first_path, false);
    v.push_back(submenu("Open with", std::move(ow)));
  }
  v.push_back(MenuItem::sep());
  if (writable) v.push_back(submenu("Send to", send_to_items()));
  v.push_back(MenuItem::sep());
  v.push_back(item("Cut", Cmd::Cut, 0, "", "Ctrl+X", writable));
  v.push_back(item("Copy", Cmd::Copy, 0, "", "Ctrl+C"));
  v.push_back(item("Copy location", Cmd::CopyLocation, 0, "", ""));
  if (one_dir) v.push_back(item("Paste into folder", Cmd::Paste, 1, "", "", writable));
  v.push_back(MenuItem::sep());
  if (writable) v.push_back(item("Create shortcut", Cmd::CreateShortcut));
  v.push_back(item("Delete", Cmd::Delete, 0, "", "Del"));
  v.push_back(item("Rename", Cmd::Rename, 0, "", "F2", one && writable));
  v.push_back(MenuItem::sep());
  if (writable) {
    std::vector<MenuItem> comp = {item("Zip file (.zip)", Cmd::Compress, 0, "", "", have_program("zip")), item("Tar gzip (.tar.gz)", Cmd::Compress, 1, "", "", have_program("tar")),
                                  item("Tar xz (.tar.xz)", Cmd::Compress, 2, "", "", have_program("tar"))};
    v.push_back(submenu("Compress to", std::move(comp)));
    if (one && all_files && is_archive_name(b.label_at(sel[0])))
      v.push_back(submenu("Extract", {item("Extract here", Cmd::Extract, 0), item("Extract to \"" + b.label_at(sel[0]) + "/\"", Cmd::Extract, 1)}));
    v.push_back(MenuItem::sep());
  }
  v.push_back(item("Calculate checksums...", Cmd::Checksums, 0, "", "", all_files));
  v.push_back(item("Properties", Cmd::Properties, 0, "", "Alt+Enter"));
  open_menu(std::move(v), x, y);
}

void FmWindow::menu_for_background(double x, double y) {
  Browser& b = tab();
  std::vector<MenuItem> v;
  if (b.place == PlaceKind::Trash) {
    v.push_back(item("Empty the Trash", Cmd::EmptyTrash, 0, "", "", !b.shown.empty()));
    open_menu(std::move(v), x, y);
    return;
  }
  const bool writable = b.place == PlaceKind::Local || b.place == PlaceKind::Remote;
  v.push_back(submenu("View", view_items()));
  v.push_back(submenu("Sort by", sort_items()));
  v.push_back(submenu("Group by", group_items()));
  v.push_back(item("Refresh", Cmd::Reload, 0, "", "F5"));
  v.push_back(MenuItem::sep());
  v.push_back(item("Paste", Cmd::Paste, 0, "", "Ctrl+V", writable));
  v.push_back(item("Undo" + (undo_.empty() ? std::string() : " " + undo_.back().label), Cmd::Undo, 0, "", "Ctrl+Z", !undo_.empty()));
  v.push_back(MenuItem::sep());
  if (writable) {
    v.push_back(submenu("New", {item("Folder", Cmd::NewFolder, 0, "", "Ctrl+Shift+N"), item("Text document", Cmd::NewFolder, 1)}));
    v.push_back(MenuItem::sep());
    v.push_back(item("Open in terminal", Cmd::OpenTerminal, 0, b.path));
    v.push_back(item("Open in new tab", Cmd::OpenInNewTab, 0, b.path));
  }
  v.push_back(item("Properties", Cmd::Properties, 0, "", "Alt+Enter"));
  open_menu(std::move(v), x, y);
}

}  // namespace fleetwm::fm
