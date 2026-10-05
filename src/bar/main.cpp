// fleetwm-bar: GTK-free top bar. One layer-shell TOP surface drawn
// with cairo on wl_shm: workspace switcher, clock, CPU/GPU/Disk/Volume
// stats, system tray, battery + power-mode indicator, power button.
// Redraws only when something visible changed; otherwise it sleeps in poll().

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/signalfd.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "bar_config.hpp"
#include "battery_reading.hpp"
#include "ipc_client.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "theme.hpp"
#include "tray.hpp"
#include "volume_source.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

extern char** environ;

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;
using fleetwm::bar::Tray;
using fleetwm::bar::VolumeSource;

constexpr int kBarHeight = 24;
constexpr int kReconnectMs = 2000;
constexpr int kIslandMinMonitorWidth = 1366;
constexpr int kIslandTopMargin = 5;
constexpr int kIslandSideInset = 8;
constexpr double kFont = 14.67;  // GTK default 11pt at 96 dpi
constexpr uint32_t kBtnLeft = 0x110, kBtnRight = 0x111;

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

void spawn(const char* prog) {
  char* argv[] = {const_cast<char*>(prog), nullptr};
  pid_t pid;
  if (posix_spawnp(&pid, prog, nullptr, nullptr, argv, environ) != 0)
    std::fprintf(stderr, "fleetwm-bar: failed to launch %s\n", prog);
}

struct Bar {
  App app;
  ThemeConfig theme;
  BarConfig config;
  Palette pal;
  std::unique_ptr<Surface> surface;
  IpcClient ipc;
  int ipc_watch = 0, reconnect_timer = 0;

  // Sources (destroyed before `app`, declared after it).
  std::unique_ptr<VolumeSource> volume;
  std::unique_ptr<Tray> tray;

  // Display text.
  std::string clock_text = "--:--:--", cpu_text = "CPU --%", gpu_text = "GPU --%",
              disk_text = "Disk --%", vol_text = "Vol --%";
  int active_workspace = 0;
  BatteryReading battery;
  bool on_ac = true;
  std::string battery_dir;

  // CPU / GPU sampling state.
  unsigned long long prev_idle = 0, prev_total = 0;
  bool have_prev = false;
  int cpu_fd = -1, gpu_fd = -1;
  std::string gpu_path;
  bool gpu_nvidia = false;
  int gpu_tick = 0;
  bool gpu_query_running = false;

  // Hit rects, rebuilt on every draw.
  Rect ws_rect[10], vol_rect, power_rect, battery_rect;
  std::vector<Rect> tray_rects;
  int hover_power = 0;
  std::unique_ptr<Tooltip> tooltip;
  int tooltip_timer = 0;

  bool island = false;

  // -------------------------------------------------------------- layout --
  struct Metrics {
    cairo_t* cr;
    double text_w(const std::string& s, bool bold = false) { return measure_text(cr, s, kFont, bold).width; }
  };

  static constexpr double kStatPad = 8;      // 4px left + right
  static constexpr double kRightGap = 12;    // right box child spacing
  static constexpr double kBoxGap = 8;       // bar_box child spacing
  static constexpr double kMargin = 8;
  static constexpr double kTrayIcon = 16, kTraySpacing = 6;
  static constexpr double kBoltW = 8, kBatteryW = 26 + kStatPad + kBoltW, kModeW = 12 + kStatPad, kPowerW = 32;

  double ws_button_w(Metrics& m, int i) {
    char label[4];
    std::snprintf(label, sizeof label, "%d", (i + 1) % 10);
    return std::max(32.0, m.text_w(label) + 12);
  }
  double ws_total(Metrics& m) {
    double t = 9;  // 1px spacing x 9
    for (int i = 0; i < 10; ++i) t += ws_button_w(m, i);
    return t;
  }
  double tray_total() {
    const size_t n = tray->items().size();
    return n ? n * kTrayIcon + (n - 1) * kTraySpacing : 0;
  }
  double right_total(Metrics& m) {
    double t = 0;
    int children = 0;
    for (const std::string* s : {&cpu_text, &gpu_text, &disk_text, &vol_text}) {
      t += m.text_w(*s) + kStatPad;
      ++children;
    }
    t += tray_total();
    ++children;  // tray box always participates in the spacing
    t += kModeW + kBatteryW;  // power-mode glyph + battery/plug, shown on every machine
    children += 2;
    t += kPowerW;
    ++children;
    return t + kRightGap * (children - 1);
  }
  double natural_width(Metrics& m) {
    return 2 * kMargin + 4 * kBoxGap + ws_total(m) + m.text_w(clock_text, true) + right_total(m);
  }

