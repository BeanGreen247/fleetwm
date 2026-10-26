#include "settings_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fleetwm {

namespace {

std::string catalog_lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct IndexEntry {
  const char* title;
  int page;
  const char* keywords;
};

// One line per setting the window shows, plus the words people look for. Kept next to the pages' own labels (apps/settings).
const IndexEntry kIndex[] = {
    {"Theme", kPageTheme, "dark light mode colour color appearance"},
    {"Accent color", kPageTheme, "accent colour highlight"},
    {"Window layout", kPageTheme, "floating tiling desktop windows"},
    {"Glass effects (Windows Aero)", kPageTheme, "aero translucent transparency blur"},
    {"Window shadows", kPageTheme, "shadow drop"},
    {"Glass tint", kPageTheme, "colour intensity"},
    {"Focus border", kPageTheme, "border outline active window"},
    {"Gap between windows", kPageTheme, "gaps spacing tiling"},
    {"Titlebar height", kPageTheme, "title bar caption size"},
    {"Window buttons position", kPageTheme, "minimize maximize close buttons left right"},
    {"Taskbar position", kPageTaskbar, "bar top bottom left right panel"},
    {"Virtual desktops", kPageTaskbar, "workspaces"},
    {"Window buttons on the taskbar", kPageTaskbar, "task list titles icons"},
    {"Start menu in the middle", kPageTaskbar, "centre center windows 11"},
    {"Hide the taskbar until the pointer arrives", kPageTaskbar, "autohide auto-hide"},
    {"Media applet", kPageTaskbar, "music player waveform"},
    {"Pinned apps", kPageTaskbar, "add an app launcher shortcuts"},
    {"Clock format", kPageTaskbar, "time 12 24 hour date"},
    {"Wallpaper", kPageWallpaper, "background picture image desktop"},
    {"Solid colour background", kPageWallpaper, "solid color"},
    {"Screen resolution", kPageDisplay, "display size monitor"},
    {"Refresh rate", kPageDisplay, "hz hertz monitor"},
    {"Primary display", kPageDisplay, "main monitor screens multiple"},
    {"Display position", kPageDisplay, "arrange extend left right monitors"},
    {"Wi-Fi", kPageNetwork, "wifi wireless network internet connect radio"},
    {"Ethernet", kPageNetwork, "wired network cable lan"},
    {"VPN", kPageNetwork, "tunnel connection"},
    {"Saved networks", kPageNetwork, "forget password hotspot"},
    {"Bluetooth", kPageBluetooth, "pair device headphones wireless"},
    {"Visible to other devices", kPageBluetooth, "discoverable"},
    {"Keyboard layouts", kPageKeyboard, "language input us german czech"},
    {"Switching layouts", kPageKeyboard, "shortcut alt shift"},
    {"Repeat speed and delay", kPageKeyboard, "typing key repeat"},
    {"Pointer speed", kPageMouse, "mouse cursor sensitivity"},
    {"Enhance pointer precision", kPageMouse, "acceleration"},
    {"Natural scrolling", kPageMouse, "touchpad reverse wheel"},
    {"Primary mouse button", kPageMouse, "left right handed swap"},
    {"Pointer shapes", kPageMouse, "cursor arrow"},
    {"Sleep and display", kPagePower, "screen off lock suspend idle timeout"},
    {"Power mode", kPagePower, "performance balanced power saver battery"},
    {"Battery", kPagePower, "charge level laptop"},
    {"Time zone", kPageDateTime, "timezone region city clock"},
    {"Set the time automatically", kPageDateTime, "ntp network time sync"},
    {"Default terminal", kPageDefaultApps, "console shell"},
    {"Default browser", kPageDefaultApps, "web internet"},
    {"Default applications", kPageDefaultApps, "open with file types mime pdf image video music mail"},
    {"Master volume", kPageAudio, "sound speakers loudness mute"},
    {"Speaker test", kPageAudio, "sound check"},
    {"Application volume", kPageAudio, "per app mixer"},
    {"Render mode", kPagePerformance, "gpu software pixman gles fps frame rate"},
    {"Frame rate limit", kPagePerformance, "fps lock"},
    {"Performance overlay", kPagePerformance, "stats counters debug"},
    {"File manager", kPageFileManager, "files folders explorer fleetwm-fm view icons list"},
    {"Desktop icons", kPageDesktopIcons, "shortcuts trash home computer"},
    {"VNC server", kPageRemote, "remote desktop screen sharing guacamole viewer"},
    {"RDP server", kPageRemote, "remote desktop windows mstsc"},
    {"Remote desktop password", kPageRemote, "keep permanent own"},
    {"Remote desktop frame rate", kPageRemote, "fps quality bandwidth limit lag"},
    {"Version", kPageAbout, "about fleetwm build release"},
    {"Back up settings", kPageAbout, "export import restore backup"},
    {"Display name", kPageAccount, "user profile account name"},
    {"Profile picture", kPageAccount, "avatar icon image"},
};

}  // namespace

