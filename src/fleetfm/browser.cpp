#include "browser.hpp"
#include "grouping.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>

namespace fleetwm::fm {

namespace {
std::string lower_ascii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c += 32;
  return s;
}
}  // namespace

std::string Browser::address() const {
  switch (place) {
    case PlaceKind::Computer: return "computer:///";
    case PlaceKind::Network: return "network:///";
    case PlaceKind::Trash: return "trash:///";
    case PlaceKind::Recent: return "recent:///";
    case PlaceKind::Remote: return uri.empty() ? path : uri;
    default: return path;
  }
}

void Browser::remember_current(size_t keep) {
  const std::string a = address();
  forward.clear();
  if (keep == 0) {
    back.clear();
    return;
  }
  if (back.empty() || back.back() != a) back.push_back(a);
  while (back.size() > keep) back.erase(back.begin());
}

bool Browser::can_up() const { return !up_address().empty(); }

std::string Browser::up_address() const {
  if (place == PlaceKind::Local) {
    if (path == "/" || path.empty()) return "computer:///";
    const size_t slash = path.rfind('/');
    return slash == 0 ? "/" : path.substr(0, slash);
  }
  if (place == PlaceKind::Remote) {
    const Uri u = parse_uri(uri);
    if (!u.valid || u.path.empty() || u.path == "/") return "network:///";
    Uri p = u;
    const size_t slash = u.path.rfind('/', u.path.size() > 1 ? u.path.size() - 2 : 0);
    p.path = slash == std::string::npos || slash == 0 ? "/" : u.path.substr(0, slash);
    return uri_to_string(p);
  }
  if (place == PlaceKind::Computer) return {};
  return "computer:///";
}

std::string Browser::go_back() {
  if (back.empty()) return {};
  forward.push_back(address());
  const std::string a = back.back();
  back.pop_back();
  return a;
}

std::string Browser::go_forward() {
  if (forward.empty()) return {};
  back.push_back(address());
  const std::string a = forward.back();
  forward.pop_back();
  return a;
}

void Browser::set_listing(DirListing&& l) {
  std::set<std::string> keep;
  for (size_t i = 0; i < shown.size(); ++i)
    if (i < sel.size() && sel[i]) keep.insert(name_at(static_cast<int>(i)));
  std::string focus_name = focus >= 0 && focus < static_cast<int>(shown.size()) ? name_at(focus) : std::string();
  raw = std::move(l);
  shown.clear();
  sel.clear();
  rebuild();
  for (size_t i = 0; i < shown.size(); ++i)
    if (keep.count(name_at(static_cast<int>(i)))) sel[i] = 1;
  focus = -1;
  if (!focus_name.empty())
    for (size_t i = 0; i < shown.size(); ++i)
      if (name_at(static_cast<int>(i)) == focus_name) focus = static_cast<int>(i);
  anchor = focus;
}

void Browser::rebuild() {
  // Keep the selection across a re-sort by name.
  std::set<std::string> keep;
  for (size_t i = 0; i < shown.size(); ++i)
    if (i < sel.size() && sel[i]) keep.insert(name_at(static_cast<int>(i)));
  std::string focus_name = focus >= 0 && focus < static_cast<int>(shown.size()) ? name_at(focus) : std::string();
  shown.clear();
  const std::string f = lower_ascii(filter);
  for (const Entry& e : raw.entries) {
    if (e.hidden && !show_hidden) continue;
    if (!f.empty() && lower_ascii(std::string(raw.name(e))).find(f) == std::string::npos) continue;
    shown.push_back(e);
    shown.back().src = static_cast<uint32_t>(&e - raw.entries.data());
  }
  sort_entries(raw, &shown, sort_key, ascending, folders_first);
  group_starts.clear();
  group_labels.clear();
  if (group_by != GroupBy::None && !shown.empty()) {
    // Group order first (A before B, today before last year; the direction flips it), the chosen sort inside each group.
    const time_t now = std::time(nullptr);
    std::vector<GroupKey> keys(shown.size());
    for (size_t i = 0; i < shown.size(); ++i) {
      const std::string nm = label_at(static_cast<int>(i));
      const bool dir = raw.is_dir(shown[i]);
      keys[i] = group_of(group_by, nm, dir, shown[i].size, shown[i].mtime, now, group_by == GroupBy::Type ? (type_label ? type_label(nm, dir) : extension_of(nm)) : std::string());
    }
    std::vector<uint32_t> order(shown.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<uint32_t>(i);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return ascending ? keys[a].rank < keys[b].rank : keys[a].rank > keys[b].rank; });
    std::vector<Entry> grouped;
    grouped.reserve(shown.size());
    int last_rank = -1;
    std::string last_label;
    for (uint32_t i : order) {
      if (grouped.empty() || keys[i].rank != last_rank || keys[i].label != last_label) {
        group_starts.push_back(static_cast<int>(grouped.size()));
        group_labels.push_back(keys[i].label);
        last_rank = keys[i].rank;
        last_label = keys[i].label;
      }
      grouped.push_back(shown[i]);
    }
    shown.swap(grouped);
  }
  sel.assign(shown.size(), 0);
  focus = -1;
  for (size_t i = 0; i < shown.size(); ++i) {
    const std::string n = name_at(static_cast<int>(i));
    if (keep.count(n)) sel[i] = 1;
    if (n == focus_name) focus = static_cast<int>(i);
  }
  anchor = focus;
}