  int monitor_width() {
    const auto& outs = app.outputs();
    if (outs.empty()) return 0;
    const int sc = std::max(1, outs[0].scale);
    return outs[0].width / sc;
  }

  // Applies Full/Island sizing to the layer surface.
  void apply_layout() {
    BarLayout layout = config.layout;
    const int mw = monitor_width();
    if (layout == BarLayout::Island && mw > 0 && mw < kIslandMinMonitorWidth) layout = BarLayout::Full;
    island = layout == BarLayout::Island;
    constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, L = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
                       R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (!island) {
      surface->set_anchor(T | L | R);
      surface->set_margins(0, 0, 0, 0);
      surface->set_exclusive_zone(kBarHeight);
      surface->set_size(0, kBarHeight);
    } else {
      cairo_surface_t* cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
      cairo_t* cr = cairo_create(cs);
      Metrics m{cr};
      const double natural = natural_width(m);
      cairo_destroy(cr);
      cairo_surface_destroy(cs);
      const double cap = mw > 0 ? mw - 2 * kIslandSideInset : natural;
      const int w = std::max(1, static_cast<int>(std::ceil(natural > 0 ? std::min(natural, cap) : cap)));
      surface->set_anchor(T);
      surface->set_margins(kIslandTopMargin, 0, 0, 0);
      surface->set_exclusive_zone(kBarHeight + kIslandTopMargin);
      surface->set_size(w, kBarHeight);
    }
    surface->queue_draw();
  }

  // -------------------------------------------------------------- drawing --
  void draw_power_glyph(cairo_t* cr, double cx, double cy, const Color& c) {
    cairo_save(cr);
    cairo_new_path(cr);
    set_source(cr, c);
    cairo_set_line_width(cr, 1.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_arc(cr, cx, cy + 0.5, 4.6, -M_PI / 2 + 0.6, -M_PI / 2 - 0.6 + 2 * M_PI);
    cairo_stroke(cr);
    cairo_move_to(cr, cx, cy - 6);
    cairo_line_to(cr, cx, cy - 0.5);
    cairo_stroke(cr);
    cairo_restore(cr);
  }

  void draw_mode_glyph(cairo_t* cr, double cx, double cy, const Color& c) {
    cairo_save(cr);
    cairo_new_path(cr);
    set_source(cr, c);
    cairo_set_line_width(cr, 1.3);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    switch (config.power_mode) {
      case PowerMode::Performance:  // lightning bolt
        cairo_move_to(cr, cx + 1.5, cy - 6);
        cairo_line_to(cr, cx - 3, cy + 0.5);
        cairo_line_to(cr, cx, cy + 0.5);
        cairo_line_to(cr, cx - 1.5, cy + 6);
        cairo_line_to(cr, cx + 3, cy - 0.5);
        cairo_line_to(cr, cx, cy - 0.5);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
      case PowerMode::BatterySaver:  // leaf
        cairo_move_to(cr, cx - 5, cy + 5);
        cairo_curve_to(cr, cx - 6, cy - 3, cx, cy - 6, cx + 5, cy - 5);
        cairo_curve_to(cr, cx + 6, cy + 1, cx + 1, cy + 6, cx - 5, cy + 5);
        cairo_close_path(cr);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - 5, cy + 5);
        cairo_line_to(cr, cx + 1, cy - 1);
        cairo_stroke(cr);
        break;
      case PowerMode::Normal:  // half-filled circle (balanced)
        cairo_arc(cr, cx, cy, 5, 0, 2 * M_PI);
        cairo_stroke(cr);
        cairo_arc(cr, cx, cy, 5, M_PI / 2, 3 * M_PI / 2);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    }
    cairo_restore(cr);
  }

