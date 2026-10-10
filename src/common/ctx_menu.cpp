#include "ctx_menu.hpp"

#include <algorithm>
#include <sstream>

#include "menu_metrics.hpp"
#include "popup_spot.hpp"

namespace fleetwm {

std::optional<MenuItem> parse_menu_item(const std::string& text) {
  MenuItem it;
  if (text == "-") return it;  // separator
  const size_t a = text.find('|');
  if (a == std::string::npos || a == 0) return std::nullopt;
  const size_t b = text.find('|', a + 1);
  if (b == std::string::npos) return std::nullopt;
  it.label = text.substr(0, a);
  const std::string kind = text.substr(a + 1, b - a - 1);
  it.arg = text.substr(b + 1);
  if (it.arg.empty()) return std::nullopt;
  if (kind == "exec") it.kind = MenuItem::Kind::Exec;
  else if (kind == "ipc") it.kind = MenuItem::Kind::Ipc;
  else if (kind == "pin") it.kind = MenuItem::Kind::Pin;
  else if (kind == "unpin") it.kind = MenuItem::Kind::Unpin;
  else return std::nullopt;
  return it;
}

std::string format_menu_item(const MenuItem& item) {
  const char* kind = "exec";
  switch (item.kind) {
    case MenuItem::Kind::Separator: return "-";
    case MenuItem::Kind::Exec: kind = "exec"; break;
    case MenuItem::Kind::Ipc: kind = "ipc"; break;
    case MenuItem::Kind::Pin: kind = "pin"; break;
    case MenuItem::Kind::Unpin: kind = "unpin"; break;
  }
  return item.label + "|" + kind + "|" + item.arg;
}

std::vector<std::string> split_exec_arg(const std::string& arg) {
  std::vector<std::string> out;
  std::istringstream in(arg);
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

bool apply_pin(BarConfig* config, const std::string& id, bool pin) {
  auto& list = config->pinned_apps;
  const auto it = std::find(list.begin(), list.end(), id);
  if (pin) {
    if (it != list.end() || id.empty()) return false;
    list.push_back(id);
    return true;
  }
  if (it == list.end()) return false;
  list.erase(it);
  return true;
}

MenuSpot ctx_menu_spot(TaskbarPosition edge, int along, int menu_w, int menu_h, int screen_w, int screen_h, int gap, int screen_edge) {
  MenuSpot s;
  auto clamp_to = [&](int v, int size, int limit) { return std::clamp(v - size / 2, screen_edge, std::max(screen_edge, limit - size - screen_edge)); };
  switch (edge) {
    case TaskbarPosition::Top:
      s.anchor = kAnchorTop | kAnchorLeft;
      s.top = kTaskbarThickness + gap;
      s.left = clamp_to(along, menu_w, screen_w);
      break;
    case TaskbarPosition::Left:
      s.anchor = kAnchorTop | kAnchorLeft;
      s.left = kTaskbarWidth + gap;
      s.top = clamp_to(along, menu_h, screen_h);
      break;
    case TaskbarPosition::Right:
      s.anchor = kAnchorTop | kAnchorRight;
      s.right = kTaskbarWidth + gap;
      s.top = clamp_to(along, menu_h, screen_h);
      break;
    case TaskbarPosition::Bottom:
    default:
      s.anchor = kAnchorBottom | kAnchorLeft;
      s.bottom = kTaskbarThickness + gap;
      s.left = clamp_to(along, menu_w, screen_w);
      break;
  }
  return s;
}

MenuSize ctx_menu_size(const std::vector<MenuItem>& items, int widest_label_px) {
  int h = static_cast<int>(2 * menu::kOuterPadding);
  for (const MenuItem& i : items)
    h += static_cast<int>(i.kind == MenuItem::Kind::Separator ? menu::kSeparatorHeight : menu::kRowHeight);
  return {std::max(160, widest_label_px + static_cast<int>(2 * menu::kTextPadding) + 2), h};
}

}  // namespace fleetwm
