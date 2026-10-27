#include "gpu_policy.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fleetwm {

namespace fs = std::filesystem;

bool is_discrete(const DrmCard& card, bool has_other_boot_card) {
  if (card.driver == "nvidia" || card.driver == "nouveau") return true;
  return has_other_boot_card && !card.boot_vga;
}

std::string wlr_drm_devices_for(DesktopGpu policy, const std::vector<DrmCard>& cards) {
  if (policy == DesktopGpu::Default || cards.size() < 2) return "";
  std::vector<DrmCard> ordered = cards;
  const bool boot_card = std::any_of(cards.begin(), cards.end(), [](const DrmCard& c) { return c.boot_vga; });
  auto discrete = [&](const DrmCard& c) { return is_discrete(c, boot_card); };
  const bool want_discrete = policy == DesktopGpu::Discrete;
  const bool any_discrete = std::any_of(cards.begin(), cards.end(), discrete);
  const bool any_other = std::any_of(cards.begin(), cards.end(), [&](const DrmCard& c) { return !discrete(c); });
  if (!any_discrete || !any_other) return "";  // two cards of one kind: nothing to prefer
  std::stable_partition(ordered.begin(), ordered.end(), [&](const DrmCard& c) { return discrete(c) == want_discrete; });
  std::string out;
  for (const DrmCard& c : ordered) out += (out.empty() ? "" : ":") + c.path;
  return out;
}

std::vector<DrmCard> scan_drm_cards(const std::string& sys_class_drm) {
  std::vector<DrmCard> cards;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(sys_class_drm, ec)) {
    const std::string name = e.path().filename().string();
    if (name.size() < 5 || name.compare(0, 4, "card") != 0 || name.find('-') != std::string::npos) continue;
    DrmCard c;
    c.path = "/dev/dri/" + name;
    const fs::path dev = e.path() / "device";
    const fs::path driver = fs::read_symlink(dev / "driver", ec);
    if (!ec) c.driver = driver.filename().string();
    std::ifstream bv(dev / "boot_vga");
    int v = 0;
    if (bv >> v) c.boot_vga = v == 1;
    cards.push_back(c);
  }
  std::sort(cards.begin(), cards.end(), [](const DrmCard& a, const DrmCard& b) { return a.path < b.path; });
  return cards;
}

}  // namespace fleetwm