std::string Browser::path_at(int i) const {
  const std::string n = name_at(i);
  if (!n.empty() && n[0] == '/') return n;
  return path == "/" ? "/" + n : path + "/" + n;
}

std::string Browser::label_at(int i) const {
  const std::string n = name_at(i);
  const size_t slash = n.rfind('/');
  return slash == std::string::npos ? n : n.substr(slash + 1);
}

std::string Browser::folder_at(int i) const {
  const std::string n = name_at(i);
  const size_t slash = n.rfind('/');
  if (slash == std::string::npos) return {};
  return n[0] == '/' ? (slash == 0 ? "/" : n.substr(0, slash)) : n.substr(0, slash);
}

int Browser::selected_count() const { return static_cast<int>(std::count(sel.begin(), sel.end(), 1)); }

std::vector<int> Browser::selected() const {
  std::vector<int> out;
  for (size_t i = 0; i < sel.size(); ++i)
    if (sel[i]) out.push_back(static_cast<int>(i));
  return out;
}

uint64_t Browser::selected_bytes(bool* complete) const {
  uint64_t n = 0;
  if (complete) *complete = true;
  for (size_t i = 0; i < sel.size(); ++i)
    if (sel[i] && !raw.is_dir(shown[i])) {
      if (!shown[i].has_stat && complete) *complete = false;
      n += shown[i].size;
    }
  return n;
}

void Browser::ensure_stat(int i) {
  if (i < 0 || i >= static_cast<int>(shown.size()) || shown[i].has_stat || place == PlaceKind::Computer || place == PlaceKind::Network) return;
  Entry e = shown[i];
  const std::string n = name_at(i);
  const std::string dir = !n.empty() && n[0] == '/' ? std::string() : path;
  if (!stat_entry(dir, n, &e)) {
    shown[i].has_stat = true;  // gone; do not try again every frame
    return;
  }
  shown[i] = e;
  if (e.src < raw.entries.size()) {
    Entry& r = raw.entries[e.src];
    r.has_stat = true;
    r.mode = e.mode;
    r.size = e.size;
    r.mtime = e.mtime;
    r.kind = e.kind;
    r.link_to_dir = e.link_to_dir;
  }
}

void Browser::ensure_stat_range(int first, int last) {
  for (int i = std::max(0, first); i <= last && i < static_cast<int>(shown.size()); ++i) ensure_stat(i);
}

void Browser::clear_selection() { std::fill(sel.begin(), sel.end(), 0); }

void Browser::select_only(int i) {
  clear_selection();
  if (i >= 0 && i < static_cast<int>(sel.size())) sel[i] = 1;
  focus = anchor = i;
}

void Browser::toggle(int i) {
  if (i < 0 || i >= static_cast<int>(sel.size())) return;
  sel[i] = !sel[i];
  focus = anchor = i;
}

void Browser::select_to(int i) {
  if (i < 0 || i >= static_cast<int>(sel.size())) return;
  const int a = anchor < 0 ? i : anchor;
  clear_selection();
  for (int k = std::min(a, i); k <= std::max(a, i); ++k) sel[k] = 1;
  focus = i;
  anchor = a;
}

void Browser::select_all() { std::fill(sel.begin(), sel.end(), 1); }

void Browser::invert_selection() {
  for (char& c : sel) c = !c;
}

void Browser::select_indices(const std::vector<int>& idx, bool add) {
  if (!add) clear_selection();
  for (int i : idx)
    if (i >= 0 && i < static_cast<int>(sel.size())) sel[i] = 1;
}

void Browser::set_focus(int i) {
  if (i >= 0 && i < static_cast<int>(shown.size())) focus = i;
}

int Browser::type_ahead(const std::string& letter, double now) {
  if (now - typed_at > 1.0) typed.clear();
  typed_at = now;
  const bool same_letter_again = typed.size() == 1 && letter == typed;
  if (!same_letter_again) typed += letter;
  const int from = same_letter_again && focus >= 0 ? focus + 1 : (focus >= 0 && typed.size() > 1 ? focus : 0);
  const int hit = find_prefix(raw, shown, typed, from);
  if (hit >= 0) select_only(hit);
  return hit;
}

void Browser::fix_focus() {
  if (focus >= static_cast<int>(shown.size())) focus = static_cast<int>(shown.size()) - 1;
  if (anchor >= static_cast<int>(shown.size())) anchor = focus;
}

}  // namespace fleetwm::fm