const std::vector<SettingsPageInfo>& settings_pages() {
  static const std::vector<SettingsPageInfo> kPages = {
      {"Home", "Every setting in one place.", "preferences-system", -1},
      {"Theme", "Colours, window frames, glass effects, borders and gaps.", "preferences-desktop-theme", 2},
      {"Taskbar", "Where the taskbar sits, what it shows and how it behaves.", "preferences-desktop-panel", 2},
      {"Wallpaper", "The picture or colour behind your windows.", "preferences-desktop-wallpaper", 2},
      {"Display", "Resolution, refresh rate and the arrangement of your screens.", "preferences-desktop-display", 0},
      {"Network", "Wi-Fi, wired connections and saved networks.", "preferences-system-network", 1},
      {"Bluetooth", "Pair and manage wireless devices.", "bluetooth", 1},
      {"Keyboard", "Layouts, shortcuts and typing.", "preferences-desktop-keyboard", 3},
      {"Mouse", "Pointer speed, buttons, scrolling and pointer shapes.", "input-mouse", 3},
      {"Power", "Sleep, the display and the power mode.", "preferences-system-power", 0},
      {"Date & Time", "Time zone and automatic time.", "preferences-system-time", 0},
      {"Default Apps", "Which program opens which kind of file.", "preferences-desktop-default-applications", 4},
      {"Audio", "Volume, speakers and per-application sound.", "audio-card", 3},
      {"Performance", "How the screen is drawn and what it costs.", "utilities-system-monitor", 0},
      {"File Manager", "How the file manager looks and works.", "system-file-manager", 4},
      {"Desktop Icons", "The shortcuts on your desktop.", "user-desktop", 2},
      {"Remote Desktop", "Reach this desktop from another computer over VNC or RDP.", "preferences-desktop-remote-desktop", 1},
      {"About", "Version, licence and backup of all your settings.", "help-about", 0},
      {"Account", "Your name and picture.", "avatar-default", 5},
  };
  return kPages;
}

const std::vector<SettingsCategory>& settings_categories() {
  static const std::vector<SettingsCategory> kCategories = {
      {"System", "Display, power, time, performance and the backup of your settings.", "preferences-system", 0x3B82F6,
       {kPageDisplay, kPagePower, kPageDateTime, kPagePerformance, kPageAbout}},
      {"Network & Remote", "Connections, Bluetooth and access from other computers.", "preferences-system-network", 0x10B981,
       {kPageNetwork, kPageBluetooth, kPageRemote}},
      {"Personalization", "Theme, wallpaper, taskbar and desktop icons.", "preferences-desktop-theme", 0xA855F7,
       {kPageTheme, kPageWallpaper, kPageTaskbar, kPageDesktopIcons}},
      {"Hardware & Sound", "Keyboard, mouse and audio.", "preferences-desktop-peripherals", 0xF59E0B,
       {kPageKeyboard, kPageMouse, kPageAudio}},
      {"Programs & Files", "Default applications and the file manager.", "applications-other", 0x14B8A6,
       {kPageDefaultApps, kPageFileManager}},
      {"User Account", "Your name and picture.", "system-users", 0x64748B, {kPageAccount}},
  };
  return kCategories;
}

const std::vector<SettingsTask>& settings_tasks(int category) {
  static const std::vector<std::vector<SettingsTask>> kTasks = {
      {{"Change the screen resolution", kPageDisplay}, {"Choose a power mode", kPagePower}, {"Back up your settings", kPageAbout}},
      {{"Connect to a Wi-Fi network", kPageNetwork}, {"Pair a Bluetooth device", kPageBluetooth}, {"Turn on remote access", kPageRemote}},
      {{"Change the theme colours", kPageTheme}, {"Change the wallpaper", kPageWallpaper}, {"Customize the taskbar", kPageTaskbar}},
      {{"Adjust the pointer speed", kPageMouse}, {"Change the keyboard layout", kPageKeyboard}, {"Set the speaker volume", kPageAudio}},
      {{"Choose the default applications", kPageDefaultApps}, {"Change how files are shown", kPageFileManager}},
      {{"Change your name or picture", kPageAccount}},
  };
  static const std::vector<SettingsTask> kNone;
  return category >= 0 && category < static_cast<int>(kTasks.size()) ? kTasks[static_cast<size_t>(category)] : kNone;
}

