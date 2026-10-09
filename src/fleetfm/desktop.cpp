#include "desktop.hpp"

#include <toml++/toml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

#include "config_paths.hpp"
#include "natural_sort.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

std::string desktop_config_path() { return (config_internal::config_home() / "fleetwm" / "desktop.toml").string(); }

int desktop_icon_px(DesktopIconSize s) { return s == DesktopIconSize::Small ? 32 : (s == DesktopIconSize::Large ? 64 : 48); }

DesktopConfig load_desktop_config() {
  DesktopConfig c;
  std::error_code ec;
  if (!fs::exists(desktop_config_path(), ec)) return c;
  try {
    toml::table t = toml::parse_file(desktop_config_path());
    if (auto v = t["show_icons"].value<bool>()) c.show_icons = *v;
    if (auto v = t["auto_arrange"].value<bool>()) c.auto_arrange = *v;
    if (auto v = t["align_to_grid"].value<bool>()) c.align_to_grid = *v;
    if (auto v = t["show_hint"].value<bool>()) c.show_hint = *v;
    if (auto v = t["show_computer"].value<bool>()) c.show_computer = *v;
    if (auto v = t["show_home"].value<bool>()) c.show_home = *v;
    if (auto v = t["show_trash"].value<bool>()) c.show_trash = *v;
    if (auto v = t["ascending"].value<bool>()) c.ascending = *v;
    if (auto v = t["icon_size"].value<std::string>()) c.icon_size = *v == "small" ? DesktopIconSize::Small : (*v == "large" ? DesktopIconSize::Large : DesktopIconSize::Medium);
    if (auto v = t["sort"].value<std::string>()) c.sort_key = *v == "size" ? SortKey::Size : (*v == "modified" ? SortKey::Modified : (*v == "type" ? SortKey::Type : SortKey::Name));
    if (auto* cells = t["cells"].as_table())
      for (auto&& [k, v] : *cells)
        if (auto* a = v.as_array(); a && a->size() == 2) {
          const auto col = (*a)[0].value<int64_t>(), row = (*a)[1].value<int64_t>();
          if (col && row) c.cells[std::string(k.str())] = {static_cast<int>(*col), static_cast<int>(*row)};
        }
  } catch (const toml::parse_error&) {
    return DesktopConfig{};
  }
  return c;
}

void save_desktop_config(const DesktopConfig& c) {
  const fs::path path = desktop_config_path();
  fs::create_directories(path.parent_path());
  toml::table t;
  t.insert_or_assign("show_icons", c.show_icons);
  t.insert_or_assign("auto_arrange", c.auto_arrange);
  t.insert_or_assign("align_to_grid", c.align_to_grid);
  t.insert_or_assign("show_hint", c.show_hint);
  t.insert_or_assign("show_computer", c.show_computer);
  t.insert_or_assign("show_home", c.show_home);
  t.insert_or_assign("show_trash", c.show_trash);
  t.insert_or_assign("ascending", c.ascending);
  t.insert_or_assign("icon_size", c.icon_size == DesktopIconSize::Small ? "small" : (c.icon_size == DesktopIconSize::Large ? "large" : "medium"));
  t.insert_or_assign("sort", c.sort_key == SortKey::Size ? "size" : (c.sort_key == SortKey::Modified ? "modified" : (c.sort_key == SortKey::Type ? "type" : "name")));
  toml::table cells;
  for (const auto& [k, v] : c.cells) {
    toml::array a;
    a.push_back(static_cast<int64_t>(v.first));
    a.push_back(static_cast<int64_t>(v.second));
    cells.insert_or_assign(k, std::move(a));
  }
  t.insert_or_assign("cells", std::move(cells));
  const std::string tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("cannot write " + tmp);
    out << t;
    if (!out) throw std::runtime_error("cannot write " + tmp);
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
}

namespace {
// Name= of a .desktop launcher, "" when there is none.
std::string launcher_name(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  bool in_entry = false;
  while (std::getline(f, line)) {
    if (line == "[Desktop Entry]") in_entry = true;
    else if (!line.empty() && line[0] == '[') in_entry = false;
    else if (in_entry && line.compare(0, 5, "Name=") == 0) return line.substr(5);
  }
  return {};
}
}  // namespace

std::vector<DesktopIcon> list_desktop_icons(const std::string& desktop_dir, const std::string& home, const DesktopConfig& c) {
  std::vector<DesktopIcon> out;
  auto special = [&](const char* key, const char* label, const char* target, IconKind k) {
    DesktopIcon i;
    i.key = key;
    i.label = label;
    i.target = target;
    i.icon = k;
    i.special = true;
    i.is_dir = true;
    out.push_back(i);
  };
  if (c.show_computer) special("computer", "Computer", "computer:///", IconKind::Computer);
  if (c.show_home) {
    special("home", fs::path(home).filename().string().empty() ? "Home" : fs::path(home).filename().string().c_str(), home.c_str(), IconKind::Home);
    out.back().label = fs::path(home).filename().string().empty() ? "Home" : fs::path(home).filename().string();
  }
  if (c.show_trash) special("trash", "Trash", "trash:///", IconKind::Trash);
  DirListing l;
  if (list_dir(desktop_dir, {true, false}, &l)) {
    sort_listing(&l, c.sort_key, c.ascending, true);
    for (const Entry& e : l.entries) {
      DesktopIcon i;
      const std::string name(l.name(e));
      i.key = name;
      i.label = name;
      i.target = desktop_dir + "/" + name;
      i.is_dir = l.is_dir(e);
      i.icon = i.is_dir ? icon_for_folder_name(name) : icon_for_file(name, false);
      i.mtime = e.mtime;
      i.size = e.size;
      if (!i.is_dir && extension_of(name) == "desktop") {
        const std::string n = launcher_name(i.target);
        if (!n.empty()) i.label = n;
        i.icon = IconKind::Executable;
      }
      out.push_back(std::move(i));
    }
  }
  return out;
}