  // Charging bolt, green like the battery fill.
  void draw_bolt(cairo_t* cr, double cx, double cy) {
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, 0.30, 0.85, 0.39, 1.0);
    cairo_move_to(cr, cx + 1.8, cy - 6);
    cairo_line_to(cr, cx - 2.8, cy + 0.8);
    cairo_line_to(cr, cx - 0.2, cy + 0.8);
    cairo_line_to(cr, cx - 1.8, cy + 6);
    cairo_line_to(cr, cx + 2.8, cy - 0.8);
    cairo_line_to(cr, cx + 0.2, cy - 0.8);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
  }

  // Mains plug: body, two prongs, cord.
  void draw_plug(cairo_t* cr, double cx, double cy, const Color& c) {
    cairo_save(cr);
    cairo_new_path(cr);
    set_source(cr, c);
    cairo_set_line_width(cr, 1.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx - 2.5, cy - 7);
    cairo_line_to(cr, cx - 2.5, cy - 3);
    cairo_move_to(cr, cx + 2.5, cy - 7);
    cairo_line_to(cr, cx + 2.5, cy - 3);
    cairo_stroke(cr);
    rounded_rect(cr, cx - 5, cy - 3, 10, 6.5, 2);
    cairo_fill(cr);
    cairo_move_to(cr, cx, cy + 3.5);
    cairo_line_to(cr, cx, cy + 7);
    cairo_stroke(cr);
    cairo_restore(cr);
  }

  void draw_battery(cairo_t* cr, double ox, double oy, const Color& fg) {
    const BatteryReading& r = battery;
    const double width = 26, height = 14;
    const double nub_w = 2.0, body_w = width - nub_w - 1.0, body_h = height, radius = 3.0, stroke_w = 1.3;
    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_new_path(cr);
    cairo_set_line_width(cr, stroke_w);
    set_source(cr, fg);
    const double x = stroke_w / 2.0, y = stroke_w / 2.0, w = body_w - stroke_w, h = body_h - stroke_w;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI_2, 0);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI_2);
    cairo_arc(cr, x + radius, y + h - radius, radius, M_PI_2, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_stroke(cr);
    const double nub_h = body_h * 0.4;
    cairo_rectangle(cr, body_w, (body_h - nub_h) / 2.0, nub_w, nub_h);
    cairo_fill(cr);
    const double pct = std::max(0, std::min(100, r.percent)) / 100.0;
    const double inset = stroke_w + 1.5;
    const double fill_w = std::max(0.0, (body_w - 2 * inset) * pct), fill_h = body_h - 2 * inset;
    if (r.charging || r.percent > 20) cairo_set_source_rgba(cr, 0.30, 0.85, 0.39, 1.0);
    else if (r.percent > 10) cairo_set_source_rgba(cr, 1.0, 0.62, 0.04, 1.0);
    else cairo_set_source_rgba(cr, 1.0, 0.23, 0.19, 1.0);
    if (fill_w > 0.0) {
      cairo_rectangle(cr, inset, inset, fill_w, fill_h);
      cairo_fill(cr);
    }
    set_source(cr, fg);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 8.5);
    const std::string text = std::to_string(r.percent);
    cairo_text_extents_t e{};
    cairo_text_extents(cr, text.c_str(), &e);
    cairo_move_to(cr, x + w / 2.0 - e.width / 2.0 - e.x_bearing, y + h / 2.0 - e.height / 2.0 - e.y_bearing);
    cairo_show_text(cr, text.c_str());
    cairo_new_path(cr);
    cairo_restore(cr);
  }

  void draw(cairo_t* cr, int W, int H) {
    Metrics m{cr};
    // Bar background (rounded unless the sharp corner style is selected;
    // the island is always a pill).
    const double radius = (island || pal.rounded) ? kBarHeight / 2.0 : 0;
    rounded_rect(cr, 0, 0, W, H, radius);
    set_source(cr, pal.bg_primary);
    cairo_fill(cr);

    const TextExtents fe = measure_text(cr, "Ag", kFont);
    const double base = (H - fe.height) / 2.0 + fe.ascent;

    const WorkspaceColors& wc = config.workspace_colors;
    const Color in_bg = parse_color(wc.inactive_bg, {0.235, 0.235, 0.235}),
                in_fg = parse_color(wc.inactive_fg, {1, 1, 1}),
                ac_bg = parse_color(wc.active_bg, {1, 0.47, 0}),
                ac_fg = parse_color(wc.active_fg, {0, 0, 0});
    const double btn_r = wc.buttons_rounded ? 6 : 0;

    const double nat = natural_width(m);
    const double spacer = std::max(0.0, (W - nat) / 2.0);

    double x = kMargin;
    // Workspace buttons.
    for (int i = 0; i < 10; ++i) {
      const double bw = ws_button_w(m, i), bh = 20, by = (H - bh) / 2.0;
      ws_rect[i] = {x, by, bw, bh};
      const bool active = i == active_workspace;
      rounded_rect(cr, x, by, bw, bh, btn_r);
      set_source(cr, active ? ac_bg : in_bg);
      cairo_fill(cr);
      char label[4];
      std::snprintf(label, sizeof label, "%d", (i + 1) % 10);
      const double tw = m.text_w(label);
      draw_text(cr, label, x + (bw - tw) / 2.0, base, kFont, active ? ac_fg : in_fg);
      x += bw + 1;
    }
    x += -1 + kBoxGap + spacer + kBoxGap;

    // Clock (accent, bold).
    draw_text(cr, clock_text, x, base, kFont, pal.accent, true);

    // Right group, laid out from the right edge inward.
    const double right_w = right_total(m);
    double rx = W - kMargin - right_w;
    auto stat = [&](const std::string& s, Rect* rect) {
      const double tw = m.text_w(s);
      if (rect) *rect = {rx, 0, tw + kStatPad, static_cast<double>(H)};
      draw_text(cr, s, rx + kStatPad / 2, base, kFont, pal.fg_secondary);
      rx += tw + kStatPad + kRightGap;
    };
    stat(cpu_text, nullptr);
    stat(gpu_text, nullptr);
    stat(disk_text, nullptr);
    stat(vol_text, &vol_rect);

    // Tray icons.
    const auto& items = tray->items();
    tray_rects.assign(items.size(), Rect{});
    for (size_t i = 0; i < items.size(); ++i) {
      const double iy = (H - kTrayIcon) / 2.0;
      tray_rects[i] = {rx, iy, kTrayIcon, kTrayIcon};
      if (items[i].icon) {
        cairo_save(cr);
        const double iw = cairo_image_surface_get_width(items[i].icon);
        const double ih = cairo_image_surface_get_height(items[i].icon);
        cairo_translate(cr, rx, iy);
        cairo_scale(cr, kTrayIcon / iw, kTrayIcon / ih);
        cairo_set_source_surface(cr, items[i].icon, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
      }
      rx += kTrayIcon + (i + 1 < items.size() ? kTraySpacing : 0);
    }
    rx += kRightGap;

    draw_mode_glyph(cr, rx + kModeW / 2, H / 2.0, pal.fg_secondary);
    rx += kModeW + kRightGap;
    battery_rect = {rx, 0, kBatteryW, static_cast<double>(H)};
    if (battery.available) {
      if (battery.charging) draw_bolt(cr, rx + kStatPad / 2 + kBoltW / 2.0, H / 2.0);
      draw_battery(cr, rx + kStatPad / 2 + kBoltW, (H - 14) / 2.0, pal.fg_secondary);
    } else {
      draw_plug(cr, rx + kBatteryW / 2, H / 2.0, pal.fg_secondary);
    }
    rx += kBatteryW + kRightGap;

    // Power button.
    const double pbh = 20, pby = (H - pbh) / 2.0;
    power_rect = {rx, pby, kPowerW, pbh};
    rounded_rect(cr, rx, pby, kPowerW, pbh, btn_r);
    set_source(cr, in_bg);
    cairo_fill(cr);
    draw_power_glyph(cr, rx + kPowerW / 2, H / 2.0, hover_power ? pal.accent : in_fg);
  }

  // -------------------------------------------------------------- sources --
  std::string format_clock() {
    const ClockFormat& fmt = config.clock;
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char tb[16];
    std::strftime(tb, sizeof tb, fmt.show_seconds ? "%H:%M:%S" : "%H:%M", &lt);
    std::string label = tb;
    if (fmt.show_date) {
      std::string date;
      auto part = [&](bool on, const char* f) {
        if (!on) return;
        char b[8];
        std::strftime(b, sizeof b, f, &lt);
        if (!date.empty()) date += "-";
        date += b;
      };
      part(fmt.show_year, "%Y");
      part(fmt.show_month, "%m");
      part(fmt.show_day, "%d");
      if (!date.empty()) label += "  " + date;
    }
    return label;
  }

  bool set_if_changed(std::string& dst, const std::string& v) {
    if (dst == v) return false;
    dst = v;
    return true;
  }

  bool update_cpu() {
    if (cpu_fd < 0) {
      cpu_fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
      if (cpu_fd < 0) return false;
    }
    char buf[512];
    const ssize_t got = pread(cpu_fd, buf, sizeof buf - 1, 0);
    if (got <= 0) return false;
    buf[got] = 0;
    unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iow = 0, irq = 0, sirq = 0, steal = 0;
    if (std::sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &sys, &idle, &iow,
                    &irq, &sirq, &steal) < 4)
      return false;
    const unsigned long long idle_total = idle + iow;
    const unsigned long long total = user + nice + sys + idle + iow + irq + sirq + steal;
    bool changed = false;
    if (have_prev && total > prev_total) {
      const unsigned long long td = total - prev_total, id = idle_total - prev_idle;
      const int pct = static_cast<int>(100.0 * static_cast<double>(td - id) / static_cast<double>(td) + 0.5);
      changed = set_if_changed(cpu_text, "CPU " + std::to_string(pct) + "%");
    }
    prev_idle = idle_total;
    prev_total = total;
    have_prev = true;
    return changed;
  }

  void init_gpu() {
    for (int card = 0; card < 8; ++card) {
      const std::string base = "/sys/class/drm/card" + std::to_string(card);
      if (std::ifstream(base + "/device/gpu_busy_percent").good()) {
        gpu_path = base + "/device/gpu_busy_percent";
        return;
      }
      if (std::ifstream(base + "/gt_busy_percent").good()) {
        gpu_path = base + "/gt_busy_percent";
        return;
      }
    }
    if (const char* path = std::getenv("PATH")) {
      std::string p = path;
      size_t pos = 0;
      while (pos <= p.size()) {
        size_t e = p.find(':', pos);
        if (e == std::string::npos) e = p.size();
        const std::string cand = p.substr(pos, e - pos) + "/nvidia-smi";
        if (access(cand.c_str(), X_OK) == 0) {
          gpu_nvidia = true;
          break;
        }
        pos = e + 1;
      }
    }
    if (!gpu_nvidia) gpu_text = "GPU N/A";
  }

  bool update_gpu() {
    if (!gpu_path.empty()) {
      if (gpu_fd < 0) {
        gpu_fd = open(gpu_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (gpu_fd < 0) return false;
      }
      char buf[32];
      const ssize_t got = pread(gpu_fd, buf, sizeof buf - 1, 0);
      if (got <= 0) return false;
      buf[got] = 0;
      return set_if_changed(gpu_text, "GPU " + std::to_string(std::atoi(buf)) + "%");
    }
    if (gpu_nvidia && ++gpu_tick >= 3 && !gpu_query_running) {
      gpu_tick = 0;
      gpu_query_running = true;
      // nvidia-smi can take a while; keep it off the UI thread.
      std::thread([this] {
        int pct = -1;
        if (FILE* p = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r")) {
          if (std::fscanf(p, "%d", &pct) != 1) pct = -1;
          pclose(p);
        }
        app.post([this, pct] {
          gpu_query_running = false;
          if (pct >= 0 && set_if_changed(gpu_text, "GPU " + std::to_string(pct) + "%")) redraw();
        });
      }).detach();
    }
    return false;
  }

  bool update_disk() {
    struct statvfs v {};
    if (statvfs("/", &v) != 0) return set_if_changed(disk_text, "Disk N/A");
    if (v.f_blocks == 0) return false;
    const int pct = static_cast<int>(100.0 * static_cast<double>(v.f_blocks - v.f_bfree) /
                                         static_cast<double>(v.f_blocks) + 0.5);
    return set_if_changed(disk_text, "Disk " + std::to_string(pct) + "%");
  }

  bool update_battery() {
    const BatteryReading r = battery_internal::read_battery_reading(battery_dir);
    const bool ac = ac_online();
    const bool changed = r.available != battery.available || r.percent != battery.percent ||
                         r.charging != battery.charging || ac != on_ac;
    battery = r;
    on_ac = ac;
    if (changed) apply_layout_if_island();
    return changed;
  }

  void apply_layout_if_island() {
    if (island) apply_layout();  // natural width changes when the battery appears
  }

  void redraw() {
    if (surface) surface->queue_draw();
  }

  void clock_tick() {
    bool ch = set_if_changed(clock_text, format_clock());
    ch |= update_cpu();
    ch |= update_gpu();
    if (ch) {
      if (island) apply_layout();  // text widths changed, island width follows
      redraw();
    }
  }

  // ------------------------------------------------------------------ ipc --
  void handle_ipc_line(const std::string& line) {
    int ws = -1;
    try {
      if (line.rfind("WORKSPACE_CHANGED ", 0) == 0) ws = std::stoi(line.substr(18));
      else if (!line.empty() && line.find_first_not_of("0123456789") == std::string::npos) ws = std::stoi(line);
    } catch (...) {
      return;
    }
    if (ws >= 0 && ws != active_workspace) {
      active_workspace = ws;
      redraw();
    }
  }

  void try_connect() {
    if (ipc.connect()) {
      ipc_watch = app.watch_fd(ipc.fd(), [this] {
        ipc.poll_lines([this](const std::string& l) { handle_ipc_line(l); });
        if (!ipc.is_connected()) {
          app.unwatch(ipc_watch);
          ipc_watch = 0;
          try_connect();
        }
      });
      ipc.send_command("WORKSPACE?");
      return;
    }
    if (!reconnect_timer) {
      reconnect_timer = app.add_timer(kReconnectMs, [this] {
        if (ipc.is_connected()) {
          app.unwatch(reconnect_timer);
          reconnect_timer = 0;
          return;
        }
        try_connect();
      });
    }
  }

  // ---------------------------------------------------------------- input --
  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (!pressed) return;
    if (b == kBtnLeft) {
      for (int i = 0; i < 10; ++i)
        if (ws_rect[i].hit(x, y)) {
          ipc.send_command("WORKSPACE " + std::to_string(i));
          return;
        }
      if (vol_rect.hit(x, y)) return spawn("fleetwm-audiomixer");
      if (power_rect.hit(x, y)) return spawn("fleetwm-powermenu");
    }
    for (size_t i = 0; i < tray_rects.size(); ++i)
      if (tray_rects[i].hit(x, y)) {
        const int ax = static_cast<int>(x), ay = static_cast<int>(y);
        tray->click(i, b, ax, ay);
        return;
      }
  }

  std::string battery_tooltip_text() {
    if (!battery.available) return on_ac ? "On AC power" : "No battery";
    std::string t = std::to_string(battery.percent) + "% " + (battery.charging ? "(charging)" : on_ac ? "(on AC, not charging)" : "(on battery)");
    if (battery.hours_remaining >= 0.0) {
      const int mins = static_cast<int>(battery.hours_remaining * 60.0 + 0.5);
      t += " - " + std::to_string(mins / 60) + "h " + std::to_string(mins % 60) + "m " +
           (battery.charging ? "until full" : "remaining");
    }
    return t;
  }

  void hide_tooltip() {
    if (tooltip_timer) {
      app.unwatch(tooltip_timer);
      tooltip_timer = 0;
    }
    tooltip.reset();
  }

  void on_motion(double x, double y) {
    const int h = power_rect.hit(x, y) ? 1 : 0;
    if (h != hover_power) {
      hover_power = h;
      redraw();
    }
    const bool over_battery = battery_rect.hit(x, y);
    if (!over_battery) {
      hide_tooltip();
    } else if (!tooltip && !tooltip_timer) {
      const double cx = battery_rect.x + battery_rect.w / 2;
      tooltip_timer = app.add_oneshot(500, [this, cx] {
        tooltip_timer = 0;
        const int left = island ? island_left() : 0;
        tooltip = std::make_unique<Tooltip>(app, pal, battery_tooltip_text(), left + static_cast<int>(cx),
                                            (island ? kIslandTopMargin : 0) + kBarHeight + 4);
      });
    }
  }

  // Island: surface is centered horizontally by the compositor.
  int island_left() {
    const int mw = monitor_width();
    return mw > 0 ? std::max(0, (mw - surface->width()) / 2) : 0;
  }

  void reload_config() {
    theme = load_theme_config();
    config = load_bar_config();
    pal = load_palette(theme);
    apply_layout();
    redraw();
  }
};

}  // namespace

