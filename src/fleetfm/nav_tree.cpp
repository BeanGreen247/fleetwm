#include "nav_tree.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
NavRow header_row(const std::string& label, const std::string& address = {}, const std::string& key = {}) {
  NavRow r;
  r.header = true;
  r.label = label;
  r.address = address;
  r.key = key;
  return r;
}

NavRow place(int depth, const std::string& label, IconKind icon, const std::string& address, bool expandable = false) {
  NavRow r;
  r.depth = depth;
  r.label = label;
  r.icon = icon;
  r.address = address;
  r.key = address;
  r.expandable = expandable;
  return r;
}

IconKind volume_icon(const Volume& v) {
  switch (v.kind) {
    case DriveKind::Removable: return v.disk.compare(0, 11, "/dev/mmcblk") == 0 ? IconKind::DriveCard : IconKind::DriveUsb;
    case DriveKind::Optical: return IconKind::DriveOptical;
    case DriveKind::Network: return IconKind::DriveNetwork;
    default: return IconKind::DriveInternal;
  }
}

std::string vol_address(const Volume& v) { return v.mounted ? v.mountpoint : "mount:" + v.device; }
}  // namespace

void NavTree::add_item(NavRow r) { rows_.push_back(std::move(r)); }

bool NavTree::toggle(const std::string& key) {
  auto it = std::find_if(rows_.begin(), rows_.end(), [&](const NavRow& r) { return r.key == key && r.expandable; });
  if (it == rows_.end()) return false;
  if (!expanded_.erase(key)) expanded_.insert(key);
  return true;
}

int NavTree::row_for_address(const std::string& a) const {
  int best = -1;
  size_t best_len = 0;
  for (size_t i = 0; i < rows_.size(); ++i) {
    const std::string& ra = rows_[i].address;
    if (ra.empty()) continue;
    if (ra == a) return static_cast<int>(i);
    // The deepest folder row that contains the open path.
    if (ra[0] == '/' && a.compare(0, ra.size(), ra) == 0 && (ra == "/" || a[ra.size()] == '/') && ra.size() > best_len) {
      best = static_cast<int>(i);
      best_len = ra.size();
    }
  }
  return best;
}

void NavTree::add_children(const NavInput& in, int depth, const std::string& path) {
  if (!in.subfolders) return;
  std::vector<std::string> names = in.subfolders(path);
  size_t shown = 0;
  for (const std::string& n : names) {
    if (++shown > 500) break;
    const std::string child = path == "/" ? "/" + n : path + "/" + n;
    NavRow r = place(depth, n, icon_for_folder_name(n), child, true);
    const bool on_path = in.settings->nav_expand_to_current && in.current.size() > child.size() && in.current.compare(0, child.size(), child) == 0 && in.current[child.size()] == '/';
    const bool open = is_expanded(child) || on_path;
    r.expanded = open;
    add_item(r);
    if (open) add_children(in, depth + 1, child);
  }
}