std::vector<SearchHit> search_settings(const std::string& query, size_t max) {
  std::vector<std::string> words;
  {
    std::istringstream in(catalog_lower(query));
    std::string w;
    while (in >> w) words.push_back(w);
  }
  std::vector<SearchHit> hits;
  if (words.empty()) return hits;
  const auto& pages = settings_pages();
  for (const IndexEntry& e : kIndex) {
    const std::string title = catalog_lower(e.title), keys = catalog_lower(e.keywords), page = catalog_lower(pages[static_cast<size_t>(e.page)].name);
    int score = 0;
    bool all = true;
    for (const std::string& w : words) {
      int best = 0;
      if (title == w) best = 100;
      else if (title.rfind(w, 0) == 0) best = 80;
      else if (title.find(w) != std::string::npos) best = 60;
      else if (keys.find(w) != std::string::npos) best = 40;
      else if (page.find(w) != std::string::npos) best = 20;
      if (!best) {
        all = false;
        break;
      }
      score += best;
    }
    if (all) hits.push_back({e.title, e.page, score});
  }
  // A page whose own name matches is a result too, so "mouse" finds the Mouse page itself.
  for (size_t p = 1; p < pages.size(); ++p) {
    const std::string name = catalog_lower(pages[p].name);
    bool all = true;
    int score = 0;
    for (const std::string& w : words) {
      if (name == w) score += 110;
      else if (name.rfind(w, 0) == 0) score += 90;
      else if (name.find(w) != std::string::npos) score += 70;
      else {
        all = false;
        break;
      }
    }
    if (all) hits.push_back({pages[p].name, static_cast<int>(p), score});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
  // The same title on the same page once.
  std::vector<SearchHit> out;
  for (const SearchHit& h : hits) {
    if (std::any_of(out.begin(), out.end(), [&](const SearchHit& o) { return o.page == h.page && catalog_lower(o.title) == catalog_lower(h.title); })) continue;
    out.push_back(h);
    if (out.size() >= max) break;
  }
  return out;
}

namespace {
std::filesystem::path ui_state_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  const char* home = std::getenv("HOME");
  std::filesystem::path base = state && *state ? std::filesystem::path(state) : std::filesystem::path(home && *home ? home : "/tmp") / ".local" / "state";
  return base / "fleetwm" / "settings-ui.txt";
}
}  // namespace

SettingsUiState load_settings_ui_state() {
  SettingsUiState s;
  std::ifstream f(ui_state_path());
  std::string line;
  while (std::getline(f, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    if (key == "view") {
      s.view = val == "large" ? HomeView::LargeIcons : val == "small" ? HomeView::SmallIcons : HomeView::Categories;
    } else if (key == "recent") {
      std::istringstream in(val);
      std::string tok;
      while (std::getline(in, tok, ',')) {
        char* end = nullptr;
        const long p = std::strtol(tok.c_str(), &end, 10);
        if (end != tok.c_str() && p > 0 && p < kPageCount) note_recent_page(s.recent, static_cast<int>(p), 8);
      }
      std::reverse(s.recent.begin(), s.recent.end());  // note_recent_page puts each at the front; the file lists most recent first
    }
  }
  return s;
}

void save_settings_ui_state(const SettingsUiState& s) {
  try {
    const auto path = ui_state_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    f << "view=" << (s.view == HomeView::LargeIcons ? "large" : s.view == HomeView::SmallIcons ? "small" : "categories") << "\n";
    f << "recent=";
    for (size_t i = 0; i < s.recent.size(); ++i) f << (i ? "," : "") << s.recent[i];
    f << "\n";
  } catch (const std::exception&) {
    // a state file that cannot be written only means the next start forgets the recent pages
  }
}

void note_recent_page(std::vector<int>& recent, int page, size_t keep) {
  if (page <= 0 || page >= kPageCount) return;
  recent.erase(std::remove(recent.begin(), recent.end(), page), recent.end());
  recent.insert(recent.begin(), page);
  if (recent.size() > keep) recent.resize(keep);
}

}  // namespace fleetwm