DesktopGrid desktop_grid(int w, int h, DesktopIconSize s) {
  DesktopGrid g;
  g.icon_px = desktop_icon_px(s);
  g.cell_w = g.icon_px + 44;
  g.cell_h = g.icon_px + 46;
  g.cols = std::max(1, (w - 2 * g.margin_x) / g.cell_w);
  g.rows = std::max(1, (h - 2 * g.margin_y) / g.cell_h);
  return g;
}

Pt desktop_cell_origin(const DesktopGrid& g, int col, int row) { return {g.margin_x + col * g.cell_w, g.margin_y + row * g.cell_h}; }

void place_desktop_icons(std::vector<DesktopIcon>* icons, const DesktopGrid& g, const DesktopConfig& c) {
  std::set<std::pair<int, int>> taken;
  int next = 0;  // next free cell in column-major order
  auto free_cell = [&]() {
    for (;; ++next) {
      const std::pair<int, int> cell{next / g.rows, next % g.rows};
      if (cell.first >= g.cols + 8) return cell;  // more icons than room: they pile up past the edge rather than overlap
      if (!taken.count(cell)) {
        ++next;
        return cell;
      }
    }
  };
  if (c.auto_arrange) {
    for (DesktopIcon& i : *icons) {
      const auto cell = free_cell();
      taken.insert(cell);
      i.col = cell.first;
      i.row = cell.second;
    }
    return;
  }
  // Manual: remembered cells first, so a new icon cannot take a place that belongs to another one.
  std::vector<DesktopIcon*> unplaced;
  for (DesktopIcon& i : *icons) {
    auto it = c.cells.find(i.key);
    if (it != c.cells.end() && it->second.first >= 0 && it->second.second >= 0 && it->second.first < g.cols && it->second.second < g.rows && !taken.count(it->second)) {
      i.col = it->second.first;
      i.row = it->second.second;
      taken.insert(it->second);
    } else {
      unplaced.push_back(&i);
    }
  }
  for (DesktopIcon* i : unplaced) {
    const auto cell = free_cell();
    taken.insert(cell);
    i->col = cell.first;
    i->row = cell.second;
  }
}

int desktop_icon_at(const std::vector<DesktopIcon>& icons, const DesktopGrid& g, int x, int y) {
  for (int k = static_cast<int>(icons.size()) - 1; k >= 0; --k) {
    const Pt o = desktop_cell_origin(g, icons[k].col, icons[k].row);
    // the picture and the label, not the empty corners of the cell
    const int ix = o.x + (g.cell_w - g.icon_px) / 2 - 6, iy = o.y + 2;
    if (x >= ix && x < ix + g.icon_px + 12 && y >= iy && y < o.y + g.cell_h - 2 && x >= o.x + 4 && x < o.x + g.cell_w - 4) return k;
  }
  return -1;
}

std::vector<int> desktop_icons_in_rect(const std::vector<DesktopIcon>& icons, const DesktopGrid& g, int x0, int y0, int x1, int y1) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  std::vector<int> out;
  for (size_t k = 0; k < icons.size(); ++k) {
    const Pt o = desktop_cell_origin(g, icons[k].col, icons[k].row);
    if (o.x + 6 < x1 && o.x + g.cell_w - 6 > x0 && o.y + 2 < y1 && o.y + g.cell_h - 2 > y0) out.push_back(static_cast<int>(k));
  }
  return out;
}

std::pair<int, int> desktop_cell_at(const DesktopGrid& g, int x, int y) {
  const int col = std::clamp((x - g.margin_x) / g.cell_w, 0, g.cols - 1), row = std::clamp((y - g.margin_y) / g.cell_h, 0, g.rows - 1);
  return {col, row};
}

bool move_desktop_icon(const std::vector<DesktopIcon>& icons, const DesktopGrid&, DesktopConfig* c, const std::string& key, int col, int row) {
  for (const DesktopIcon& i : icons)
    if (i.key != key && i.col == col && i.row == row) return false;
  for (const DesktopIcon& i : icons) {  // freeze every icon where it is, so only this one moves
    if (!c->cells.count(i.key)) c->cells[i.key] = {i.col, i.row};
  }
  c->cells[key] = {col, row};
  return true;
}

std::vector<std::string> split_key_combo(const std::string& combo) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : combo) {
    if (ch == '+' && !cur.empty()) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

}  // namespace fleetwm::fm