int main() {
  fleetwm::tune_malloc_for_low_rss();
  signal(SIGCHLD, SIG_IGN);  // spawned helpers are fire-and-forget

  Bar B;
  B.theme = load_theme_config();
  B.config = load_bar_config();
  B.pal = load_palette(B.theme);
  if (!B.app.connect()) return 1;

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.height = kBarHeight;
  cfg.exclusive_zone = kBarHeight;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
  cfg.name = "fleetwm-bar";
  B.surface = std::make_unique<Surface>(B.app, cfg);
  B.surface->on_draw = [&B](cairo_t* cr, int w, int h) { B.draw(cr, w, h); };
  B.surface->on_button = [&B](double x, double y, uint32_t b, bool p) { B.on_button(x, y, b, p); };
  B.surface->on_motion = [&B](double x, double y) { B.on_motion(x, y); };
  B.surface->on_leave = [&B] {
    B.hide_tooltip();
    if (B.hover_power) {
      B.hover_power = 0;
      B.redraw();
    }
  };
  B.surface->on_closed = [&B] { B.app.quit(); };

  B.tray = std::make_unique<Tray>(B.app, [&B] {
    if (B.island) B.apply_layout();
    B.redraw();
  });
  B.tray->start();

  B.volume = std::make_unique<VolumeSource>(B.app);
  B.volume->start([&B](int percent, bool available) {
    if (B.set_if_changed(B.vol_text, available ? "Vol " + std::to_string(percent) + "%" : "Vol N/A")) {
      if (B.island) B.apply_layout();
      B.redraw();
    }
  });

  B.init_gpu();
  B.update_disk();
  B.battery_dir = find_battery_dir();
  if (const char* d = std::getenv("FLEETWM_BATTERY_DIR")) B.battery_dir = d;  // test hook
  B.update_battery();
  B.clock_tick();
  B.apply_layout();
  B.try_connect();

  B.app.add_timer(1000, [&B] { B.clock_tick(); });
  B.app.add_timer(5000, [&B] {
    if (B.update_disk()) B.redraw();
  });
  B.app.add_timer(15000, [&B] {
    if (B.update_battery()) B.redraw();
  });
  B.app.on_outputs_changed = [&B] { B.apply_layout(); };

  kit::watch_dirs(B.app,
                   {std::filesystem::path(user_config_path()).parent_path().string(),
                    std::filesystem::path(bar_user_config_path()).parent_path().string()},
                   [&B] { B.reload_config(); });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  B.app.watch_fd(sfd, [&B] { B.app.quit(); });

  B.app.run();
  B.volume.reset();
  return 0;
}