void NavTree::build(const NavInput& in) {
  rows_.clear();
  const FmSettings& s = *in.settings;
  const StyleSpec& sp = style_spec(in.style);
  const std::string home = in.home;
  auto exists_dir = [&](const std::string& name) {
    std::error_code ec;
    return fs::is_directory(fs::path(home) / name, ec);
  };
  auto user_dir = [&](const char* name, IconKind icon, int depth) {
    if (!exists_dir(name)) return;
    add_item(place(depth, name, icon, home + "/" + name, s.nav_show_all_folders));
  };
  const bool show_vol = true;

  if (sp.nav == NavKind::Tree) {
    // ---- Windows 7 / 10 tree ----
    if (s.nav_show_favorites) {
      add_item(header_row("Favorites"));
      user_dir("Desktop", IconKind::Desktop, 1);
      user_dir("Downloads", IconKind::Downloads, 1);
      if (s.show_recent_files) add_item(place(1, "Recent Places", IconKind::Recent, "recent:///"));
      add_item(place(1, "Home", IconKind::Home, home, s.nav_show_all_folders));
    }
    if (s.nav_show_libraries) {
      add_item(header_row("Libraries"));
      user_dir("Documents", IconKind::Documents, 1);
      user_dir("Music", IconKind::Music, 1);
      user_dir("Pictures", IconKind::Pictures, 1);
      user_dir("Videos", IconKind::Videos, 1);
    }
    NavRow computer = header_row("Computer", "computer:///", "computer:///");
    computer.icon = IconKind::Computer;
    computer.expandable = true;
    computer.expanded = true;
    add_item(computer);
    for (const Volume& v : in.volumes) {
      if (v.kind == DriveKind::Network && !s.show_network_in_this_pc) continue;
      if (!v.mounted && !s.show_unmounted_removable) continue;
      NavRow r = place(1, volume_display_name(v), volume_icon(v), vol_address(v), v.mounted);
      r.has_volume = true;
      r.volume = v;
      r.eject = v.ejectable || v.kind == DriveKind::Network;
      r.used_fraction = v.used_fraction();
      r.sub = volume_space_text(v);
      const bool open = is_expanded(r.key) || (s.nav_expand_to_current && v.mounted && in.current.compare(0, v.mountpoint.size(), v.mountpoint) == 0 &&
                                               (v.mountpoint == "/" || in.current.size() == v.mountpoint.size() || in.current[v.mountpoint.size()] == '/'));
      if (open && v.mounted) expanded_.insert(r.key);
      r.expanded = open && v.mounted;
      add_item(r);
      if (r.expanded) add_children(in, 2, v.mountpoint);
    }
    if (s.nav_show_network) {
      NavRow net = header_row("Network", "network:///", "network:///");
      net.icon = IconKind::Network;
      add_item(net);
      for (const SavedPlace& p : in.places) {
        NavRow r = place(1, p.name.empty() ? p.uri : p.name, IconKind::NetworkServer, p.uri);
        add_item(r);
      }
    }
    if (s.nav_show_trash) add_item(place(0, "Trash", IconKind::Trash, "trash:///"));
    return;
  }

  // ---- flat sidebar (Caja, Nemo, Nautilus, Thunar, PCManFM, Dolphin, Finder) ----
  const bool finder = in.style == ViewStyle::Mac;
  add_item(header_row(finder ? "Favorites" : "Places"));
  if (finder && s.show_recent_files) add_item(place(1, "Recents", IconKind::Recent, "recent:///"));
  add_item(place(1, finder ? "Home" : "Home", IconKind::Home, home));
  user_dir("Desktop", IconKind::Desktop, 1);
  user_dir("Documents", IconKind::Documents, 1);
  user_dir("Downloads", IconKind::Downloads, 1);
  user_dir("Music", IconKind::Music, 1);
  user_dir("Pictures", IconKind::Pictures, 1);
  user_dir("Videos", IconKind::Videos, 1);
  if (!finder && s.show_recent_files) add_item(place(1, "Recent", IconKind::Recent, "recent:///"));
  if (s.nav_show_trash && !finder) add_item(place(1, "Trash", IconKind::Trash, "trash:///"));
  if (show_vol) {
    add_item(header_row(finder ? "Locations" : "Devices"));
    add_item(place(1, in.style == ViewStyle::Windows10 ? "This PC" : "Computer", IconKind::Computer, "computer:///"));
    for (const Volume& v : in.volumes) {
      if (v.kind == DriveKind::Network) continue;
      if (!v.mounted && !s.show_unmounted_removable) continue;
      NavRow r = place(1, volume_display_name(v), volume_icon(v), vol_address(v));
      r.has_volume = true;
      r.volume = v;
      r.eject = v.ejectable;
      r.used_fraction = v.used_fraction();
      r.sub = volume_space_text(v);
      add_item(r);
    }
  }
  if (s.nav_show_network) {
    add_item(header_row("Network"));
    add_item(place(1, "Browse Network", IconKind::Network, "network:///"));
    for (const Volume& v : in.volumes) {
      if (v.kind != DriveKind::Network) continue;
      NavRow r = place(1, volume_display_name(v), IconKind::DriveNetwork, v.mountpoint);
      r.has_volume = true;
      r.volume = v;
      r.eject = true;
      add_item(r);
    }
    for (const SavedPlace& p : in.places) add_item(place(1, p.name.empty() ? p.uri : p.name, IconKind::NetworkServer, p.uri));
  }
  if (finder && s.nav_show_trash) add_item(place(1, "Trash", IconKind::Trash, "trash:///"));
}

}  // namespace fleetwm::fm
