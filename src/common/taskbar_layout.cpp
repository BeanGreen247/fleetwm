#include "taskbar_layout.hpp"

#include <algorithm>

namespace fleetwm {

namespace {
struct Info {
  TbElement id;
  const char* name;
  const char* label;
};
// The default order is this table's order.
constexpr Info kInfo[kTbElementCount] = {
    {TbElement::Start, "start", "Start button"},
    {TbElement::Workspaces, "workspaces", "Workspaces"},
    {TbElement::Pinned, "pinned", "Pinned apps"},
    {TbElement::Windows, "windows", "Window list"},
    {TbElement::Metrics, "metrics", "CPU, memory, GPU and disk"},
    {TbElement::Tray, "tray", "Tray icons"},
    {TbElement::Layout, "layout", "Keyboard layout"},
    {TbElement::Volume, "volume", "Volume"},
    {TbElement::Network, "network", "Network"},
    {TbElement::Bluetooth, "bluetooth", "Bluetooth"},
    {TbElement::Mode, "mode", "Power mode"},
    {TbElement::Battery, "battery", "Battery"},
    {TbElement::Clock, "clock", "Clock"},
};
}  // namespace

const char* tb_element_name(TbElement e) { return kInfo[static_cast<int>(e)].name; }
const char* tb_element_label(TbElement e) { return kInfo[static_cast<int>(e)].label; }

bool tb_element_from_name(const std::string& name, TbElement* out) {
  for (const Info& i : kInfo)
    if (name == i.name) {
      *out = i.id;
      return true;
    }
  return false;
}

bool tb_is_main_zone(TbElement e) {
  return e == TbElement::Start || e == TbElement::Workspaces || e == TbElement::Pinned || e == TbElement::Windows;
}

std::vector<TbElement> tb_default_order() {
  std::vector<TbElement> out;
  for (const Info& i : kInfo) out.push_back(i.id);
  return out;
}

std::vector<TbElement> tb_normalize_order(const std::vector<std::string>& saved) {
  std::vector<TbElement> out;
  for (const std::string& n : saved) {
    TbElement e;
    if (tb_element_from_name(n, &e) && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
  }
  // A missing element goes right after the element that precedes it in the default order (or first when nothing precedes it).
  for (int d = 0; d < kTbElementCount; ++d) {
    const TbElement e = kInfo[d].id;
    if (std::find(out.begin(), out.end(), e) != out.end()) continue;
    auto at = out.begin();
    for (int p = d - 1; p >= 0; --p) {
      auto it = std::find(out.begin(), out.end(), kInfo[p].id);
      if (it != out.end()) {
        at = it + 1;
        break;
      }
    }
    out.insert(at, e);
  }
  return out;
}

std::vector<TbElement> tb_normalize_hidden(const std::vector<std::string>& saved) {
  std::vector<TbElement> out;
  for (const std::string& n : saved) {
    TbElement e;
    if (tb_element_from_name(n, &e) && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
  }
  return out;
}

std::vector<std::string> tb_names(const std::vector<TbElement>& list) {
  std::vector<std::string> out;
  for (TbElement e : list) out.push_back(tb_element_name(e));
  return out;
}

bool tb_move(std::vector<TbElement>* order, TbElement e, int delta) {
  auto it = std::find(order->begin(), order->end(), e);
  if (it == order->end() || delta == 0) return false;
  const bool zone = tb_is_main_zone(e);
  const int step = delta < 0 ? -1 : 1;
  auto j = it;
  while (true) {
    if ((step < 0 && j == order->begin()) || (step > 0 && j + 1 == order->end())) return false;
    j += step;
    if (tb_is_main_zone(*j) == zone) break;
  }
  std::iter_swap(it, j);
  return true;
}

std::vector<TbSlot> tb_layout(const std::vector<TbItem>& visible, const TbLayoutParams& p) {
  std::vector<TbItem> main, status;
  for (const TbItem& i : visible) (tb_is_main_zone(i.id) ? main : status).push_back(i);

  // The status zone, from the far end backwards, remembering where it starts.
  std::vector<TbSlot> out;
  double end = p.length - p.margin;
  std::vector<TbSlot> status_slots;
  for (auto it = status.rbegin(); it != status.rend(); ++it) {
    if (it->size <= 0) continue;
    status_slots.push_back({it->id, end - it->size, it->size});
    end -= it->size + p.gap;
  }
  std::reverse(status_slots.begin(), status_slots.end());
  const double status_start = status_slots.empty() ? p.length - p.margin + p.gap : status_slots.front().pos;
  const double room_end = status_start - p.gap;  // the main zone must end before this

  // The main zone. Fixed items first (everything but the window list), to know what is left for it.
  double fixed = 0;
  int shown = 0;
  bool has_windows = false;
  double windows_want = 0;
  for (const TbItem& i : main) {
    if (i.id == TbElement::Windows) {
      has_windows = true;
      windows_want = i.size;
      continue;
    }
    if (i.size <= 0) continue;
    fixed += i.size;
    ++shown;
  }
  const double gaps_without_windows = shown > 0 ? (shown - 1) * p.gap : 0;
  double windows = 0;
  if (has_windows && windows_want > 0) {
    const double left = room_end - p.margin - fixed - gaps_without_windows - (shown > 0 ? p.gap : 0);
    windows = (p.centered_start || !p.windows_fill) ? std::min(windows_want, left) : left;
    if (windows < p.min_windows) windows = 0;
  }
  double x = p.margin;
  if (p.centered_start) {
    const double total = fixed + gaps_without_windows + (windows > 0 ? windows + (shown > 0 ? p.gap : 0) : 0);
    x = std::max(p.margin, (p.length - total) / 2.0);
    x = std::min(x, std::max(p.margin, room_end - total));  // never over the status zone
  }
  for (const TbItem& i : main) {
    const double size = i.id == TbElement::Windows ? windows : i.size;
    if (size <= 0) continue;
    out.push_back({i.id, x, size});
    x += size + p.gap;
  }
  out.insert(out.end(), status_slots.begin(), status_slots.end());
  return out;
}

}  // namespace fleetwm
