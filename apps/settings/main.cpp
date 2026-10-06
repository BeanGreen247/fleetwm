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
#include <cmath>
#include <fstream>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

#include "audio_mixer.hpp"
#include "bar_config.hpp"
#include "battery_reading.hpp"
#include "power_config.hpp"
#include "settings_pages.hpp"
#include "default_apps.hpp"
#include "ipc_client.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "keyboard_tab.hpp"
#include "network_tab.hpp"
#include "mimeapps.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "ui.hpp"
#include "wallpaper_config.hpp"

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
  App app;
  Palette pal;
  Ui ui{Palette{}};
  std::unique_ptr<Surface> surface;

  ThemeConfig config;
  BarConfig bar;
  WallpaperConfig wallpaper;
  DefaultAppsConfig default_apps;

  int tab = 0;
  double scroll[12] = {};
  std::vector<std::string> tab_names{"Theme", "Bar", "Wallpaper", "Display", "Network", "Keyboard", "Power", "Date & Time", "Default Apps", "Audio", "Performance", "About"};

  // network
  std::unique_ptr<NetworkTab> net_tab;
  std::unique_ptr<KeyboardTab> kb_tab;

  // power
  bool has_battery = false;
  std::string battery_dir;
  BatteryReading battery;
  PowerConfig power;

  // default apps
  std::vector<DesktopEntry> entries;
  std::vector<MimeRow> mime_rows;
  std::vector<int> terminal_apps;
  int terminal_selected = -1;

  TimeInfo tinfo;

  // display
  std::vector<DispMon> mons;
  std::string disp_status;
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
    if (kb_tab) kb_tab->reload();
    apply_theme();
    redraw();
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
      std::string cmdline;
      for (const auto& a : argv) cmdline += (cmdline.empty() ? "" : " ") + a;
      const int index = static_cast<int>(terminal_apps.size());
      if (default_apps.terminal_command == cmdline) exact_match = index;
      if (!argv.empty() && default_apps.terminal_command == argv[0]) executable_match = index;
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

    ui.section("Clock");
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
  void tab_theme(cairo_t*) {
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
    ui.row("Glass effects");
    if (ui.checkbox("Translucent frames and menus", &config.glass)) save_theme();
    ui.newline();
    ui.label("The bar, the taskbar, titlebars, the start menu and the Alt+Tab panel get a see-through, frosted look. Off is flat and matte.", true);
    ui.newline();
    const bool tiling = config.window_layout == WindowLayout::Tiling;
    const bool desktop = !tiling;
    ui.label(tiling ? "Tiling: windows tile automatically; keyboard shortcuts drive everything (Alt+Shift+/ lists them)."
                    : "Desktop: floating windows with titlebars, a taskbar and a start menu. Super+/ lists the shortcuts.",
             true);
    ui.newline();

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
    ui.label("Apps that keep the screen awake (video, presentations) are respected.", true);
    ui.newline();

    ui.space(6);
    ui.section("Power mode");
    ui.row("Power mode");
    int mode = static_cast<int>(bar.power_mode);
    if (ui.segmented({"Normal", "Performance", "Battery Saver"}, &mode)) {
      bar.power_mode = static_cast<PowerMode>(mode);
      save_bar();  // the bar picks this up live
      spawn_detached({"powerprofilesctl", "set", power_mode_to_profiles_daemon_name(bar.power_mode)});
    }
    ui.newline();
  }

  void tab_bar(cairo_t*) {
    const bool tiling = config.window_layout == WindowLayout::Tiling;
    const bool desktop = !tiling;
    ui.label(tiling ? "Tiling layout: a top bar with workspace buttons. The taskbar settings below are not in use."
                    : "Desktop layout: the bar is a taskbar. The top-bar settings below are not in use.",
             true);
    ui.newline();
    ui.space(6);

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
    if (any_override && ui.button("Reset to theme colors", tiling)) {
      for (auto& c : colors) c.field->clear();
      save_bar();
    }
    ui.newline();
    ui.space(4);
    if (ui.checkbox("Rounded workspace buttons", &bar.workspace_colors.buttons_rounded, tiling)) save_bar();
    ui.newline();

    ui.space(8);
    ui.section(desktop ? "Taskbar (Desktop layout)" : "Taskbar (Desktop layout, not in use)");
    ui.row("Taskbar position");
    int tpos = static_cast<int>(bar.taskbar_position);
    if (ui.segmented({"Bottom", "Top", "Left", "Right"}, &tpos, desktop)) {
      bar.taskbar_position = static_cast<TaskbarPosition>(tpos);
      save_bar();
    }
    ui.newline();
    ui.row("Window buttons");
    if (ui.checkbox("Rounded corners", &bar.taskbar_rounded, desktop)) save_bar();
    ui.newline();
    ui.label("Start menu, one button per window, and the clock and status widgets on the right.", true);
    ui.newline();
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
          default_apps.terminal_command = argv[0];
          save_default_apps_config(default_apps);
        }
      }
    }
    ui.newline();
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
  }

  // ---------------------------------------------------------------- draw --
  void draw(cairo_t* cr, int w, int h) {
    ui.begin(cr, w, h);
    // Sidebar.
    {
      const Palette& p = ui.palette();
      cairo_rectangle(cr, 0, 0, kSidebarW, h);
      set_source(cr, p.bg_secondary);
      cairo_fill(cr);
      cairo_rectangle(cr, kSidebarW - 1, 0, 1, h);
      Color line = p.fg_secondary;
      line.a = 0.18;
      set_source(cr, line);
      cairo_fill(cr);
      draw_text(cr, "Settings", 24, 38, 17, p.fg_primary, true);
    }
    ui.nav(tab_names, &tab, {0, 52, kSidebarW, static_cast<double>(h) - 52});

    // Content: title + the tab's sections in a scrolling pane.
    ui.set_margins(28, 8, 28);
    ui.set_label_width(220);
    ui.begin_scroll({kSidebarW, 0, static_cast<double>(w) - kSidebarW, static_cast<double>(h)}, &scroll[tab]);
    ui.space(10);
    ui.title(tab_names[static_cast<size_t>(tab)]);
    switch (tab) {
      case 0: tab_theme(cr); break;
      case 1: tab_bar(cr); break;
      case 2: tab_wallpaper(cr); break;
      case 3: tab_display(cr); break;
      case 4: net_tab->draw(ui, cr); break;
      case 5: kb_tab->draw(ui, cr); break;
      case 6: tab_power(cr); break;
      case 7: tab_datetime(cr); break;
      case 8: tab_default_apps(cr); break;
      case 9: tab_audio(cr); break;
      case 10: tab_performance(cr); break;
      default: tab_about(cr); break;
    }
    ui.space(24);
    ui.end_scroll();
    ui.end();
    if (ui.wants_another_frame()) redraw();
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::tune_malloc_for_low_rss();
  signal(SIGCHLD, SIG_IGN);

  Settings S;
  S.config = load_theme_config();
  S.bar = load_bar_config();
  S.wallpaper = load_wallpaper_config();
  S.default_apps = load_default_apps_config();
  S.power = load_power_config();
  S.apply_theme();
  S.net_tab = std::make_unique<NetworkTab>(S.app, [&S] { S.redraw(); });
  // `fleetwm-settings --page power` opens on that page (the bar's battery icon uses it).
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--page") {
      const int page = find_settings_page(S.tab_names, argv[i + 1]);
      if (page >= 0) S.tab = page;
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
    S.ui.pointer_motion(x, y);
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
