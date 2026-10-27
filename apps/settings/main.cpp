// fleetwm-settings: GTK-free settings window. A regular xdg_toplevel
// (app id dev.fleetwm.Settings, which the compositor floats, centres and keeps
// on top) drawn with cairo through fleetkit's immediate-mode Ui. Every change is
// saved immediately; the compositor, bar, wallpaper and clients pick the new
// config up live through their own inotify watches.

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <fstream>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

#include "quit_signals.hpp"
#include "audio_mixer.hpp"
#include "bar_config.hpp"
#include "config_backup.hpp"
#include "battery_reading.hpp"
#include "power_config.hpp"
#include "settings_catalog.hpp"
#include "settings_pages.hpp"
#include "icon_theme.hpp"
#include <unistd.h>
#include "default_apps.hpp"
#include "desktop.hpp"
#include "fm_settings_ui.hpp"
#include "terminal_launch.hpp"
#include "ipc_client.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "prewarm.hpp"
#include "cursor_draw.hpp"
#include "keyboard_tab.hpp"
#include "remote_tab.hpp"
#include "autostart.hpp"
#include "game_config.hpp"
#include "tuxthrottle.hpp"
#include "mouse_config.hpp"
#include "user_profile.hpp"
#include "bluetooth_tab.hpp"
#include "taskbar_layout.hpp"
#include "network_tab.hpp"
#include "mimeapps.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "ui.hpp"
#include "image.hpp"
#include "wallpaper_config.hpp"
#include "output_config.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kWindowW = 800, kWindowH = 620;
constexpr double kSidebarW = 210;
constexpr double kFont = 14.67;
// Runs argv (no shell), returns the exit status and its stdout+stderr. Used for
// timedatectl, which answers (or refuses) immediately thanks to --no-ask-password.
int run_capture(const std::vector<std::string>& argv, std::string* out) {
  int fds[2];
  if (pipe(fds) != 0) return -1;
  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    const int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) dup2(devnull, STDIN_FILENO);
    close(fds[0]);
    close(fds[1]);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(fds[1]);
  char buf[4096];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out->append(buf, static_cast<size_t>(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct TimeInfo {
  bool loaded = false;
  std::vector<std::string> regions;
  std::map<std::string, std::vector<std::string>> cities;  // region -> zone remainder
  int region_sel = 0, city_sel = 0;
  std::string system_tz;
  bool ntp = false, synced = false;
  std::string server;
  std::string status;  // result of the last action
};

constexpr int kKeepPos = -999999;  // OUTPUT_SET: leave the position unchanged

struct DispMode {
  int w = 0, h = 0, r = 0;
  bool cur = false, pref = false;
};

struct DispMon {
  std::string name;
  int x = 0, y = 0, w = 0, h = 0, r = 0;  // as reported by the compositor
  std::vector<DispMode> modes;
  // edit state
  std::vector<std::pair<int, int>> res;  // unique resolutions, largest first
  std::vector<std::string> res_labels;
  int res_sel = 0;
  std::vector<int> hz;  // refresh rates (mHz) for the selected resolution, highest first
  std::vector<std::string> hz_labels;
  int hz_sel = 0;
  int ex = 0, ey = 0;  // edited position
  int rel_sel = 0, side_sel = 0, align_sel = 0;
};

std::string hz_text(int mhz) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2f Hz", mhz / 1000.0);
  std::string s = buf;
  // "60.00 Hz" -> "60 Hz"
  const size_t dot = s.find(".00 ");
  if (dot != std::string::npos) s.erase(dot, 3);
  return s;
}

void rebuild_mon(DispMon& m, bool keep_edit_selection) {
  const int prev_w = keep_edit_selection && m.res_sel < static_cast<int>(m.res.size()) ? m.res[static_cast<size_t>(m.res_sel)].first : m.w;
  const int prev_h = keep_edit_selection && m.res_sel < static_cast<int>(m.res.size()) ? m.res[static_cast<size_t>(m.res_sel)].second : m.h;
  m.res.clear();
  for (const auto& md : m.modes) {
    const std::pair<int, int> r{md.w, md.h};
    if (std::find(m.res.begin(), m.res.end(), r) == m.res.end()) m.res.push_back(r);
  }
  std::sort(m.res.begin(), m.res.end(), [](const auto& a, const auto& b) {
    return a.first * a.second != b.first * b.second ? a.first * a.second > b.first * b.second : a.first > b.first;
  });
  m.res_labels.clear();
  m.res_sel = 0;
  for (size_t i = 0; i < m.res.size(); ++i) {
    m.res_labels.push_back(std::to_string(m.res[i].first) + " x " + std::to_string(m.res[i].second));
    if (m.res[i].first == prev_w && m.res[i].second == prev_h) m.res_sel = static_cast<int>(i);
  }
  m.hz.clear();
  m.hz_labels.clear();
  m.hz_sel = 0;
  if (m.res.empty()) return;
  const auto sel = m.res[static_cast<size_t>(m.res_sel)];
  for (const auto& md : m.modes)
    if (md.w == sel.first && md.h == sel.second) m.hz.push_back(md.r);
  std::sort(m.hz.begin(), m.hz.end(), std::greater<int>());
  for (size_t i = 0; i < m.hz.size(); ++i) {
    m.hz_labels.push_back(hz_text(m.hz[i]));
    if (std::abs(m.hz[i] - m.r) <= 500 && sel.first == m.w && sel.second == m.h) m.hz_sel = static_cast<int>(i);
  }
}

struct MimeRow {
  const char* label;
  const char* mime;
  std::vector<int> apps;  // indices into Settings::entries
  int selected = -1;
};

struct Settings {
  ~Settings() {
    if (profile_surface) cairo_surface_destroy(profile_surface);
    for (auto& [name, surf] : icon_cache)
      if (surf) cairo_surface_destroy(surf);
  }
  App app;
  Palette pal;
  Ui ui{Palette{}};
  std::unique_ptr<Surface> surface;

  ThemeConfig config;
  BarConfig bar;
  WallpaperConfig wallpaper;
  DefaultAppsConfig default_apps;

  int tab = 0;
  double scroll[24] = {};
  std::vector<std::string> tab_names = [] {
    std::vector<std::string> names;
    for (const fleetwm::SettingsPageInfo& pg : fleetwm::settings_pages()) names.push_back(pg.name);
    return names;
  }();
  // The look of the window: Windows 11 Settings (search, recent, a profile card), macOS System Settings (grouped sidebar, page descriptions)
  // and the classic Control Panel (category view with task links, View by, back and forward).
  fleetwm::SettingsUiState ui_state;
  std::string search;                    // the header's "Find a setting" box
  std::vector<int> history{0};           // pages visited, for back and forward
  size_t hist_pos = 0;
  std::map<std::string, cairo_surface_t*> icon_cache;
  std::vector<int> nav_pages;            // the page of each sidebar entry, -1 for a group heading
  std::string host_name;
  UserProfile profile;
  std::string profile_draft_name;

  // network
  std::unique_ptr<NetworkTab> net_tab;
  std::unique_ptr<BluetoothTab> bt_tab;
  std::unique_ptr<KeyboardTab> kb_tab;
  std::unique_ptr<RemoteTab> remote_tab;

  // power
  bool has_battery = false;
  std::string battery_dir;
  BatteryReading battery;
  PowerConfig power;
  MouseConfig mouse;
  GameConfig game;
  std::set<std::string> startup_on;     // ids of the enabled autostart entries
  std::vector<size_t> startup_order;    // `entries` sorted by name, built on first view
  bool startup_loaded = false;
  fleetwm::TuxThrottleStatus tux;       // TuxThrottle's readout, read when the Gaming page opens and on Refresh
  bool tux_loaded = false;
  fleetwm::fm::FmSettings fm_cfg;       // fleetwm-fm: fleetfm.toml
  fleetwm::fm::DesktopConfig desktop_cfg;  // fleetwm-desktop: desktop.toml
  int fm_page = 0;

  // default apps
  std::vector<DesktopEntry> entries;
  std::vector<MimeRow> mime_rows;
  std::vector<int> terminal_apps;
  int terminal_selected = -1;

  TimeInfo tinfo;

  // display
  std::vector<DispMon> mons;
  DisplaySettings display;
  std::string disp_status;

  // backup (About tab)
  std::string backup_path = std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/fleetwm-config.tar.gz";
  std::string backup_status;
  bool backup_all = false;
  IpcClient ipc;
  int ipc_watch = 0;
  bool disp_requested = false;
  std::vector<DispMon> collecting;
  bool in_list = false;
  bool confirm_active = false;
  int confirm_left = 0, confirm_timer = 0;
  DispMon confirm_prev;
  int drag_mon = -1;
  double drag_off_x = 0, drag_off_y = 0;
  std::vector<std::string> side_names{"Right of", "Left of", "Below", "Above"};
  std::vector<std::string> align_names{"Start", "Center", "End"};

  // audio
  common::AudioMixer mixer;
  bool master_available = false, master_muted = false;
  int master_value = 0;
  std::vector<common::AudioStream> streams;
  std::string speaker_test_status;
  int mouse_test_timer = 0;
  int mouse_test_remaining = 0;
  uint64_t mouse_test_events = 0;
  double mouse_test_rate = 0;
  std::string profile_image_loaded;
  std::vector<uint8_t> profile_pixels;
  cairo_surface_t* profile_surface = nullptr;

  // ------------------------------------------------------------ helpers --
  void redraw() { surface->queue_draw(); }

  void apply_theme() {
    pal = load_palette(config);
    ui.set_palette(pal);
  }

  void save_theme() {
    save_theme_config(config);
    apply_theme();
  }
  void save_bar() { save_bar_config(bar); }
  void save_wallpaper() { save_wallpaper_config(wallpaper); }

  // Follows changes made outside this window (another tool, a hand edit, the
  // wallpaper's auto accent) so what is shown always matches the files, in any
  // window layout. Our own saves land here too and simply re-read what was written.
  void reload_from_disk() {
    config = load_theme_config();
    bar = load_bar_config();
    wallpaper = load_wallpaper_config();
    default_apps = load_default_apps_config();
    power = load_power_config();
    mouse = load_mouse_config();
    profile = load_user_profile();
    profile_draft_name = profile.display_name;
    fm_cfg = fleetwm::fm::load_fm_settings();
    desktop_cfg = fleetwm::fm::load_desktop_config();
    if (kb_tab) kb_tab->reload();
    apply_theme();
    redraw();
  }

  // Shortens a label at the end ("Change the screen reso...") so it fits `max_w` px at `px` pixels.
  std::string fit_text(cairo_t* cr, std::string s, double max_w, double px, bool bold = false) {
    if (measure_text(cr, s, px, bold).width <= max_w) return s;
    while (s.size() > 1 && measure_text(cr, s + "...", px, bold).width > max_w) {
      s.pop_back();
      while (s.size() > 1 && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();  // not in the middle of a UTF-8 character
    }
    return s + "...";
  }

  // Shortens a path in the middle so it fits `max_w` px.
  std::string ellipsize(cairo_t* cr, const std::string& s, double max_w) {
    if (measure_text(cr, s, kFont).width <= max_w) return s;
    std::string head = s.substr(0, s.size() / 2), tail = s.substr(s.size() / 2);
    while (!head.empty() && !tail.empty() &&
           measure_text(cr, head + "..." + tail, kFont).width > max_w) {
      if (head.size() > tail.size()) head.pop_back();
      else tail.erase(0, 1);
    }
    return head + "..." + tail;
  }

  void load_apps() {
    entries = load_desktop_entries();
    std::sort(entries.begin(), entries.end(),
              [](const DesktopEntry& a, const DesktopEntry& b) { return a.name < b.name; });
    static const struct {
      const char* label;
      const char* mime;
    } kCats[] = {{"Web Browser", "x-scheme-handler/https"}, {"File Manager", "inode/directory"},
                 {"Image Viewer", "image/png"},             {"Text Editor", "text/plain"},
                 {"Video Player", "video/mp4"},             {"PDF Viewer", "application/pdf"},
                 {"Archive Manager", "application/zip"}};
    mime_rows.clear();
    for (const auto& c : kCats) {
      MimeRow row{c.label, c.mime, {}, -1};
      const auto ids = mime_apps_for(c.mime);
      const std::string def = mime_default_for(c.mime);
      for (const auto& id : ids) {
        for (size_t i = 0; i < entries.size(); ++i)
          if (entries[i].id == id) {
            if (id == def) row.selected = static_cast<int>(row.apps.size());
            row.apps.push_back(static_cast<int>(i));
            break;
          }
      }
      mime_rows.push_back(std::move(row));
    }
    terminal_apps.clear();
    int exact_match = -1, executable_match = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
      const std::string& cats = entries[i].categories;
      size_t pos = 0;
      bool is_term = false;
      while (pos <= cats.size()) {
        size_t e = cats.find(';', pos);
        if (e == std::string::npos) e = cats.size();
        if (cats.substr(pos, e - pos) == "TerminalEmulator") is_term = true;
        pos = e + 1;
      }
      if (!is_term) continue;
      const auto argv = exec_argv(entries[i]);
      const int index = static_cast<int>(terminal_apps.size());
      // Two entries can share one program ("Lestrix" and "Lestrix Lite" = lestrix --lite): the whole command line says which was picked.
      if (exact_match < 0 && split_command(default_apps.terminal_command) == argv) exact_match = index;
      if (executable_match < 0 && !argv.empty() && default_apps.terminal_command == argv[0]) executable_match = index;
      terminal_apps.push_back(static_cast<int>(i));
    }
    // An exact command-line match wins over a mere executable match.
    terminal_selected = exact_match >= 0 ? exact_match : executable_match;
  }

  void update_battery() {
    battery = battery_internal::read_battery_reading(battery_dir);
    redraw();
  }

  // ---------------------------------------------------------- date & time --
  static std::string trim_nl(std::string v) {
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
    return v;
  }

  void load_time_info() {
    tinfo = TimeInfo{};
    tinfo.loaded = true;
    std::string out;
    run_capture({"timedatectl", "show", "-p", "Timezone", "-p", "NTP", "-p", "NTPSynchronized"}, &out);
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string k = line.substr(0, eq), v = trim_nl(line.substr(eq + 1));
      if (k == "Timezone") tinfo.system_tz = v;
      else if (k == "NTP") tinfo.ntp = v == "yes";
      else if (k == "NTPSynchronized") tinfo.synced = v == "yes";
    }
    out.clear();
    run_capture({"timedatectl", "timesync-status"}, &out);
    std::istringstream in2(out);
    while (std::getline(in2, line)) {
      const size_t p = line.find("Server:");
      if (p != std::string::npos) tinfo.server = trim_nl(line.substr(p + 7));
      while (!tinfo.server.empty() && tinfo.server.front() == ' ') tinfo.server.erase(0, 1);
    }
    // zone list
    std::vector<std::string> zones;
    out.clear();
    if (run_capture({"timedatectl", "list-timezones"}, &out) == 0) {
      std::istringstream zs(out);
      while (std::getline(zs, line)) {
        line = trim_nl(line);
        if (!line.empty()) zones.push_back(line);
      }
    }
    if (zones.empty()) {  // no systemd: read the tz database index directly
      std::ifstream tab("/usr/share/zoneinfo/zone1970.tab");
      while (std::getline(tab, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream f(line);
        std::string cc, coord, tz;
        if (f >> cc >> coord >> tz) zones.push_back(tz);
      }
      std::sort(zones.begin(), zones.end());
    }
    for (const auto& z : zones) {
      const size_t slash = z.find('/');
      const std::string region = slash == std::string::npos ? z : z.substr(0, slash);
      const std::string city = slash == std::string::npos ? z : z.substr(slash + 1);
      if (!tinfo.cities.count(region)) tinfo.regions.push_back(region);
      tinfo.cities[region].push_back(city);
    }
    select_zone(effective_timezone());
  }

  std::string effective_timezone() const {
    return !bar.clock.timezone.empty() ? bar.clock.timezone : tinfo.system_tz;
  }

  void select_zone(const std::string& tz) {
    const size_t slash = tz.find('/');
    const std::string region = slash == std::string::npos ? tz : tz.substr(0, slash);
    const std::string city = slash == std::string::npos ? tz : tz.substr(slash + 1);
    for (size_t i = 0; i < tinfo.regions.size(); ++i)
      if (tinfo.regions[i] == region) {
        tinfo.region_sel = static_cast<int>(i);
        const auto& cs = tinfo.cities[region];
        tinfo.city_sel = 0;
        for (size_t j = 0; j < cs.size(); ++j)
          if (cs[j] == city) tinfo.city_sel = static_cast<int>(j);
      }
  }

  std::string selected_zone() {
    if (tinfo.regions.empty()) return "";
    const std::string& region = tinfo.regions[static_cast<size_t>(tinfo.region_sel)];
    const auto& cs = tinfo.cities[region];
    if (cs.empty()) return region;
    const std::string& city = cs[static_cast<size_t>(std::min<int>(tinfo.city_sel, static_cast<int>(cs.size()) - 1))];
    return city == region ? region : region + "/" + city;
  }

  void apply_timezone() {
    const std::string tz = selected_zone();
    if (tz.empty()) return;
    std::string out;
    const int rc = run_capture({"timedatectl", "--no-ask-password", "set-timezone", tz}, &out);
    if (rc == 0) {
      bar.clock.timezone.clear();  // the system zone now says it all
      tinfo.system_tz = tz;
      tinfo.status = "System time zone set to " + tz + ".";
    } else {
      bar.clock.timezone = tz;  // still show the chosen zone on the bar clock
      tinfo.status = "Bar clock set to " + tz + ". Changing the system time zone needs administrator rights (sudo timedatectl set-timezone " + tz + ").";
    }
    save_bar();
  }

  void set_ntp(bool on) {
    std::string out;
    const int rc = run_capture({"timedatectl", "--no-ask-password", "set-ntp", on ? "true" : "false"}, &out);
    if (rc == 0) {
      tinfo.ntp = on;
      tinfo.status = on ? "Automatic time is on." : "Automatic time is off.";
    } else {
      tinfo.status = "Could not change automatic time: " + trim_nl(out);
    }
    // re-read the real state
    const std::string keep = tinfo.status;
    load_time_info();
    tinfo.status = keep;
  }

  void tab_datetime(cairo_t*) {
    if (!tinfo.loaded) load_time_info();

    ui.space(8);
    ui.section("Time zone");
    ui.label("Current: " + (effective_timezone().empty() ? std::string("unknown") : effective_timezone()), true);
    ui.newline();
    if (!tinfo.regions.empty()) {
      ui.row("Region");
      if (ui.dropdown(tinfo.regions, &tinfo.region_sel, 200)) tinfo.city_sel = 0;
      ui.newline();
      const std::string& region = tinfo.regions[static_cast<size_t>(tinfo.region_sel)];
      ui.row("City");
      ui.dropdown(tinfo.cities[region], &tinfo.city_sel, 260);
      ui.newline();
      ui.row("");
      if (ui.button("Set time zone", true, true)) apply_timezone();
      ui.newline();
    }

    ui.space(8);
    ui.section("Network time");
    bool ntp = tinfo.ntp;
    if (ui.checkbox("Set the time automatically from the nearest time server", &ntp)) set_ntp(ntp);
    ui.newline();
    ui.label(std::string("Clock synchronized: ") + (tinfo.synced ? "yes" : "no") +
                 (tinfo.server.empty() ? "" : "   Server: " + tinfo.server),
             true);
    ui.newline();
    ui.paragraph("Uses the system time service (systemd-timesyncd), which picks servers from the public NTP pool closest to you.");
    if (!tinfo.status.empty()) ui.paragraph(tinfo.status, false);
    if (ui.button("Refresh")) load_time_info();
    ui.newline();
  }

  // ------------------------------------------------------------ display --
  void connect_ipc() {
    if (ipc.is_connected()) return;
    if (!ipc.connect()) return;
    ipc_watch = app.watch_fd(ipc.fd(), [this] {
      ipc.poll_lines([this](const std::string& l) { handle_ipc_line(l); });
      if (!ipc.is_connected()) {
        app.unwatch(ipc_watch);
        ipc_watch = 0;
      }
      redraw();
    });
  }

  void request_outputs() {
    connect_ipc();
    if (!ipc.is_connected()) {
      disp_status = "Not connected to the fleetwm compositor.";
      return;
    }
    ipc.send_command("OUTPUTS?");
  }

  void handle_ipc_line(const std::string& line) {
    std::istringstream in(line);
    std::string tag;
    in >> tag;
    if (tag == "OUTPUT") {
      if (!in_list) {
        collecting.clear();
        in_list = true;
      }
      DispMon m;
      in >> m.name >> m.x >> m.y >> m.w >> m.h >> m.r;
      m.ex = m.x;
      m.ey = m.y;
      collecting.push_back(std::move(m));
    } else if (tag == "DISPLAY") {
      int all = 1;
      in >> display.primary_output >> all;
      display.taskbar_all_displays = all != 0;
    } else if (tag == "MODE" && in_list && !collecting.empty()) {
      DispMode md;
      int cur = 0, pref = 0;
      in >> md.w >> md.h >> md.r >> cur >> pref;
      md.cur = cur;
      md.pref = pref;
      collecting.back().modes.push_back(md);
    } else if (tag == "END") {
      // keep per-monitor UI selections that still make sense
      for (auto& m : collecting) {
        for (const auto& old : mons)
          if (old.name == m.name) {
            m.rel_sel = old.rel_sel;
            m.side_sel = old.side_sel;
            m.align_sel = old.align_sel;
          }
        rebuild_mon(m, false);
      }
      mons = std::move(collecting);
      collecting.clear();
      in_list = false;
    } else if (tag == "OUTPUTS_CHANGED") {
      if (drag_mon < 0) request_outputs();
    } else if (tag == "OK") {
      disp_status.clear();
    } else if (tag == "ERR") {
      disp_status = line.size() > 4 ? line.substr(4) : "The compositor rejected that setting.";
    }
  }

  void send_output_set(const std::string& name, int w, int h, int r, int x, int y) {
    connect_ipc();
    if (!ipc.is_connected()) return;
    ipc.send_command("OUTPUT_SET " + name + " " + std::to_string(w) + " " + std::to_string(h) + " " +
                     std::to_string(r) + " " + std::to_string(x) + " " + std::to_string(y));
  }

  void send_display_settings() {
    connect_ipc();
    if (!ipc.is_connected() || display.primary_output.empty()) return;
    ipc.send_command("DISPLAY_SET " + display.primary_output + " " +
                     (display.taskbar_all_displays ? "1" : "0"));
  }

  void apply_monitor(DispMon& m) {
    if (m.res.empty()) return;
    const auto res = m.res[static_cast<size_t>(m.res_sel)];
    const int hz = m.hz.empty() ? 0 : m.hz[static_cast<size_t>(std::min<int>(m.hz_sel, static_cast<int>(m.hz.size()) - 1))];
    const bool mode_changed = res.first != m.w || res.second != m.h || std::abs(hz - m.r) > 500;
    if (mode_changed && !confirm_active) {
      confirm_prev = m;
      confirm_active = true;
      confirm_left = 15;
      if (confirm_timer) app.unwatch(confirm_timer);
      confirm_timer = app.add_timer(1000, [this] {
        if (--confirm_left <= 0) revert_display();
        redraw();
      });
    }
    send_output_set(m.name, res.first, res.second, hz, m.ex, m.ey);
  }

  void keep_display() {
    confirm_active = false;
    if (confirm_timer) app.unwatch(confirm_timer);
    confirm_timer = 0;
  }

  void revert_display() {
    keep_display();
    send_output_set(confirm_prev.name, confirm_prev.w, confirm_prev.h, confirm_prev.r, confirm_prev.x, confirm_prev.y);
  }

  // Snaps the dragged rectangle to the other monitors' edges and centres.
  void snap(const DispMon& m, int& nx, int& ny, double scale) {
    const double t = 16.0 / std::max(0.01, scale);
    double best_dx = t, best_dy = t;
    int sx = nx, sy = ny;
    for (const auto& o : mons) {
      if (o.name == m.name) continue;
      const int ox = o.ex, oy = o.ey;
      const int xc[] = {ox + o.w, ox - m.w, ox, ox + o.w - m.w, ox + (o.w - m.w) / 2};
      const int yc[] = {oy + o.h, oy - m.h, oy, oy + o.h - m.h, oy + (o.h - m.h) / 2};
      for (int c : xc)
        if (std::abs(c - nx) < best_dx) {
          best_dx = std::abs(c - nx);
          sx = c;
        }
      for (int c : yc)
        if (std::abs(c - ny) < best_dy) {
          best_dy = std::abs(c - ny);
          sy = c;
        }
    }
    nx = sx;
    ny = sy;
  }

  void place_relative(DispMon& m) {
    std::vector<const DispMon*> others;
    for (const auto& o : mons)
      if (o.name != m.name) others.push_back(&o);
    if (others.empty()) return;
    const DispMon& o = *others[static_cast<size_t>(std::min<int>(m.rel_sel, static_cast<int>(others.size()) - 1))];
    int x = m.ex, y = m.ey;
    switch (m.side_sel) {
      case 0: x = o.ex + o.w; break;           // right of
      case 1: x = o.ex - m.w; break;           // left of
      case 2: y = o.ey + o.h; break;           // below
      default: y = o.ey - m.h; break;          // above
    }
    const bool horizontal = m.side_sel <= 1;
    if (horizontal) y = m.align_sel == 0 ? o.ey : m.align_sel == 1 ? o.ey + (o.h - m.h) / 2 : o.ey + o.h - m.h;
    else x = m.align_sel == 0 ? o.ex : m.align_sel == 1 ? o.ex + (o.w - m.w) / 2 : o.ex + o.w - m.w;
    m.ex = x;
    m.ey = y;
    send_output_set(m.name, 0, 0, 0, x, y);
  }

  void tab_display(cairo_t* cr) {
    if (!disp_requested) {
      disp_requested = true;
      request_outputs();
    }
    if (confirm_active) {
      ui.label("Keep these display settings? Reverting in " + std::to_string(confirm_left) + " s");
      ui.newline();
      if (ui.button("Keep", true, true)) keep_display();
      if (ui.button("Revert")) revert_display();
      ui.newline();
      ui.separator();
    }
    if (!disp_status.empty()) {
      ui.label(disp_status, true);
      ui.newline();
    }
    if (mons.empty()) {
      ui.label(ipc.is_connected() ? "Waiting for the compositor..." : "No compositor connection.", true);
      ui.newline();
      return;
    }

    ui.row("Primary display");
    std::vector<std::string> display_names;
    for (const auto& m : mons) display_names.push_back(m.name);
    int primary = 0;
    for (size_t i = 0; i < display_names.size(); ++i)
      if (display_names[i] == display.primary_output) primary = static_cast<int>(i);
    if (ui.dropdown(display_names, &primary, 190)) {
      display.primary_output = display_names[static_cast<size_t>(primary)];
      send_display_settings();
    }
    ui.newline();
    ui.row("Taskbar");
    int taskbar_scope = display.taskbar_all_displays ? 0 : 1;
    if (ui.segmented({"All displays", "Primary only"}, &taskbar_scope)) {
      display.taskbar_all_displays = taskbar_scope == 0;
      send_display_settings();
    }
    ui.newline();

    // Arrangement canvas: drag a monitor to move it; edges and centres snap.
    ui.paragraph("Drag the screens to arrange them. Edges and centres snap to the neighbouring screen.");
    UiRect rect;
    const Ui::CanvasEvent ev = ui.canvas(170, &rect);
    {
      int minx = 1 << 30, miny = 1 << 30, maxx = -(1 << 30), maxy = -(1 << 30);
      for (const auto& m : mons) {
        minx = std::min(minx, m.ex);
        miny = std::min(miny, m.ey);
        maxx = std::max(maxx, m.ex + m.w);
        maxy = std::max(maxy, m.ey + m.h);
      }
      const double bw = std::max(1, maxx - minx), bh = std::max(1, maxy - miny);
      const double pad = 28;
      const double scale = std::min((rect.w - 2 * pad) / bw, (rect.h - 2 * pad) / bh);
      const double ox = rect.x + (rect.w - bw * scale) / 2, oy = rect.y + (rect.h - bh * scale) / 2;
      auto to_x = [&](int x) { return ox + (x - minx) * scale; };
      auto to_y = [&](int y) { return oy + (y - miny) * scale; };
      if (ev.pressed) {
        for (int i = static_cast<int>(mons.size()) - 1; i >= 0; --i) {
          const DispMon& m = mons[static_cast<size_t>(i)];
          const UiRect mr{to_x(m.ex), to_y(m.ey), m.w * scale, m.h * scale};
          if (mr.hit(rect.x + ev.x, rect.y + ev.y)) {
            drag_mon = i;
            drag_off_x = (rect.x + ev.x - mr.x) / scale;
            drag_off_y = (rect.y + ev.y - mr.y) / scale;
            break;
          }
        }
      }
      if (drag_mon >= 0 && drag_mon < static_cast<int>(mons.size()) && (ev.down || ev.released)) {
        DispMon& m = mons[static_cast<size_t>(drag_mon)];
        int nx = static_cast<int>(std::lround(minx + (rect.x + ev.x - ox) / scale - drag_off_x));
        int ny = static_cast<int>(std::lround(miny + (rect.y + ev.y - oy) / scale - drag_off_y));
        snap(m, nx, ny, scale);
        m.ex = nx;
        m.ey = ny;
        if (ev.released) {
          send_output_set(m.name, 0, 0, 0, m.ex, m.ey);
          drag_mon = -1;
        }
      } else if (ev.released) {
        drag_mon = -1;
      }
      for (size_t i = 0; i < mons.size(); ++i) {
        const DispMon& m = mons[i];
        const UiRect mr{to_x(m.ex), to_y(m.ey), m.w * scale, m.h * scale};
        const bool dragging = static_cast<int>(i) == drag_mon;
        rounded_rect(cr, mr.x, mr.y, mr.w, mr.h, 4);
        set_source(cr, dragging ? pal.accent : pal.bg_primary);
        cairo_fill_preserve(cr);
        set_source(cr, pal.accent);
        cairo_set_line_width(cr, dragging ? 2 : 1.2);
        cairo_stroke(cr);
        const Color fg = dragging ? pal.bg_primary : pal.fg_primary;
        const TextExtents t1 = measure_text(cr, m.name, 13, true);
        draw_text(cr, m.name, mr.x + (mr.w - t1.width) / 2, mr.y + mr.h / 2 - 2, 13, fg, true);
        const std::string dim = std::to_string(m.w) + "x" + std::to_string(m.h);
        const TextExtents t2 = measure_text(cr, dim, 12);
        draw_text(cr, dim, mr.x + (mr.w - t2.width) / 2, mr.y + mr.h / 2 + 14, 12, dragging ? pal.bg_primary : pal.fg_secondary);
      }
    }
    ui.newline();

    for (auto& m : mons) {
      ui.separator();
      ui.heading(m.name + "  (" + std::to_string(m.w) + " x " + std::to_string(m.h) + " @ " + hz_text(m.r) + ")");
      ui.row("Resolution");
      if (ui.dropdown(m.res_labels, &m.res_sel, 210)) {
        // new resolution: default to its highest refresh rate
        const auto sel = m.res[static_cast<size_t>(m.res_sel)];
        m.hz.clear();
        m.hz_labels.clear();
        for (const auto& md : m.modes)
          if (md.w == sel.first && md.h == sel.second) m.hz.push_back(md.r);
        std::sort(m.hz.begin(), m.hz.end(), std::greater<int>());
        for (int h : m.hz) m.hz_labels.push_back(hz_text(h));
        m.hz_sel = 0;
      }
      ui.newline();
      ui.row("Refresh rate");
      ui.dropdown(m.hz_labels, &m.hz_sel, 150);
      ui.newline();
      ui.row("Position");
      ui.label("X");
      ui.spin(&m.ex, -32000, 32000, 10);
      ui.label("Y");
      ui.spin(&m.ey, -32000, 32000, 10);
      ui.newline();
      ui.row("");
      if (ui.button("Apply", true, true)) apply_monitor(m);
      ui.newline();
      if (mons.size() > 1) {
        std::vector<std::string> others;
        for (const auto& o : mons)
          if (o.name != m.name) others.push_back(o.name);
        ui.row("Place");
        ui.dropdown(side_names, &m.side_sel, 110);
        ui.dropdown(others, &m.rel_sel, 130);
        ui.newline();
        ui.row("Align");
        ui.dropdown(align_names, &m.align_sel, 110);
        if (ui.button("Place")) place_relative(m);
        ui.newline();
      }
    }
  }

  // --------------------------------------------------------------- tabs --
  void tab_theme(cairo_t* cr) {
    ui.row("Theme");
    int t = static_cast<int>(config.theme);
    if (ui.segmented({"Dark", "Catppuccin", "Dracula", "OLED Black", "Light"}, &t)) {
      config.theme = static_cast<ThemeName>(t);
      save_theme();
    }
    ui.newline();

    ui.row("Accent color");
    bool autoc = config.accent.auto_extract;
    if (ui.checkbox("Auto", &autoc)) {
      config.accent.auto_extract = autoc;
      save_theme();
    }
    if (ui.color_button(&config.accent.hex, !config.accent.auto_extract)) save_theme();
    ui.newline();

    ui.row("Window layout");
    int layout = static_cast<int>(config.window_layout);
    if (ui.dropdown({"Tiling (dwm-style)", "Desktop (floating windows)"}, &layout, 240)) {
      config.window_layout = layout == 1 ? WindowLayout::Desktop : WindowLayout::Tiling;
      save_theme();
    }
    ui.newline();
    ui.row("Glass effects (Windows Aero)");
    if (ui.checkbox("Translucent frames and menus", &config.glass)) save_theme();
    ui.newline();
    ui.paragraph("The bar, the taskbar, titlebars, the start menu and the Alt+Tab panel get a see-through, frosted look. Off is flat and matte.", true);
    ui.row("Window shadows");
    if (ui.checkbox("Soft shadow around floating windows", &config.window_shadows)) save_theme();
    ui.newline();
    ui.row("Glass tint");
    int tint_mode = static_cast<int>(config.glass_tint.mode);
    if (ui.dropdown({"Windows 7 blue", "Pick a colour", "From the wallpaper", "Theme colours"}, &tint_mode, 220)) {
      config.glass_tint.mode = static_cast<GlassTintMode>(tint_mode);
      save_theme();
    }
    ui.newline();
    if (config.glass_tint.mode == GlassTintMode::Custom) {
      ui.row("Tint colour");
      if (ui.color_button(&config.glass_tint.hex)) save_theme();
      ui.newline();
    }
    if (config.glass_tint.mode != GlassTintMode::Theme) {
      ui.row("Colour intensity");
      int intensity = config.glass_tint.intensity;
      if (ui.slider(&intensity, 0, 100, 220)) {
        config.glass_tint.intensity = intensity;
        save_theme();
      }
      ui.newline();
    }
    {  // what the glass looks like with this tint: the menu/bar colour, then a focused and an unfocused titlebar
      UiRect sw;
      ui.canvas(22, &sw);
      const kit::Color chips[3] = {pal.glass_surface, pal.glass_title, pal.glass_title_idle};
      for (int i = 0; i < 3; ++i) {
        kit::rounded_rect(cr, sw.x + i * 62, sw.y, 56, sw.h, pal.rounded ? 5 : 0);
        kit::set_source(cr, chips[i]);
        cairo_fill_preserve(cr);
        kit::set_source(cr, pal.fg_secondary);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
      }
      ui.label("Bar and menus, active window, inactive window", true);
      ui.newline();
    }
    ui.paragraph("The colour behind the see-through frames, the taskbar and menus. From the wallpaper picks the colour most of the wallpaper's colourful parts share and follows it when the wallpaper changes.", true);
    const bool tiling = config.window_layout == WindowLayout::Tiling;
    const bool desktop = !tiling;
    ui.paragraph(tiling ? "Tiling: windows tile automatically; keyboard shortcuts drive everything (Alt+Shift+/ lists them)."
                        : "Desktop: floating windows with titlebars, a taskbar and a start menu. Super+/ lists the shortcuts.",
                 true);

    // Settings that only one layout uses stay visible but greyed out in the other.
    ui.space(6);
    ui.section(tiling ? "Tiling layout" : "Tiling layout (not in use)");
    ui.row("Focus border (px)");
    if (ui.spin(&config.focus_border_thickness_px, 0, 10, 1, tiling)) save_theme();
    ui.newline();
    ui.row("Focus border color");
    if (ui.color_button(&config.focus_border_color, tiling)) save_theme();
    ui.newline();
    ui.row("Gap between windows (px)");
    if (ui.spin(&config.gap_px, 0, 64, 1, tiling)) save_theme();
    ui.newline();
    ui.row("Gap at screen edges (px)");
    if (ui.spin(&config.outer_gap_px, 0, 64, 1, tiling)) save_theme();
    ui.newline();
    ui.row("Gap next to the bar (px)");
    if (ui.spin(&config.bar_gap_px, 0, 64, 1, tiling)) save_theme();
    ui.newline();
    ui.row("Pinned border (px)");
    if (ui.spin(&config.pinned_border_thickness_px, 0, 10, 1, tiling)) save_theme();
    ui.newline();
    ui.row("Pinned border color");
    if (ui.color_button(&config.pinned_border_color, tiling)) save_theme();
    ui.newline();
    ui.row("Pinned+focused border color");
    if (ui.color_button(&config.pinned_focused_border_color, tiling)) save_theme();
    ui.newline();

    ui.space(8);
    ui.section(desktop ? "Desktop layout: window titlebar" : "Desktop layout: window titlebar (not in use)");
    TitlebarConfig& tb = config.titlebar;
    ui.row("Titlebar height (px)");
    if (ui.spin(&tb.height, 20, 64, 1, desktop)) {
      tb.button_height = std::min(tb.button_height, tb.height);
      save_theme();
    }
    ui.newline();
    ui.row("Window frame (px)");
    if (ui.spin(&tb.frame_px, 0, 16, 1, desktop)) save_theme();
    ui.newline();
    ui.row("Button width (px)");
    if (ui.spin(&tb.button_width, 20, 80, 1, desktop)) save_theme();
    ui.newline();
    ui.row("Button height (px)");
    if (ui.spin(&tb.button_height, 14, tb.height, 1, desktop)) save_theme();
    ui.newline();
    ui.row("Buttons position");
    int side = tb.buttons_side == ButtonSide::Left ? 1 : 0;
    if (ui.segmented({"Right", "Left"}, &side, desktop)) {
      tb.buttons_side = side == 1 ? ButtonSide::Left : ButtonSide::Right;
      save_theme();
    }
    ui.newline();
    ui.row("Title position");
    int align = static_cast<int>(tb.title_align);
    if (ui.segmented({"Left", "Middle", "Right"}, &align, desktop)) {
      tb.title_align = static_cast<TitleAlign>(align);
      save_theme();
    }
    ui.newline();
    ui.row("Buttons shown");
    if (ui.checkbox("Pin", &tb.show_pin, desktop)) save_theme();
    if (ui.checkbox("Minimize", &tb.show_minimize, desktop)) save_theme();
    if (ui.checkbox("Maximize", &tb.show_maximize, desktop)) save_theme();
    ui.newline();
    ui.row("Application icon");
    if (ui.toggle(&tb.show_icon, desktop)) save_theme();
    ui.newline();
  }

  // ---------------------------------------------------------------- power --
  // One column of display/sleep timers (a profile for mains power or for battery).
  void power_column(const char* title, PowerProfile* profile, double x, double width) {
    (void)width;
    ui.set_cursor_x(x);
    ui.label(title);
    ui.newline();
    const auto& choices = power_timeout_choices();
    std::vector<std::string> labels;
    for (int m : choices) labels.push_back(power_timeout_label(m));
    ui.label("Turn off the display", true);
    ui.newline();
    int d = nearest_power_timeout_index(profile->display_off_minutes);
    ui.set_cursor_x(x);
    if (ui.dropdown(labels, &d, 170)) {
      profile->display_off_minutes = choices[static_cast<size_t>(d)];
      save_power();
    }
    ui.newline();
    ui.label("Put the computer to sleep", true);
    ui.newline();
    int s = nearest_power_timeout_index(profile->sleep_minutes);
    ui.set_cursor_x(x);
    if (ui.dropdown(labels, &s, 170)) {
      profile->sleep_minutes = choices[static_cast<size_t>(s)];
      save_power();
    }
    ui.newline();
  }

  void save_power() { save_power_config(power); }

  void tab_power(cairo_t* cr) {
    const bool on_ac = ac_online();
    const BatteryText t = describe_battery(battery, on_ac);
    const Palette& p = ui.palette();

    // Status card at the top: the battery with its percentage and time, or the plug.
    UiRect r;
    ui.canvas(96, &r);
    {
      const double cy = r.y + r.h / 2;
      if (has_battery && battery.available) {
        const double bw = 64, bh = 30, bx = r.x + 8, by = cy - bh / 2;
        cairo_set_line_width(cr, 2);
        set_source(cr, p.fg_secondary);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_stroke(cr);
        cairo_rectangle(cr, bx + bw, by + 8, 4, bh - 16);
        cairo_fill(cr);
        const double frac = std::clamp(battery.percent / 100.0, 0.0, 1.0);
        set_source(cr, battery.percent <= 15 && !battery.charging ? Color{0.85, 0.25, 0.25, 1.0} : p.accent);
        cairo_rectangle(cr, bx + 3, by + 3, (bw - 6) * frac, bh - 6);
        cairo_fill(cr);
        draw_text(cr, t.headline, bx + bw + 24, cy - 2, 18, p.fg_primary, true);
        if (!t.detail.empty()) draw_text(cr, t.detail, bx + bw + 24, cy + 22, 14, p.fg_secondary);
      } else {
        // Mains plug: body, two prongs, cord.
        const double px = r.x + 40, py = cy;
        set_source(cr, p.fg_secondary);
        cairo_rectangle(cr, px - 14, py - 8, 28, 22);
        cairo_fill(cr);
        cairo_rectangle(cr, px - 8, py - 22, 5, 14);
        cairo_rectangle(cr, px + 3, py - 22, 5, 14);
        cairo_fill(cr);
        cairo_rectangle(cr, px - 2, py + 14, 4, 12);
        cairo_fill(cr);
        draw_text(cr, "On AC power", r.x + 88, cy + 6, 18, p.fg_primary, true);
      }
    }

    ui.space(6);
    ui.section("Sleep and display");
    if (!has_battery) {
      power_column("On AC power", &power.ac, ui.content_left(), 0);
      return;
    }
    // Two columns: mains on the left, battery on the right. The one in use is marked.
    const double left = ui.content_left();
    const double col2 = left + 280;
    const double y0 = ui.cursor_y();
    power_column(on_ac ? "On AC power (now)" : "On AC power", &power.ac, left, 0);
    const double y1 = ui.cursor_y();
    ui.set_cursor_y(y0);
    power_column(on_ac ? "On battery" : "On battery (now)", &power.battery, col2, 0);
    ui.set_cursor_y(std::max(y1, ui.cursor_y()));
    ui.set_cursor_x(left);
    ui.space(4);
    ui.paragraph("Apps that keep the screen awake (video, presentations) are respected.", true);

    ui.space(6);
    ui.section("Power mode");
    ui.row("Power mode");
    int mode = static_cast<int>(bar.power_mode);
    if (ui.segmented({"Normal", "Performance", "Battery Saver"}, &mode)) {
      bar.power_mode = static_cast<PowerMode>(mode);
      save_bar();  // the bar picks this up live and hands the mode to power-profiles-daemon
    }
    ui.newline();
  }

  int pin_add_sel = 0;

  void tab_bar(cairo_t*) {
    const bool tiling = config.window_layout == WindowLayout::Tiling;
    const bool desktop = !tiling;
    ui.label(tiling ? "Tiling layout: a top bar with workspace buttons. The taskbar settings below are not in use."
                    : "Desktop layout: the bar is a taskbar. The top-bar settings below are not in use.",
             true);
    ui.newline();
    ui.space(6);

    auto top_bar = [&] {
    ui.section(tiling ? "Top bar (Tiling layout)" : "Top bar (Tiling layout, not in use)");
    ui.row("Bar layout");
    // Display order differs from the enum order: Capsules first (the default).
    static const BarLayout kOrder[] = {BarLayout::Capsules, BarLayout::Island, BarLayout::Full};
    int layout = 0;
    for (int i = 0; i < 3; ++i)
      if (kOrder[i] == bar.layout) layout = i;
    if (ui.segmented({"Capsules", "Island", "Strip"}, &layout, tiling)) {
      bar.layout = kOrder[layout];
      save_bar();
    }
    ui.newline();
    ui.space(4);
    ui.label("Workspace colors", true);
    ui.newline();
    ui.paragraph("By default the active workspace uses the theme accent and the others stay transparent.");
    auto hex_of = [](const Color& c) { return color_to_hex(c.r, c.g, c.b); };
    struct {
      const char* label;
      std::string* field;
      std::string theme_default;
    } colors[] = {{"Inactive background", &bar.workspace_colors.inactive_bg, hex_of(pal.bg_secondary)},
                  {"Inactive text", &bar.workspace_colors.inactive_fg, hex_of(pal.fg_secondary)},
                  {"Active background", &bar.workspace_colors.active_bg, hex_of(pal.accent)},
                  {"Active text", &bar.workspace_colors.active_fg, hex_of(pal.bg_primary)}};
    bool any_override = false;
    for (auto& c : colors) {
      ui.row(c.label);
      std::string shown = c.field->empty() ? c.theme_default : *c.field;
      if (ui.color_button(&shown, tiling)) {
        *c.field = shown;
        save_bar();
      }
      if (!c.field->empty()) {
        any_override = true;
        ui.label("custom", true);
      }
      ui.newline();
    }
    if (any_override && ui.button("Reset colors", tiling)) {
      for (auto& c : colors) c.field->clear();
      save_bar();
    }
    ui.newline();
    ui.space(4);
    if (ui.checkbox("Rounded workspace buttons", &bar.workspace_colors.buttons_rounded, tiling)) save_bar();
    ui.newline();

    };
    auto task_bar = [&] {
    ui.space(8);
    ui.section(desktop ? "Taskbar (Desktop layout)" : "Taskbar (Desktop layout, not in use)");
    ui.row("Taskbar position");
    int tpos = static_cast<int>(bar.taskbar_position);
    if (ui.segmented({"Bottom", "Top", "Left", "Right"}, &tpos, desktop)) {
      bar.taskbar_position = static_cast<TaskbarPosition>(tpos);
      save_bar();
    }
    ui.newline();
    ui.row("Virtual desktops");
    {
      int n = bar.taskbar_workspaces;
      if (ui.spin(&n, 1, 10, 1, desktop)) {
        bar.taskbar_workspaces = n;
        save_bar();
      }
      ui.label(n <= 1 ? "No switcher" : "1 hides the switcher", true);
    }
    ui.newline();
    ui.row("Window buttons");
    if (ui.checkbox("Rounded corners", &bar.taskbar_rounded, desktop)) save_bar();
    ui.newline();
    ui.row("Running-program line");
    {  // glass has no line, so the switch is off and greyed out while glass effects are on
      bool shown = config.glass ? false : bar.taskbar_indicator;
      if (ui.checkbox("Line under running apps", &shown, desktop && !config.glass)) {
        bar.taskbar_indicator = shown;
        save_bar();
      }
    }
    if (config.glass) ui.label("Needs glass off", true);
    ui.newline();
    ui.row("Window titles");
    if (ui.toggle(&bar.taskbar_labels, desktop)) save_bar();
    ui.label(bar.taskbar_labels ? "Icon and title" : "Icon only", true);
    ui.newline();
    ui.row("Start menu in the middle");
    if (ui.toggle(&bar.start_centered, desktop)) save_bar();
    ui.label(bar.start_centered ? "Centred, like Windows 11" : "At the left edge", true);
    ui.newline();
    ui.row("Hide until the pointer arrives");
    if (ui.toggle(&bar.taskbar_autohide, desktop)) save_bar();
    ui.label(bar.taskbar_autohide ? "Hidden until needed" : "Always visible", true);
    ui.newline();

    ui.row("Media applet");
    int media_display = static_cast<int>(bar.media_display);
    if (ui.segmented({"Controls", "Progress", "Waveform"}, &media_display, true)) {
      bar.media_display = static_cast<MediaDisplay>(media_display);
      save_bar();
    }
    ui.newline();
    ui.paragraph("The applet follows the active MPRIS player. Left-click plays or pauses, middle-click skips, and right-click selects the next player. Waveform mode uses a lightweight beat animation.", true);
    ui.newline();

    ui.space(6);
    ui.label("Parts of the taskbar", true);
    ui.newline();
    ui.paragraph("Show or hide each part and put them in the order you like. The window list takes the space that is left. Parts of the left side stay on the left and the system area stays on the right.");
    {
      const std::vector<TbElement> order = tb_normalize_order(bar.taskbar_order);
      std::vector<TbElement> hidden = tb_normalize_hidden(bar.taskbar_hidden);
      auto zone = [&](bool main_zone, const char* title) {
        ui.label(title, true);
        ui.newline();
        for (TbElement e : order) {
          if (tb_is_main_zone(e) != main_zone) continue;
          ui.row(tb_element_label(e));
          const auto it = std::find(hidden.begin(), hidden.end(), e);
          bool visible = it == hidden.end();
          if (ui.toggle(&visible, desktop)) {
            if (visible) hidden.erase(it);
            else hidden.push_back(e);
            bar.taskbar_order = tb_names(order);
            bar.taskbar_hidden = tb_names(hidden);
            save_bar();
          }
          ui.same_line();
          if (ui.button("Up", desktop)) {
            std::vector<TbElement> moved = order;
            if (tb_move(&moved, e, -1)) {
              bar.taskbar_order = tb_names(moved);
              bar.taskbar_hidden = tb_names(hidden);
              save_bar();
            }
          }
          ui.same_line();
          if (ui.button("Down", desktop)) {
            std::vector<TbElement> moved = order;
            if (tb_move(&moved, e, +1)) {
              bar.taskbar_order = tb_names(moved);
              bar.taskbar_hidden = tb_names(hidden);
              save_bar();
            }
          }
          ui.newline();
        }
      };
      zone(true, "Left side");
      ui.space(4);
      zone(false, "System area");
      ui.space(4);
      ui.row("");
      if (ui.button("Restore the default", desktop)) {
        bar.taskbar_order.clear();
        bar.taskbar_hidden = BarConfig{}.taskbar_hidden;
        save_bar();
      }
      ui.newline();
    }

    ui.space(6);
    ui.label("Pinned apps", true);
    ui.newline();
    ui.paragraph("A pinned app has an icon button on the taskbar that starts it, or brings its window forward when it is already open.");
    for (size_t i = 0; i < bar.pinned_apps.size(); ++i) {
      std::string name = bar.pinned_apps[i];
      for (const DesktopEntry& de : entries)
        if (de.id == bar.pinned_apps[i]) name = de.name;
      ui.row(name);
      if (ui.button("Remove", desktop)) {
        bar.pinned_apps.erase(bar.pinned_apps.begin() + static_cast<long>(i));
        save_bar();
        break;
      }
      ui.newline();
    }
    {
      std::vector<std::string> names;
      std::vector<const DesktopEntry*> candidates;
      for (const DesktopEntry& de : entries) {
        if (std::find(bar.pinned_apps.begin(), bar.pinned_apps.end(), de.id) != bar.pinned_apps.end()) continue;
        names.push_back(de.name);
        candidates.push_back(&de);
      }
      if (!names.empty()) {
        ui.row("Add an app");
        pin_add_sel = std::clamp(pin_add_sel, 0, static_cast<int>(names.size()) - 1);
        ui.dropdown(names, &pin_add_sel, 260);
        ui.same_line();
        if (ui.button("Add", desktop, true)) {
          bar.pinned_apps.push_back(candidates[static_cast<size_t>(pin_add_sel)]->id);
          save_bar();
        }
        ui.newline();
      }
    }

    };
    if (desktop) {
      task_bar();
      ui.space(8);
      top_bar();
    } else {
      top_bar();
      task_bar();
    }

    ui.space(8);
    ui.section("Colours");
    ui.label("Colours of the bar's parts. A grey swatch with no colour set follows the theme; \"Theme\" puts a part back to that.", true);
    ui.newline();
    auto colour_row = [&](const char* label, std::string* hex) {
      ui.row(label);
      if (ui.color_button(hex)) save_bar();
      if (!hex->empty() && ui.button("Theme")) {
        hex->clear();
        save_bar();
      }
      ui.newline();
    };
    colour_row("Taskbar background", &bar.taskbar_colors.bg);
    colour_row("Start button", &bar.taskbar_colors.start);
    colour_row("Active window button", &bar.taskbar_colors.active);
    colour_row("Clock", &bar.taskbar_colors.clock_fg);
    colour_row("Status icons", &bar.taskbar_colors.status_fg);
    colour_row("Workspace button", &bar.workspace_colors.inactive_bg);
    colour_row("Workspace text", &bar.workspace_colors.inactive_fg);
    colour_row("Current workspace", &bar.workspace_colors.active_bg);
    colour_row("Current workspace text", &bar.workspace_colors.active_fg);
    ui.space(8);
    ui.section("Clock");
    ui.label("Used by the taskbar and by the top bar of the Tiling layout.", true);
    ui.newline();
    ui.row("Time format");
    int f24 = bar.clock.use_24h ? 0 : 1;
    if (ui.segmented({"24-hour", "12-hour"}, &f24)) {
      bar.clock.use_24h = f24 == 0;
      save_bar();
    }
    ui.newline();
    struct {
      const char* label;
      bool* field;
    } toggles[] = {{"Show seconds", &bar.clock.show_seconds}, {"Show date", &bar.clock.show_date},
                   {"Show year", &bar.clock.show_year},       {"Show month", &bar.clock.show_month},
                   {"Show day", &bar.clock.show_day}};
    for (auto& t : toggles) {
      ui.row("");
      if (ui.checkbox(t.label, t.field)) save_bar();
      ui.newline(-2);
    }

  }

  void tab_wallpaper(cairo_t* cr) {
    const std::string shown =
        wallpaper.path.empty() ? "No wallpaper set" : ellipsize(cr, wallpaper.path, ui.width() - 32);
    ui.label(shown, true);
    ui.newline();
    if (ui.button("Choose Image...", !wallpaper.use_solid_color))
      ui.open_file_dialog("Choose Wallpaper", wallpaper.path, {".png", ".jpg", ".jpeg", ".webp"});
    ui.newline();
    std::string chosen;
    if (ui.take_file_result(&chosen)) {
      wallpaper.path = chosen;
      save_wallpaper();
    }
    ui.space(6);
    bool solid = wallpaper.use_solid_color;
    if (ui.checkbox("Use solid color", &solid)) {
      wallpaper.use_solid_color = solid;
      save_wallpaper();
    }
    if (ui.color_button(&wallpaper.solid_color, wallpaper.use_solid_color)) save_wallpaper();
    ui.newline();
  }

  void tab_default_apps(cairo_t*) {
    for (auto& row : mime_rows) {
      ui.row(row.label);
      if (row.apps.empty()) {
        ui.label("No apps found", true);
        ui.newline();
        continue;
      }
      std::vector<std::string> names;
      for (int i : row.apps) names.push_back(entries[static_cast<size_t>(i)].name);
      if (ui.radio_group(names, &row.selected) && row.selected >= 0)
        mime_set_default(row.mime, entries[static_cast<size_t>(row.apps[static_cast<size_t>(row.selected)])].id);
      ui.newline();
    }
    ui.row("Terminal");
    if (terminal_apps.empty()) {
      ui.label("No apps found", true);
    } else {
      std::vector<std::string> names;
      for (int i : terminal_apps) names.push_back(entries[static_cast<size_t>(i)].name);
      if (ui.radio_group(names, &terminal_selected) && terminal_selected >= 0) {
        const auto argv = exec_argv(entries[static_cast<size_t>(terminal_apps[static_cast<size_t>(terminal_selected)])]);
        if (!argv.empty()) {
          default_apps.terminal_command = join_command(argv);  // with its arguments: "lestrix --lite" is not "lestrix"
          save_default_apps_config(default_apps);
        }
      }
    }
    ui.newline();
  }

  void save_game() {
    try {
      save_game_config(game);  // the compositor watches the file; the desktop GPU applies at the next login
    } catch (const std::exception&) {
    }
  }

  void tab_gaming(cairo_t*) {
    ui.section("Fullscreen programs");
    ui.row("Scaling");
    int scaling = static_cast<int>(game.scaling);
    if (ui.segmented({"Fit", "Integer", "Stretch", "Off"}, &scaling)) {
      game.scaling = static_cast<fleetwm::FullscreenScaling>(scaling);
      save_game();
    }
    ui.newline();
    ui.paragraph("What happens when a fullscreen picture is not the size of the screen, like an old 4:3 game on a wide monitor. Fit keeps the shape and adds black bars, Integer zooms in whole steps so pixels stay square, Stretch fills the screen, Off leaves it as it is.", true);
    ui.row("Smoothing");
    int filter = static_cast<int>(game.filter);
    if (ui.segmented({"Automatic", "Sharp", "Smooth"}, &filter)) {
      game.filter = static_cast<fleetwm::FullscreenFilter>(filter);
      save_game();
    }
    ui.newline();
    ui.row("Switch the monitor's mode");
    if (ui.toggle(&game.mode_switch)) save_game();
    ui.newline();
    ui.paragraph("When a fullscreen picture is exactly the size of one of the monitor's own modes (an old game at 800x600), switch the monitor to it while the game runs and back afterwards, instead of scaling.", true);
    ui.row("Variable refresh rate");
    if (ui.toggle(&game.adaptive_sync)) save_game();
    ui.newline();
    ui.paragraph("Lets a monitor that supports it follow the game's frame rate while a window is fullscreen.", true);
    ui.space(6);
    ui.section("Performance");
    ui.row("GameMode");
    if (ui.toggle(&game.gamemode)) save_game();
    ui.newline();
    ui.paragraph("Asks Feral GameMode to boost every game while it runs: Steam and Proton games, Wine programs, Lutris, anything fullscreen except video players and browsers. Needs the gamemode package.", true);
    ui.row("Real-time priority");
    if (ui.toggle(&game.realtime)) save_game();
    ui.newline();
    ui.paragraph("Gives the compositor a real-time scheduling class while a game runs, through rtkit, so nothing delays a frame. A runaway compositor is stopped by the kernel after 200 ms instead of freezing the machine.", true);
    ui.row("Late render start");
    if (ui.spin(&game.max_render_time_ms, 0, fleetwm::kRenderTimeMax, 1)) save_game();
    ui.newline();
    ui.paragraph("Milliseconds before the screen refreshes at which a fullscreen game's frame is drawn (0 is off). Later means the picture shows newer input; too late misses the refresh. Try 4 to 6.", true);
    ui.row("Frame rate cap");
    if (ui.spin(&game.fps_limit, 0, fleetwm::kFpsLimitMax, 10)) save_game();
    ui.newline();
    ui.paragraph("0 is no cap. Applied by starting the game with fleetwm-game (Steam launch options: fleetwm-game -- %command%) and needs libstrangle.", true);
    if (fleetwm::tuxthrottle_installed()) {
      ui.row("TuxThrottle Game Mode");
      if (ui.toggle(&game.tuxthrottle)) save_game();
      ui.newline();
      ui.paragraph("Switches TuxThrottle's Game Mode on when the first game starts and off after the last one closes: its performance profile, fans and GPU clocks.", true);
    }
    ui.space(6);
    ui.section("Graphics card");
    ui.row("Desktop drawn by");
    int gpu = static_cast<int>(game.desktop_gpu);
    if (ui.segmented({"Integrated", "Dedicated", "Default"}, &gpu)) {
      game.desktop_gpu = static_cast<fleetwm::DesktopGpu>(gpu);
      save_game();
    }
    ui.newline();
    ui.paragraph("On a computer with two graphics cards the integrated one draws the desktop, so the dedicated card stays cool and free for games. Takes effect at the next login. Screens plugged into either card keep working.", true);
    ui.paragraph("To run one program on the dedicated card: Steam launch options  fleetwm-game --gpu discrete -- %command%  or, for any program, a PrefersNonDefaultGPU=true line in its desktop entry.", true);
    if (fleetwm::tuxthrottle_installed()) {
      ui.space(6);
      ui.section("TuxThrottle");
      if (!tux_loaded) {
        tux_loaded = true;
        tux = fleetwm::read_tuxthrottle_status();
      }
      if (!tux.ok) {
        ui.paragraph("TuxThrottle is installed but its readout is not available (is tuxthrottled running?).", true);
      } else {
        char line[160];
        ui.row("Computer");
        ui.label(tux.model.empty() ? "unknown" : tux.model, true);
        ui.newline();
        ui.row("Power profile");
        ui.label(tux.platform_profile.empty() ? "unknown" : tux.platform_profile, true);
        ui.newline();
        ui.row("Processor");
        std::snprintf(line, sizeof line, "%.0f °C, %.1f W", tux.cpu_temp_c, tux.cpu_power_w);
        ui.label(line, true);
        ui.newline();
        if (tux.dgpu_present) {
          ui.row("Dedicated GPU");
          if (tux.dgpu_awake) std::snprintf(line, sizeof line, "awake, %.0f °C, %.1f W", tux.dgpu_temp_c, tux.dgpu_power_w);
          else std::snprintf(line, sizeof line, "asleep");
          ui.label(line, true);
          ui.newline();
        }
        ui.row("Game Mode");
        ui.label(tux.game_mode ? "on" : "off", true);
        ui.newline();
        ui.paragraph(fleetwm::tuxthrottle_bridged_through_gamemode()
                         ? "gamemoded also starts TuxThrottle's helper scripts (/etc/gamemode.ini), so every game registered with GameMode switches it on."
                         : "Not wired through gamemoded (/etc/gamemode.ini has no TuxThrottle start hook); Fleetwm switches Game Mode itself while a game runs.", true);
      }
      if (ui.button("Refresh")) tux = fleetwm::read_tuxthrottle_status();
      ui.newline();
      if (ui.button(tux.game_mode ? "Turn Game Mode off now" : "Turn Game Mode on now")) {
        fleetwm::tuxthrottle_game_mode(tux.game_mode ? "off" : "on");
      }
      ui.newline();
    }
  }

  void tab_startup(cairo_t*) {
    if (!startup_loaded) {
      startup_loaded = true;
      startup_on = fleetwm::autostart_enabled_ids();
      for (size_t i = 0; i < entries.size(); ++i) startup_order.push_back(i);
      std::sort(startup_order.begin(), startup_order.end(), [this](size_t a, size_t b) {
        auto lower = [](std::string x) { std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return std::tolower(c); }); return x; };
        return lower(entries[a].name) < lower(entries[b].name);
      });
    }
    ui.section("Start with the session");
    ui.paragraph(std::to_string(startup_on.size()) + " program" + (startup_on.size() == 1 ? "" : "s") + " start when you log in. Switch one on to add it, off to take it out. The list is the standard autostart folder (~/.config/autostart), so other desktops see it too.", true);
    ui.space(4);
    for (size_t i : startup_order) {
      const kit::DesktopEntry& e = entries[i];
      bool on = startup_on.count(e.id) > 0;
      ui.row(e.name);
      if (ui.toggle(&on)) {
        fleetwm::AutostartApp app;
        app.id = e.id;
        app.name = e.name;
        app.exec = e.exec;
        app.icon = e.icon;
        app.terminal = e.terminal;
        try {
          fleetwm::autostart_set(app, on);
          if (on) startup_on.insert(e.id);
          else startup_on.erase(e.id);
        } catch (const std::exception&) {
        }
      }
      ui.newline();
    }
  }

  void save_mouse() {
    try {
      save_mouse_config(mouse);  // the compositor watches the file and applies it to every mouse at once
    } catch (const std::exception&) {
    }
  }

  void save_profile() {
    if (profile_draft_name.empty()) profile_draft_name = profile.display_name;
    profile.display_name = profile_draft_name;
    try { save_user_profile(profile); } catch (const std::exception&) {}
  }

  void refresh_profile_image() {
    if (profile.icon_path == profile_image_loaded) return;
    if (profile_surface) cairo_surface_destroy(profile_surface);
    profile_surface = nullptr;
    profile_pixels.clear();
    profile_image_loaded = profile.icon_path;
    if (profile.icon_path.empty()) return;
    const kit::Image image = kit::load_image(profile.icon_path);
    if (!image.ok()) return;
    profile_pixels.resize(64 * 64 * 4);
    kit::render_cover(image, 64, 64, profile_pixels.data());
    profile_surface = cairo_image_surface_create_for_data(profile_pixels.data(), CAIRO_FORMAT_ARGB32, 64, 64, 64 * 4);
  }

  void start_mouse_test() {
    mouse_test_events = 0;
    mouse_test_rate = 0;
    mouse_test_remaining = 5;
    if (mouse_test_timer) app.unwatch(mouse_test_timer);
    mouse_test_timer = app.add_timer(1000, [this] {
      const int elapsed = 6 - mouse_test_remaining;
      mouse_test_rate = static_cast<double>(mouse_test_events) / std::max(1, elapsed);
      if (--mouse_test_remaining <= 0) {
        app.unwatch(mouse_test_timer);
        mouse_test_timer = 0;
      }
      redraw();
    });
  }


  // ------------------------------------------------ navigation and look --
  // Goes to a page, keeping the back/forward history and the recent list.
  void go(int page) {
    if (page < 0 || page >= static_cast<int>(tab_names.size())) return;
    search.clear();
    if (page != tab) {
      history.resize(hist_pos + 1);
      history.push_back(page);
      hist_pos = history.size() - 1;
      tab = page;
      fleetwm::note_recent_page(ui_state.recent, page);
      fleetwm::save_settings_ui_state(ui_state);
    }
    redraw();
  }
  void go_back_forward(int dir) {
    const long to = static_cast<long>(hist_pos) + dir;
    if (to < 0 || to >= static_cast<long>(history.size())) return;
    hist_pos = static_cast<size_t>(to);
    tab = history[hist_pos];
    search.clear();
    redraw();
  }

  // The icon theme's picture for `name`, kept per size; nullptr when the theme has none.
  cairo_surface_t* themed_icon(const std::string& name, int size) {
    const std::string key = name + "@" + std::to_string(size);
    auto it = icon_cache.find(key);
    if (it != icon_cache.end()) return it->second;
    cairo_surface_t* surf = kit::load_icon(name, size);
    icon_cache[key] = surf;
    return surf;
  }
  // An icon in a `size` px square: the theme's picture, or a coloured rounded tile with the first letter when the theme has none.
  void draw_icon(cairo_t* cr, const std::string& name, unsigned colour, const std::string& label, double x, double y, double size) {
    if (cairo_surface_t* icon = themed_icon(name, static_cast<int>(size))) {
      const double iw = cairo_image_surface_get_width(icon), ih = cairo_image_surface_get_height(icon), sc = size / std::max(iw, ih);
      cairo_save(cr);
      cairo_translate(cr, x + (size - iw * sc) / 2, y + (size - ih * sc) / 2);
      cairo_scale(cr, sc, sc);
      cairo_set_source_surface(cr, icon, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
    kit::rounded_rect(cr, x, y, size, size, size * 0.24);
    set_source(cr, Color{((colour >> 16) & 255) / 255.0, ((colour >> 8) & 255) / 255.0, (colour & 255) / 255.0, 0.92});
    cairo_fill(cr);
    const double px = size * 0.5;
    const std::string letter = label.empty() ? "?" : label.substr(0, 1);
    const TextExtents te = measure_text(cr, letter, px, true);
    draw_text(cr, letter, x + (size - te.width) / 2, y + (size + te.ascent - (te.height - te.ascent)) / 2, px, Color{1, 1, 1, 1}, true);
  }
  void draw_page_icon(cairo_t* cr, int page, double x, double y, double size) {
    const auto& pg = fleetwm::settings_pages()[static_cast<size_t>(page)];
    const unsigned colour = pg.category >= 0 ? fleetwm::settings_categories()[static_cast<size_t>(pg.category)].colour : 0x3B82F6;
    draw_icon(cr, pg.icon, colour, pg.name, x, y, size);
  }
  void draw_avatar(cairo_t* cr, double cx, double cy, double r) {
    const Palette& p = ui.palette();
    refresh_profile_image();
    cairo_save(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * 3.141592653589793);
    if (profile_surface) {
      cairo_clip(cr);
      cairo_translate(cr, cx - r, cy - r);
      cairo_scale(cr, 2 * r / 64.0, 2 * r / 64.0);
      cairo_set_source_surface(cr, profile_surface, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
    Color c = p.accent;
    c.a = 0.85;
    set_source(cr, c);
    cairo_fill(cr);
    cairo_restore(cr);
    std::string initials;
    for (const char ch : profile.display_name) {
      if (std::isalpha(static_cast<unsigned char>(ch))) initials += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (initials.size() == 2) break;
    }
    if (initials.empty()) initials = "F";
    const TextExtents te = measure_text(cr, initials, r * 0.72, true);
    draw_text(cr, initials, cx - te.width / 2, cy + (te.ascent - (te.height - te.ascent)) / 2, r * 0.72, p.bg_primary, true);
  }
  static Color with_alpha(Color c, double a) {
    c.a = a;
    return c;
  }
  static Color blend(const Color& a, const Color& b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
  }

  // A full-width row card: icon, title, a dim line under it and a chevron. Returns true when clicked.
  bool row_card(int page, const std::string& title, const std::string& sub) {
    const UiRect r = ui.reserve(52);
    bool hover = false, focus = false;
    const bool clicked = ui.tile(r, &hover, &focus);
    cairo_t* cr = ui.cr();
    const Palette& p = ui.palette();
    kit::rounded_rect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, p.rounded ? 8 : 0);
    set_source(cr, hover ? blend(p.bg_secondary, p.accent, 0.14) : p.bg_secondary);
    cairo_fill_preserve(cr);
    set_source(cr, focus ? p.accent : with_alpha(p.fg_secondary, 0.12));
    cairo_set_line_width(cr, focus ? 1.6 : 1);
    cairo_stroke(cr);
    draw_page_icon(cr, page, r.x + 14, r.y + (r.h - 28) / 2, 28);
    draw_text(cr, fit_text(cr, title, r.w - 100, kFont, true), r.x + 58, r.y + 22, kFont, p.fg_primary, true);
    draw_text(cr, fit_text(cr, sub, r.w - 100, 12.5), r.x + 58, r.y + 40, 12.5, p.fg_secondary);
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    set_source(cr, with_alpha(p.fg_secondary, 0.8));
    cairo_move_to(cr, r.x + r.w - 22, r.y + r.h / 2 - 5);
    cairo_line_to(cr, r.x + r.w - 17, r.y + r.h / 2);
    cairo_line_to(cr, r.x + r.w - 22, r.y + r.h / 2 + 5);
    cairo_stroke(cr);
    return clicked;
  }
  std::string category_of(int page) const {
    const int c = fleetwm::settings_pages()[static_cast<size_t>(page)].category;
    return c >= 0 ? fleetwm::settings_categories()[static_cast<size_t>(c)].name : "";
  }

  // Page title with its icon and a one line description (macOS System Settings).
  void page_header(int page) {
    const UiRect r = ui.reserve(58);
    cairo_t* cr = ui.cr();
    const Palette& p = ui.palette();
    draw_page_icon(cr, page, r.x, r.y + 4, 40);
    draw_text(cr, tab_names[static_cast<size_t>(page)], r.x + 54, r.y + 24, 21, p.fg_primary, true);
    draw_text(cr, fit_text(cr, fleetwm::settings_pages()[static_cast<size_t>(page)].description, r.w - 60, 12.5), r.x + 54, r.y + 44, 12.5, p.fg_secondary);
  }

  // -------------------------------------------------------------- Home --
  void tab_home(cairo_t*) {
    cairo_t* cr = ui.cr();
    const Palette& p = ui.palette();
    // Banner: who and what this is (Windows 11 Home).
    {
      const UiRect r = ui.reserve(92);
      kit::rounded_rect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, p.rounded ? 10 : 0);
      set_source(cr, p.bg_secondary);
      cairo_fill(cr);
      draw_avatar(cr, r.x + 52, r.y + 46, 30);
      const std::string who = profile.display_name.empty() ? "Fleetwm" : profile.display_name;
      draw_text(cr, who, r.x + 98, r.y + 40, 20, p.fg_primary, true);
      draw_text(cr, host_name + "  |  Fleetwm " + version_string(), r.x + 98, r.y + 62, 12.5, p.fg_secondary);
      bool hover = false, focus = false;
      if (ui.tile(r, &hover, &focus)) go(fleetwm::kPageAccount);  // the whole banner opens the account page
      if (hover || focus) {
        kit::rounded_rect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, p.rounded ? 10 : 0);
        set_source(cr, focus ? p.accent : with_alpha(p.accent, 0.5));
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);
      }
      draw_text(cr, "Edit your name and picture", r.x + 98, r.y + 82, 12, p.accent);
    }
    // Recent pages (Windows 11 "Recommended settings").
    if (!ui_state.recent.empty()) {
      ui.section("Recent");
      for (int page : ui_state.recent)
        if (row_card(page, tab_names[static_cast<size_t>(page)], fleetwm::settings_pages()[static_cast<size_t>(page)].description)) go(page);
    }
    // All settings (the Control Panel's "Adjust your computer's settings").
    ui.section("All settings");
    ui.row("View by");
    {
      std::vector<std::string> labels = {"Categories", "Large icons", "Small icons"};
      int idx = static_cast<int>(ui_state.view);
      if (ui.dropdown(labels, &idx, 170)) {
        ui_state.view = static_cast<fleetwm::HomeView>(idx);
        fleetwm::save_settings_ui_state(ui_state);
      }
    }
    ui.newline();
    const auto& cats = fleetwm::settings_categories();
    if (ui_state.view == fleetwm::HomeView::Categories) {
      for (size_t i = 0; i < cats.size(); i += 2) {
        auto block_h = [&](size_t c) { return c < cats.size() ? 20.0 + std::max(54.0, 34.0 + 24.0 * fleetwm::settings_tasks(static_cast<int>(c)).size()) : 0.0; };
        const double bh = std::max(block_h(i), block_h(i + 1));
        const UiRect r = ui.reserve(bh);
        const double colw = r.w / 2;
        for (size_t col = 0; col < 2 && i + col < cats.size(); ++col) {
          const size_t c = i + col;
          const double x = r.x + col * colw;
          const auto& cat = cats[c];
          draw_icon(cr, cat.icon, cat.colour, cat.name, x + 4, r.y + 8, 44);
          bool hover = false, focus = false;
          const UiRect title{x + 58, r.y + 6, colw - 70, 26};
          if (ui.tile(title, &hover, &focus)) go(cat.pages.front());
          draw_text(cr, fit_text(cr, cat.name, title.w, 15.5, true), title.x, title.y + 19, 15.5, hover ? p.accent : blend(p.accent, p.fg_primary, 0.55), true);
          if (focus) {
            kit::rounded_rect(cr, title.x - 3, title.y, std::min(title.w, measure_text(cr, cat.name, 15.5, true).width + 6), title.h, 4);
            set_source(cr, with_alpha(p.accent, 0.9));
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
          }
          double ty = r.y + 36;
          for (const fleetwm::SettingsTask& task : fleetwm::settings_tasks(static_cast<int>(c))) {
            const UiRect tr{x + 58, ty, colw - 70, 22};
            bool h2 = false, f2 = false;
            if (ui.tile(tr, &h2, &f2)) go(task.page);
            const double tw = draw_text(cr, fit_text(cr, task.text, tr.w, 13), tr.x, tr.y + 16, 13, h2 ? p.accent : blend(p.accent, p.fg_primary, 0.4));
            if (h2 || f2) {
              cairo_rectangle(cr, tr.x, tr.y + 19, tw, 1);
              set_source(cr, p.accent);
              cairo_fill(cr);
            }
            ty += 24;
          }
        }
      }
    } else {
      std::vector<int> order;
      for (const auto& cat : cats)
        for (int page : cat.pages) order.push_back(page);
      const bool large = ui_state.view == fleetwm::HomeView::LargeIcons;
      const double tw = large ? 132 : 210, th = large ? 108 : 40;
      const UiRect area = ui.reserve(0);
      const int cols = std::max(1, static_cast<int>(area.w / tw));
      const int rows = (static_cast<int>(order.size()) + cols - 1) / cols;
      const UiRect grid = ui.reserve(rows * (th + 4));
      for (size_t i = 0; i < order.size(); ++i) {
        const int page = order[i];
        const UiRect r{grid.x + (i % cols) * tw, grid.y + (i / cols) * (th + 4), tw - 6, th};
        bool hover = false, focus = false;
        if (ui.tile(r, &hover, &focus)) go(page);
        if (hover || focus) {
          kit::rounded_rect(cr, r.x, r.y, r.w, r.h, p.rounded ? 8 : 0);
          set_source(cr, focus ? with_alpha(p.accent, 0.25) : with_alpha(p.fg_secondary, 0.12));
          cairo_fill(cr);
        }
        const std::string& name = tab_names[static_cast<size_t>(page)];
        if (large) {
          draw_page_icon(cr, page, r.x + (r.w - 48) / 2, r.y + 10, 48);
          const TextExtents te = measure_text(cr, name, 13);
          draw_text(cr, name, r.x + (r.w - te.width) / 2, r.y + 82, 13, p.fg_primary);
        } else {
          draw_page_icon(cr, page, r.x + 8, r.y + 8, 24);
          draw_text(cr, name, r.x + 42, r.y + 25, kFont, p.fg_primary);
        }
      }
    }
    ui.space(8);
    ui.paragraph("Press the search box at the top and type what you want to change, for example wifi, dark mode or resolution.", true);
  }

  // ---------------------------------------------------- search results --
  void tab_search() {
    const auto hits = fleetwm::search_settings(search, 12);
    ui.title("Search results");
    if (hits.empty()) {
      ui.paragraph("No setting matches \"" + search + "\". Try another word, such as wifi, theme, volume or resolution.");
      return;
    }
    int target = -1;
    for (const fleetwm::SearchHit& h : hits) {
      const std::string where = category_of(h.page).empty() ? tab_names[static_cast<size_t>(h.page)] : category_of(h.page) + "  >  " + tab_names[static_cast<size_t>(h.page)];
      if (row_card(h.page, h.title, where)) target = h.page;
    }
    if (target >= 0) go(target);
  }

  void tab_account(cairo_t*) {
    ui.section("Welcome");
    refresh_profile_image();
    UiRect avatar;
    ui.canvas(76, &avatar);
    cairo_t* cr = ui.cr();
    const Palette& p = ui.palette();
    if (profile_surface) {
      cairo_save(cr);
      cairo_arc(cr, avatar.x + 38, avatar.y + 38, 30, 0, 2 * 3.141592653589793);
      cairo_clip(cr);
      cairo_set_source_surface(cr, profile_surface, avatar.x + 6, avatar.y + 6);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      cairo_arc(cr, avatar.x + 38, avatar.y + 38, 30, 0, 2 * 3.141592653589793);
      Color avatar_color = p.accent;
      avatar_color.a = 0.85;
      set_source(cr, avatar_color);
      cairo_fill(cr);
      std::string initials;
      for (const char ch : profile.display_name) {
        if (std::isalpha(static_cast<unsigned char>(ch))) initials += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (initials.size() == 2) break;
      }
      if (initials.empty()) initials = "F";
      const TextExtents te = measure_text(cr, initials, 22, true);
      draw_text(cr, initials, avatar.x + 38 - te.width / 2, avatar.y + 38 + (te.height - te.ascent) / 2, 22, p.bg_primary, true);
    }
    ui.paragraph("Fleetwm Settings keeps your desktop preferences in one place.");
    ui.space(6);
    ui.section("Your account");
    ui.row("Display name");
    ui.text_entry(&profile_draft_name, 300);
    ui.newline();
    ui.row("Profile icon");
    if (ui.button("Choose image...")) ui.open_file_dialog("Choose profile image", profile.icon_path, {".png", ".jpg", ".jpeg", ".webp"});
    ui.newline();
    std::string chosen;
    if (ui.take_file_result(&chosen)) profile.icon_path = chosen;
    ui.paragraph(profile.icon_path.empty() ? "No image selected. Fleetwm will show your initials." : profile.icon_path, true);
    if (ui.button("Save profile", !profile_draft_name.empty(), true)) save_profile();
    ui.newline();
    ui.space(8);
    ui.paragraph("The display name is shown by Fleetwm and does not change your Linux username.", true);
  }

  // The pointer shapes, drawn by the same code the compositor uses, in the style the Glass effects setting picks.
  // Each finished picture is kept (the same cache the compositor has), so the tab does not redraw 14 pointers per frame.
  kit::CursorCache<cairo_surface_t*> cursor_previews;

  void tab_mouse(cairo_t* cr) {
    ui.section("Pointer");
    ui.row("Pointer speed");
    int speed = mouse.speed;
    if (ui.slider(&speed, kMouseSpeedMin, kMouseSpeedMax)) {
      mouse.speed = speed;
      save_mouse();
    }
    ui.newline();
    ui.paragraph("Slow to fast in eleven steps, like Windows. The middle step is the default.", true);
    ui.row("Enhance pointer precision");
    if (ui.toggle(&mouse.enhanced_precision)) save_mouse();
    ui.newline();
    ui.paragraph("On: the pointer speeds up with the mouse, so small moves stay precise and big ones cross the screen.", true);
    ui.paragraph("Off: the pointer follows the mouse one to one at any speed.", true);
    ui.space(6);
    ui.section("Buttons and scrolling");
    ui.row("Lines per wheel step");
    if (ui.spin(&mouse.scroll_lines, kScrollLinesMin, kScrollLinesMax, 1)) save_mouse();
    ui.newline();
    ui.paragraph("Controls how far a wheel step moves in Fleetwm applications.", true);
    ui.space(4);
    ui.row("Primary button");
    int primary = mouse.swap_buttons ? 1 : 0;
    if (ui.segmented({"Left", "Right"}, &primary)) {
      mouse.swap_buttons = primary == 1;
      save_mouse();
    }
    ui.newline();
    ui.row("Natural scrolling");
    if (ui.toggle(&mouse.natural_scroll)) save_mouse();
    ui.newline();
    ui.paragraph("Natural: the page moves with the wheel or the touchpad, as on a phone. Applies to every mouse and touchpad, not to the virtual pointers of remote desktops.", true);
    ui.space(6);
    ui.section("Pointer shapes");
    UiRect r;
    ui.canvas(48, &r);
    const bool glass = config.glass;
    constexpr int kCount = static_cast<int>(kit::CursorShape::Count);
    const double step = std::min(46.0, r.w / kCount);
    for (int i = 0; i < kCount; ++i) {
      cairo_surface_t* picture = cursor_previews.get({static_cast<kit::CursorShape>(i), glass, 1}, [&] {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kit::kCursorGrid, kit::kCursorGrid);
        cairo_t* c = cairo_create(s);
        kit::draw_cursor(c, static_cast<kit::CursorShape>(i), glass, 1);
        cairo_destroy(c);
        return s;
      });
      cairo_set_source_surface(cr, picture, r.x + i * step + (step - kit::kCursorGrid) / 2, r.y + (r.h - kit::kCursorGrid) / 2);
      cairo_paint(cr);
    }
    ui.label(glass ? "Windows Aero style, with a shadow (Glass effects is on)."
                   : "The same pointers, flat (Glass effects is off).", true);
    ui.newline();
    ui.section("Pointer event test");
    ui.paragraph("Move the mouse while the test runs. This observed pointer event rate is a useful polling-rate estimate.", true);
    if (ui.button(mouse_test_remaining ? "Testing..." : "Start 5 second test", !mouse_test_remaining)) start_mouse_test();
    if (mouse_test_rate > 0) {
      ui.newline();
      ui.label(std::to_string(static_cast<int>(mouse_test_rate)) + " events per second", true);
    }
  }

  void tab_audio(cairo_t*) {
    ui.section("Master volume");
    if (!master_available) {
      ui.label("Audio unavailable (no PipeWire)", true);
      ui.newline();
    } else {
      if (ui.button(master_muted ? "Unmute" : "Mute")) mixer.set_master_muted(!master_muted);
      int v = master_value;
      if (ui.slider(&v, 0, 100)) {
        master_value = v;
        mixer.set_master_volume(v);
      }
      char buf[8];
      std::snprintf(buf, sizeof buf, "%d", master_value);
      ui.newline();
    }
    ui.space(6);
    ui.section("Speaker test");
    if (ui.button("Play test sound", master_available)) {
      spawn_detached({"speaker-test", "-t", "sine", "-f", "440", "-l", "1"});
      speaker_test_status = "Playing a short 440 Hz test tone";
    }
    if (!speaker_test_status.empty()) { ui.newline(); ui.label(speaker_test_status, true); }
    ui.space(6);
    ui.section("Applications");
    if (streams.empty()) {
      ui.label("Nothing is playing", true);
      ui.newline();
    }
    for (auto& s : streams) {
      ui.label(s.label);
      ui.newline(-4);
      int v = s.volume_percent;
      if (ui.slider(&v, 0, 100)) {
        s.volume_percent = v;
        mixer.set_stream_volume(s.node_id, v);
      }
      ui.newline();
    }
  }

  void tab_performance(cairo_t*) {
    ui.row("Render mode");
    int mode = static_cast<int>(config.render_mode);
    if (ui.segmented({"Synced", "Custom FPS cap"}, &mode)) {
      config.render_mode = static_cast<RenderMode>(mode);
      save_theme();
    }
    ui.newline();
    ui.row("Custom FPS lock");
    if (ui.spin(&config.custom_fps_lock, 24, 5000, 1, config.render_mode == RenderMode::Custom)) save_theme();
    ui.newline();
    if (ui.checkbox("Show performance overlay on startup", &config.show_debug_overlay_on_startup)) save_theme();
    ui.newline();
  }

  void tab_file_manager(cairo_t*) {
    ui.tabs(fleetwm::fm::fm_settings_pages(), &fm_page);
    ui.space(6);
    const fleetwm::fm::FmSettingsResult r = fleetwm::fm::draw_fm_settings_page(ui, fm_cfg, fm_page);
    if (r.changed) {
      try {
        fleetwm::fm::save_fm_settings(fm_cfg);  // the open file manager windows pick this up from the file
      } catch (const std::exception& e) {
        std::fprintf(stderr, "fleetwm-settings: %s\n", e.what());
      }
    }
    if (r.connect) spawn_detached({"fleetwm-fm", "--connect"});
    else if (r.nextcloud) spawn_detached({"fleetwm-fm", "--nextcloud"});
  }

  void tab_desktop_icons(cairo_t*) {
    if (fleetwm::fm::draw_desktop_settings(ui, desktop_cfg)) {
      try {
        fleetwm::fm::save_desktop_config(desktop_cfg);
      } catch (const std::exception& e) {
        std::fprintf(stderr, "fleetwm-settings: %s\n", e.what());
      }
    }
  }

  void tab_about(cairo_t*) {
    ui.section("fleetwm");
    ui.paragraph("A custom wlroots-based Wayland compositor and desktop shell (bar, settings, launcher, wallpaper, greeter).");
    ui.space(6);
    ui.row("Version");
    ui.label(version_string());
    ui.newline();
    ui.row("Built");
    ui.label(std::string(__DATE__) + ", " + __TIME__, true);
    ui.newline();
    ui.space(6);
    if (ui.link("github.com/BeanGreen247/fleetwm"))
      spawn_detached({"xdg-open", "https://github.com/BeanGreen247/fleetwm"});
    ui.newline();
    ui.space(6);
    ui.label("Sole developer: BeanGreen247");
    ui.newline();
    ui.label("License: MIT", true);
    ui.newline();
    ui.space(10);
    ui.section("Backup");
    ui.paragraph("All settings are saved to one tar.gz file and can be loaded back. An import keeps the previous settings in a fleetwm.bak-DATE folder next to the settings folder.");
    ui.row("File");
    ui.text_entry(&backup_path, 380);
    ui.newline();
    ui.row("Machine specific");
    ui.checkbox("Include displays and saved servers", &backup_all);
    ui.newline();
    const bool have_path = !backup_path.empty();
    fleetwm::BackupOptions opt;
    opt.include_machine_specific = backup_all;
    if (ui.button("Export", have_path)) backup_status = fleetwm::export_config(backup_path, opt).message;
    if (ui.button("Import", have_path)) {
      backup_status = fleetwm::import_config(backup_path, opt).message;
      reload_from_disk();
    }
    ui.newline();
    if (!backup_status.empty()) {
      ui.paragraph(backup_status);
    }
  }

  // ---------------------------------------------------------------- draw --
  // The sidebar's entries: Home, then each category's heading followed by its pages.
  void build_nav(std::vector<Ui::NavEntry>* entries) {
    nav_pages.clear();
    entries->push_back({"Home", false});
    nav_pages.push_back(fleetwm::kPageHome);
    for (const fleetwm::SettingsCategory& cat : fleetwm::settings_categories()) {
      entries->push_back({cat.name, true});
      nav_pages.push_back(-1);
      for (int page : cat.pages) {
        entries->push_back({tab_names[static_cast<size_t>(page)], false});
        nav_pages.push_back(page);
      }
    }
  }

  void draw_sidebar(cairo_t* cr, int h) {
    const Palette& p = ui.palette();
    cairo_rectangle(cr, 0, 0, kSidebarW, h);
    Color sidebar = p.bg_secondary;
    sidebar.a = p.window_alpha;  // see-through with Glass effects, like the rest of the window
    set_source(cr, sidebar);
    cairo_fill(cr);
    cairo_rectangle(cr, kSidebarW - 1, 0, 1, h);
    Color line = p.fg_secondary;
    line.a = 0.18;
    set_source(cr, line);
    cairo_fill(cr);
    // Profile card (macOS and Windows 11): opens the account page.
    const UiRect card{10, 10, kSidebarW - 20, 58};
    bool hover = false, focus = false;
    if (ui.tile(card, &hover, &focus)) go(fleetwm::kPageAccount);
    if (hover || focus || tab == fleetwm::kPageAccount) {
      kit::rounded_rect(cr, card.x, card.y, card.w, card.h, p.rounded ? 10 : 3);
      set_source(cr, tab == fleetwm::kPageAccount ? with_alpha(p.accent, 0.22) : with_alpha(p.fg_secondary, 0.10));
      cairo_fill(cr);
    }
    draw_avatar(cr, card.x + 30, card.y + 29, 19);
    draw_text(cr, fit_text(cr, profile.display_name.empty() ? "Fleetwm" : profile.display_name, card.w - 70, 14.5, true), card.x + 58, card.y + 26, 14.5, p.fg_primary, true);
    draw_text(cr, "Your account", card.x + 58, card.y + 44, 12, p.fg_secondary);
    std::vector<Ui::NavEntry> entries;
    build_nav(&entries);
    int current = 0;
    for (size_t i = 0; i < nav_pages.size(); ++i)
      if (nav_pages[i] == tab) current = static_cast<int>(i);
    const int shown = tab == fleetwm::kPageAccount ? -1 : current;
    int pick = shown < 0 ? 0 : shown;
    auto icon = [this](cairo_t* c, const UiRect& box, int i) {
      if (nav_pages[static_cast<size_t>(i)] >= 0) draw_page_icon(c, nav_pages[static_cast<size_t>(i)], box.x, box.y, box.w);
    };
    if (ui.nav_entries(entries, &pick, {0, 76, kSidebarW, static_cast<double>(h) - 76}, icon) && nav_pages[static_cast<size_t>(pick)] >= 0)
      go(nav_pages[static_cast<size_t>(pick)]);
  }

  // Back and forward, the breadcrumb and the search box (Control Panel's address bar, Windows 11's search).
  void draw_header(cairo_t* cr, int w) {
    const Palette& p = ui.palette();
    const double x0 = kSidebarW + 14;
    for (int dir = -1; dir <= 1; dir += 2) {
      const UiRect b{x0 + (dir < 0 ? 0 : 36), 12, 32, 32};
      const bool can = dir < 0 ? hist_pos > 0 : hist_pos + 1 < history.size();
      bool hover = false, focus = false;
      if (ui.tile(b, &hover, &focus) && can) go_back_forward(dir);
      if (can && (hover || focus)) {
        kit::rounded_rect(cr, b.x, b.y, b.w, b.h, p.rounded ? 8 : 3);
        set_source(cr, with_alpha(p.fg_secondary, 0.16));
        cairo_fill(cr);
      }
      set_source(cr, with_alpha(p.fg_primary, can ? 0.9 : 0.3));
      cairo_set_line_width(cr, 2);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      const double cx = b.x + b.w / 2, cy = b.y + b.h / 2;
      cairo_move_to(cr, cx - dir * 3, cy - 6);
      cairo_line_to(cr, cx + dir * 3, cy);
      cairo_line_to(cr, cx - dir * 3, cy + 6);
      cairo_stroke(cr);
    }
    // Breadcrumb: Settings > Category > Page
    std::string crumb = "Settings";
    if (!search.empty()) {
      crumb += "  >  Search";
    } else if (tab != fleetwm::kPageHome) {
      const std::string cat = category_of(tab);
      if (!cat.empty()) crumb += "  >  " + cat;
      crumb += "  >  " + tab_names[static_cast<size_t>(tab)];
    }
    const double search_w = std::clamp((w - kSidebarW) * 0.34, 170.0, 280.0);
    const double crumb_x = x0 + 80, crumb_max = w - 28 - search_w - 16 - crumb_x;
    if (crumb_max > 40) draw_text(cr, fit_text(cr, crumb, crumb_max, 13.5), crumb_x, 33, 13.5, p.fg_secondary);
    ui.set_cursor(w - 28 - search_w, 10);
    ui.text_entry(&search, search_w);
    if (search.empty()) draw_text(cr, "Find a setting", w - 28 - search_w + 12, 33, 13.5, with_alpha(p.fg_secondary, 0.65));
    if (ui.entry_submitted() && !search.empty()) {
      const auto hits = fleetwm::search_settings(search, 1);
      if (!hits.empty()) go(hits.front().page);
    }
    cairo_rectangle(cr, kSidebarW, 55, w - kSidebarW, 1);
    set_source(cr, with_alpha(p.fg_secondary, 0.14));
    cairo_fill(cr);
  }

  void draw(cairo_t* cr, int w, int h) {
    ui.begin(cr, w, h);
    ui.set_scroll_lines(mouse.scroll_lines);
    if (host_name.empty()) {
      char buf[128] = {};
      host_name = gethostname(buf, sizeof buf - 1) == 0 && buf[0] ? buf : "this computer";
    }
    draw_sidebar(cr, h);
    ui.set_margins(kSidebarW + 14, 10, 28);
    draw_header(cr, w);

    // Content: the tab's sections in a scrolling pane under the header.
    ui.set_margins(28, 8, 28);
    ui.set_label_width(220);
    constexpr double kHeaderH = 56;
    ui.begin_scroll({kSidebarW, kHeaderH, static_cast<double>(w) - kSidebarW, static_cast<double>(h) - kHeaderH}, &scroll[tab]);
    ui.space(10);
    if (!search.empty()) {
      tab_search();
    } else {
      if (tab != fleetwm::kPageHome) page_header(tab);
      switch (tab) {
        case 0: tab_home(cr); break;
        case 1: tab_theme(cr); break;
        case 2: tab_bar(cr); break;
        case 3: tab_wallpaper(cr); break;
        case 4: tab_display(cr); break;
        case 5: net_tab->draw(ui, cr); break;
        case 6: bt_tab->draw(ui, cr); break;
        case 7: kb_tab->draw(ui, cr); break;
        case 8: tab_mouse(cr); break;
        case 9: tab_power(cr); break;
        case 10: tab_datetime(cr); break;
        case 11: tab_default_apps(cr); break;
        case 12: tab_audio(cr); break;
        case 13: tab_performance(cr); break;
        case 14: tab_file_manager(cr); break;
        case 15: tab_desktop_icons(cr); break;
        case 16: remote_tab->draw(ui, cr); break;
        case 17: tab_about(cr); break;
        case fleetwm::kPageStartup: tab_startup(cr); break;
        case fleetwm::kPageGaming: tab_gaming(cr); break;
        default: tab_account(cr); break;
      }
    }
    ui.space(24);
    ui.end_scroll();
    ui.end();
    if (ui.wants_another_frame()) redraw();
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-settings", "[--page NAME] [--search TEXT]")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  fleetwm::prewarm::start("fleetwm-settings");
  signal(SIGCHLD, SIG_IGN);

  Settings S;
  S.config = load_theme_config();
  S.bar = load_bar_config();
  S.wallpaper = load_wallpaper_config();
  S.fm_cfg = fleetwm::fm::load_fm_settings();
  S.desktop_cfg = fleetwm::fm::load_desktop_config();
  S.default_apps = load_default_apps_config();
  S.power = load_power_config();
  S.mouse = load_mouse_config();
  S.game = load_game_config();
  S.profile = load_user_profile();
  S.profile_draft_name = S.profile.display_name;
  S.ui_state = fleetwm::load_settings_ui_state();
  S.apply_theme();
  S.net_tab = std::make_unique<NetworkTab>(S.app, [&S] { S.redraw(); });
  S.bt_tab = std::make_unique<BluetoothTab>(S.app, [&S] { S.redraw(); });
  // `fleetwm-settings --page power` opens on that page (the bar's battery icon uses it).
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--search") S.search = argv[i + 1];
    if (std::string(argv[i]) == "--page") {
      const int page = find_settings_page(S.tab_names, argv[i + 1]);
      if (page >= 0) {
        S.tab = page;
        S.history = {page};
      }
    }
  }
  S.battery_dir = find_battery_dir();
  S.has_battery = !S.battery_dir.empty();
  if (const char* d = std::getenv("FLEETWM_BATTERY_DIR")) {  // test hook, same as the bar
    S.battery_dir = d;
    S.has_battery = true;
  }
  S.load_apps();

  if (!S.app.connect()) return 1;
  S.kb_tab = std::make_unique<KeyboardTab>(S.app, [&S] { S.redraw(); });
  S.remote_tab = std::make_unique<RemoteTab>(S.app, [&S] { S.redraw(); });

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.Settings";  // the compositor floats/centres/keeps this on top by app id
  cfg.title = "Fleetwm Settings";
  cfg.width = kWindowW;
  cfg.height = kWindowH;
  cfg.min_width = 640;
  cfg.min_height = 420;
  S.surface = std::make_unique<Surface>(S.app, cfg);
  S.surface->on_draw = [&S](cairo_t* cr, int w, int h) { S.draw(cr, w, h); };
  S.surface->on_motion = [&S](double x, double y) {
    if (S.mouse_test_remaining) ++S.mouse_test_events;
    if (!S.ui.pointer_motion(x, y)) return;
    S.redraw();
  };
  S.surface->on_leave = [&S] {
    S.ui.pointer_leave();
    S.redraw();
  };
  S.surface->on_button = [&S](double x, double y, uint32_t b, bool p) {
    S.ui.pointer_button(x, y, b, p);
    S.redraw();
  };
  S.surface->on_scroll = [&S](double, double dy) {
    S.ui.scroll(dy / 10.0);
    S.redraw();
  };
  S.surface->on_key = [&S](const KeyEvent& e) {
    if (e.pressed && S.ui.wants_paste() &&
        ((e.sym == XKB_KEY_v && (e.mods & kCtrl)) || (e.sym == XKB_KEY_Insert && (e.mods & kShift)))) {
      S.app.paste_text([&S](const std::string& text) {
        S.ui.paste(text);
        S.redraw();
      });
      return;
    }
    S.ui.key(e);
    S.redraw();
  };
  S.surface->on_closed = [&S] { S.app.quit(); };

  kit::watch_dirs(S.app, {std::filesystem::path(user_config_path()).parent_path().string()},
                  [&S] { S.reload_from_disk(); });

  S.mixer.start(
      [&S](int percent, bool muted, bool available) {
        S.master_available = available;
        S.master_muted = muted;
        S.master_value = percent;
        S.redraw();
      },
      [&S](const std::vector<common::AudioStream>& s) {
        S.streams = s;
        S.redraw();
      },
      [&S](std::function<void()> fn) { S.app.post(std::move(fn)); });

  if (S.has_battery) {
    S.update_battery();
    S.app.add_timer(15000, [&S] { S.update_battery(); });
  }

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  S.app.watch_fd(sfd, [&S] { S.app.quit(); });

  S.app.run();
  return 0;
}
