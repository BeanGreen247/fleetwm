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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "bar_config.hpp"
#include "desktop_entry.hpp"
#include "icon_theme.hpp"
#include "window_geometry.hpp"
#include "window_list.hpp"
#include "workspace_list.hpp"
#include "battery_reading.hpp"
#include "ipc_client.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "theme.hpp"
#include "backdrop.hpp"
#include "keyboard_config.hpp"
#include "network_glyphs.hpp"
#include "network_parse.hpp"
#include "network_types.hpp"
#include "system_devices.hpp"
#include "tray.hpp"
#include "xkb_rules.hpp"
#include "volume_source.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

extern char** environ;

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;
using fleetwm::bar::Tray;
using fleetwm::bar::VolumeSource;

constexpr int kBarHeight = 30;
constexpr int kCapsuleTopMargin = 6, kCapsuleSideMargin = 8;
constexpr double kWsH = 22;  // workspace button height
constexpr int kReconnectMs = 2000;
constexpr int kIslandMinMonitorWidth = 1366;
constexpr int kIslandTopMargin = 5;
constexpr int kIslandSideInset = 8;
constexpr double kFont = 13.5;
constexpr uint32_t kBtnMiddle = 0x112;
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

// Opens Settings on the named page (e.g. the Power page from the battery icon).
void spawn_settings_page(const char* page) {
  char* argv[] = {const_cast<char*>("fleetwm-settings"), const_cast<char*>("--page"), const_cast<char*>(page), nullptr};
  pid_t pid;
  if (posix_spawnp(&pid, "fleetwm-settings", nullptr, nullptr, argv, environ) != 0)
    std::fprintf(stderr, "fleetwm-bar: failed to launch fleetwm-settings\n");
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
  std::string clock_text = "--:--:--", cpu_text = "CPU --%", ram_text = "RAM --%", gpu_text = "GPU --%",
              disk_text = "Disk --%", vol_text = "Vol --%";
  int active_workspace = 0;
  BatteryReading battery;
  bool on_ac = true;
  std::string battery_dir;

  // CPU / GPU sampling state.
  unsigned long long prev_idle = 0, prev_total = 0;
  bool have_prev = false;
  int cpu_fd = -1;
  // One entry per GPU found. AMD has a "busy percent" file; Intel has none, so its idle time (RC6
  // residency) over wall-clock time gives the share of time it was awake; NVIDIA is asked through
  // nvidia-smi in the background (one line per card).
  struct Gpu {
    enum class Kind { AmdBusy, IntelIdle, Nvidia } kind = Kind::AmdBusy;
    std::string vendor, driver;  // "AMD", "amdgpu"
    std::string path;            // the busy or idle-residency file
    std::string freq_path, freq_max_path;
    int fd = -1;
    long long idle_prev_ms = -1;
    std::chrono::steady_clock::time_point idle_prev_time;
    int percent = -1;
  };
  std::vector<Gpu> gpus;
  int nvidia_cards = 0;  // how many nvidia-smi reported, once it has answered
  int gpu_tick = 0;
  bool gpu_query_running = false;
  bool has_nvidia_smi = false;

  // Hit rects, rebuilt on every draw.
  // Glass look (theme.toml glass_effects): the blurred wallpaper behind translucent surfaces.
  bool glass = false;
  cairo_surface_t* backdrop = nullptr;
  void refresh_glass() {
    glass = theme.glass;
    if (backdrop) cairo_surface_destroy(backdrop);
    backdrop = glass ? load_backdrop() : nullptr;
  }
  // Paints the bar background of one rectangle: flat, or glass when that is on. (sx, sy) is where the
  // rectangle sits on the screen, which is what the backdrop is lined up with.
  void bar_surface(cairo_t* cr, double x, double y, double w, double h, double radius, double sx, double sy, double flat_alpha) {
    if (glass) {
      GlassStyle st;
      st.tint = pal.bg_primary;
      st.tint_alpha = 0.55;
      st.radius = radius;
      paint_glass(cr, backdrop, monitor_width(), monitor_height(), sx, sy, x, y, w, h, st);
      return;
    }
    Color bg = pal.bg_primary;
    bg.a = flat_alpha;
    rounded_rect(cr, x, y, w, h, radius);
    set_source(cr, bg);
    cairo_fill(cr);
  }
  // Higher contrast than the theme's secondary text: glyphs and labels lean most of the way to the
  // primary colour so they stay readable on translucent and busy backgrounds too.
  Color icon_fg() const {
    return {pal.fg_secondary.r + (pal.fg_primary.r - pal.fg_secondary.r) * 0.85,
            pal.fg_secondary.g + (pal.fg_primary.g - pal.fg_secondary.g) * 0.85,
            pal.fg_secondary.b + (pal.fg_primary.b - pal.fg_secondary.b) * 0.85, 1.0};
  }
  Color soft_fg() const {
    return {pal.fg_secondary.r + (pal.fg_primary.r - pal.fg_secondary.r) * 0.6,
            pal.fg_secondary.g + (pal.fg_primary.g - pal.fg_secondary.g) * 0.6,
            pal.fg_secondary.b + (pal.fg_primary.b - pal.fg_secondary.b) * 0.6, 1.0};
  }
  Rect net_rect, layout_rect;
  // Where the clock was drawn last and how wide its text was: a tick that keeps the width repaints
  // only this strip (the glass behind it is repainted inside the clip, so it stays seamless).
  static constexpr double kClockPad = 6;
  Rect clock_rect;
  double clock_w_drawn = 0;
  cairo_surface_t* scratch_surface = nullptr;
  cairo_t* scratch = nullptr;
  int island_w_applied = -1;  // width the island surface was last sized to
  void ensure_scratch() {
    if (scratch) return;
    scratch_surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    scratch = cairo_create(scratch_surface);
  }
  // Text changed in the island: re-send the layout only when its width really moved (a ticking
  // clock keeps the same width almost every second). Returns true when the layout was re-applied.
  bool island_resize_if_needed() {
    if (!island) return false;
    ensure_scratch();
    Metrics m{scratch};
    const int mw = monitor_width();
    const double natural = natural_width(m), cap = mw > 0 ? mw - 2 * kIslandSideInset : natural;
    const int w = std::max(1, static_cast<int>(std::ceil(natural > 0 ? std::min(natural, cap) : cap)));
    if (w == island_w_applied) return false;
    apply_layout();
    return true;
  }
  double clock_width_now() {
    ensure_scratch();
    if (!taskbar) return measure_text(scratch, clock_text, kFont, true).width;
    const size_t split = clock_text.find("  ");
    const std::string t = split == std::string::npos ? clock_text : clock_text.substr(0, split);
    const std::string d = split == std::string::npos ? "" : clock_text.substr(split + 2);
    return std::max(measure_text(scratch, t, kFont, true).width, d.empty() ? 0.0 : measure_text(scratch, d, kSmallFont).width);
  }
  std::vector<KeyboardLayout> kb_layouts;  // from the compositor (LAYOUTS lines)
  int kb_current = 0;
  net::Device net_dev;  // the card the network icon stands for
  bool net_have = false;
  Rect ws_rect[10], vol_rect, power_rect, battery_rect, ram_rect, cpu_rect, gpu_rect, disk_rect;
  int tooltip_for = 0;  // 1 battery, 2 RAM, 3 CPU, 4 GPU, 5 disk, 6 volume, 1000+id window
  std::vector<Rect> tray_rects;
  int hover_power = 0;
  std::unique_ptr<Tooltip> tooltip;
  int tooltip_timer = 0;

  bool island = false;
  BarLayout layout_style = BarLayout::Capsules;

  // ---- Desktop layout: taskbar ----
  bool taskbar = false;  // Desktop window layout: taskbar instead of capsules/island/strip
  TaskbarPosition tb_pos = TaskbarPosition::Bottom;
  std::vector<WindowEntry> windows;  // every window the compositor reported
  std::vector<WindowEntry> shown;    // the ones the taskbar lists: this workspace's, plus pinned
  Rect start_rect;
  std::vector<Rect> win_rects;  // parallel to `windows` (zero-size = not shown)
  int hover_win = -1;           // index into `windows`
  bool hover_start = false;
  int hover_workspace = -1;
  std::map<std::string, cairo_surface_t*> win_icons;
  std::map<std::string, std::string> app_icon_names;  // lower-case app id / exec / name -> icon
  bool app_icons_loaded = false;

  // The taskbar lists the current workspace's windows (and pinned ones, which are on
  // every workspace); the rest appear when you switch to their workspace.
  void rebuild_shown() {
    shown.clear();
    for (const WindowEntry& w : windows)
      if (w.pinned || w.workspace == active_workspace) shown.push_back(w);
  }

  // Workspace buttons (the ones the user asked for, 1-10). Laid out in a row, or in two
  // columns when the taskbar is vertical. Returns the far edge (x for a row, y for columns).
  double draw_pager(cairo_t* cr, double x0, double y0, double btn_w, double btn_h, bool columns) {
    const std::vector<int> list = ws_visible();
    for (Rect& r : ws_rect) r = Rect{};
    const Color idle = with_alpha(pal.fg_secondary, 0.9);
    double edge = columns ? y0 : x0;
    for (size_t k = 0; k < list.size(); ++k) {
      const int i = list[k];
      const double x = columns ? x0 + (k % 2) * (btn_w + 3) : x0 + k * (btn_w + 3);
      const double y = columns ? y0 + (k / 2) * (btn_h + 3) : y0;
      ws_rect[i] = {x, y, btn_w, btn_h};
      const bool active = i == active_workspace;
      if (active || i == hover_workspace) {
        rounded_rect(cr, x, y, btn_w, btn_h, pal.rounded ? 6 : 2);
        set_source(cr, active ? with_alpha(pal.accent, 0.9) : with_alpha(pal.accent, 0.18));
        cairo_fill(cr);
      }
      char label[4];
      std::snprintf(label, sizeof label, "%d", (i + 1) % 10);
      const TextExtents te = measure_text(cr, label, kFont, active);
      draw_text(cr, label, x + (btn_w - te.width) / 2, y + (btn_h - te.height) / 2 + te.ascent, kFont,
                active ? pal.bg_primary : idle, active);
      edge = columns ? y + btn_h : x + btn_w;
    }
    return edge;
  }

  bool vertical() const { return tb_pos == TaskbarPosition::Left || tb_pos == TaskbarPosition::Right; }

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
  static constexpr double kBoltW = 8, kBatteryW = 26 + kStatPad + kBoltW, kModeW = 12 + kStatPad, kPowerW = 30, kNetW = 18 + kStatPad;

  // The workspace buttons shown right now (see visible_workspaces()).
  std::vector<int> ws_visible() const {
    std::vector<int> occupied;
    for (const WindowEntry& w : windows) occupied.push_back(w.workspace);
    return visible_workspaces(occupied, active_workspace, config.taskbar_workspaces);
  }
  std::vector<int> ws_last;  // what the island layout was last sized for

  // The island bar is as wide as its content, so it re-lays-out when buttons come or go.
  void ws_list_changed() {
    const std::vector<int> now = ws_visible();
    if (now == ws_last) return;
    ws_last = now;
    if (island) apply_layout();
  }

  double ws_button_w(Metrics& m, int i) {
    char label[4];
    std::snprintf(label, sizeof label, "%d", (i + 1) % 10);
    return std::max(26.0, m.text_w(label) + 12);
  }
  double ws_total(Metrics& m) {
    const std::vector<int> list = ws_visible();
    double t = list.empty() ? 0 : static_cast<double>(list.size()) - 1;  // 1px spacing between
    for (int i : list) t += ws_button_w(m, i);
    return t;
  }
  double tray_total() {
    const size_t n = tray->items().size();
    return n ? n * kTrayIcon + (n - 1) * kTraySpacing : 0;
  }
  // Desktop taskbar: CPU/RAM/GPU/Disk collapse into a 2x2 grid in a smaller font.
  static constexpr double kSmallFont = 11.5, kGridGap = 6;
  bool compact_stats() const { return taskbar && !vertical(); }
  double small_w(Metrics& m, const std::string& s) { return measure_text(m.cr, s, kSmallFont).width; }
  void grid_columns(Metrics& m, double* col_a, double* col_b) {
    *col_a = std::max(small_w(m, cpu_text), small_w(m, gpu_text));
    *col_b = std::max(small_w(m, ram_text), small_w(m, disk_text));
  }

  // The battery widget is the icon plus, when there is a battery, its percentage as text.
  std::string battery_percent_text() const { return std::to_string(battery.percent) + "%"; }
  double battery_w(Metrics& m) {
    return kBatteryW + (battery.available ? m.text_w(battery_percent_text()) + 6 : 0.0);
  }

  double right_total(Metrics& m) {
    double t = 0;
    int children = 0;
    if (compact_stats()) {
      double a, b;
      grid_columns(m, &a, &b);
      t += a + b + kGridGap + kStatPad;
      ++children;
      t += m.text_w(vol_text) + kStatPad;
      ++children;
    } else {
      for (const std::string* s : {&cpu_text, &ram_text, &gpu_text, &disk_text, &vol_text}) {
        t += m.text_w(*s) + kStatPad;
        ++children;
      }
    }
    t += tray_total();
    ++children;  // tray box always participates in the spacing
    if (!kb_layouts.empty()) {
      t += layout_pill_w(m);
      ++children;
    }
    if (net_have) {
      t += kNetW;
      ++children;
    }
    t += kModeW + battery_w(m);  // power-mode glyph + battery/plug, shown on every machine
    children += 2;
    t += kPowerW;
    ++children;
    return t + kRightGap * (children - 1);
  }
  double natural_width(Metrics& m) {
    return 2 * kMargin + 4 * kBoxGap + ws_total(m) + m.text_w(clock_text, true) + right_total(m);
  }

  int monitor_height() {
    const auto& outs = app.outputs();
    if (outs.empty()) return 0;
    const int sc = std::max(1, outs[0].scale);
    return outs[0].height / sc;
  }

  int monitor_width() {
    const auto& outs = app.outputs();
    if (outs.empty()) return 0;
    const int sc = std::max(1, outs[0].scale);
    return outs[0].width / sc;
  }

  // Applies Full/Island sizing to the layer surface.
  void apply_layout() {
    taskbar = theme.window_layout == WindowLayout::Desktop;
    tb_pos = config.taskbar_position;
    hide_tooltip();  // a tooltip placed for the previous layout would be left hanging
    if (taskbar) {
      constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, B = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                         L = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
      surface->set_margins(0, 0, 0, 0);
      switch (tb_pos) {
        case TaskbarPosition::Bottom:
          surface->set_anchor(B | L | R);
          surface->set_size(0, kTaskbarThickness);
          surface->set_exclusive_zone(kTaskbarThickness);
          break;
        case TaskbarPosition::Top:
          surface->set_anchor(T | L | R);
          surface->set_size(0, kTaskbarThickness);
          surface->set_exclusive_zone(kTaskbarThickness);
          break;
        case TaskbarPosition::Left:
          surface->set_anchor(L | T | B);
          surface->set_size(kTaskbarWidth, 0);
          surface->set_exclusive_zone(kTaskbarWidth);
          break;
        case TaskbarPosition::Right:
          surface->set_anchor(R | T | B);
          surface->set_size(kTaskbarWidth, 0);
          surface->set_exclusive_zone(kTaskbarWidth);
          break;
      }
      island = false;
      surface->queue_draw();
      return;
    }
    BarLayout layout = config.layout;
    const int mw = monitor_width();
    if (layout == BarLayout::Island && mw > 0 && mw < kIslandMinMonitorWidth) layout = BarLayout::Full;
    island = layout == BarLayout::Island;
    layout_style = layout;
    constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, L = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
                       R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (layout == BarLayout::Capsules) {
      surface->set_anchor(T | L | R);
      surface->set_margins(kCapsuleTopMargin, kCapsuleSideMargin, 0, kCapsuleSideMargin);
      surface->set_exclusive_zone(kBarHeight + kCapsuleTopMargin);
      surface->set_size(0, kBarHeight);
    } else if (!island) {
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
      island_w_applied = w;
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

  // Draws "LABEL value" with a dim label and a bright value (hierarchy inside
  // the status group instead of one flat grey run), returns the full width.
  void draw_stat(cairo_t* cr, Metrics& m, const std::string& s, double x, double base, double font = kFont) {
    (void)m;
    const size_t sp = s.find(' ');
    if (sp == std::string::npos) {
      draw_text(cr, s, x, base, font, pal.fg_primary);
      return;
    }
    const std::string label = s.substr(0, sp + 1), value = s.substr(sp + 1);
    const double lw = measure_text(cr, label, font).width;
    Color dim = soft_fg();
    draw_text(cr, label, x, base, font, dim);
    const bool na = value == "N/A" || value == "--%";
    draw_text(cr, value, x + lw, base, font, na ? dim : pal.fg_primary);
  }

  void draw(cairo_t* cr, int W, int H) {
    clock_rect = Rect{};  // set again by the layout that draws a clock
    if (taskbar) {
      draw_taskbar(cr, W, H);
      return;
    }
    Metrics m{cr};
    const bool capsules = layout_style == BarLayout::Capsules;
    const double radius = (island || pal.rounded) ? H / 2.0 : (capsules ? 6.0 : 0.0);
    Color bg = pal.bg_primary;
    bg.a = 0.94;  // the wallpaper shows through very slightly

    const TextExtents fe = measure_text(cr, "Ag", kFont);
    const double base = (H - fe.height) / 2.0 + fe.ascent;

    const WorkspaceColors& wc = config.workspace_colors;
    const Color in_bg = parse_color(wc.inactive_bg, {0, 0, 0, 0}),
                in_fg = parse_color(wc.inactive_fg, soft_fg()),
                ac_bg = parse_color(wc.active_bg, pal.accent),
                ac_fg = parse_color(wc.active_fg, pal.bg_primary);
    const double btn_r = wc.buttons_rounded ? kWsH / 2.0 : 0;

    // ---- group geometry ----
    const double pad = capsules ? 10 : 0;
    const double ws_w = ws_total(m), clock_w = m.text_w(clock_text, true), right_w = right_total(m);
    double ws_x, clock_x, right_x;  // content x of each group
    if (capsules) {
      const double left_pill = ws_w + 2 * pad, right_pill = right_w + 2 * pad, clock_pill = clock_w + 2 * pad;
      double cx = (W - clock_pill) / 2.0;
      cx = std::max(cx, left_pill + 8);
      cx = std::min(cx, W - right_pill - 8 - clock_pill);
      ws_x = pad;
      clock_x = cx + pad;
      right_x = W - right_pill + pad;
      auto pill = [&](double x, double w) {
        bar_surface(cr, x, 0, w, H, radius, kCapsuleSideMargin + x, kCapsuleTopMargin, 0.94);
      };
      pill(0, left_pill);
      pill(cx, clock_pill);
      pill(W - right_pill, right_pill);
    } else {
      bar_surface(cr, 0, 0, W, H, radius, island ? island_left() : 0, island ? kIslandTopMargin : 0, 0.94);
      const double nat = natural_width(m);
      const double spacer = std::max(0.0, (W - nat) / 2.0);
      ws_x = kMargin;
      clock_x = kMargin + ws_w + kBoxGap + spacer + kBoxGap;
      right_x = W - kMargin - right_w;
    }

    // ---- workspaces ----
    double x = ws_x;
    for (Rect& r : ws_rect) r = Rect{};
    for (int i : ws_visible()) {
      const double bw = ws_button_w(m, i), bh = kWsH, by = (H - bh) / 2.0;
      ws_rect[i] = {x, by, bw, bh};
      const bool active = i == active_workspace;
      if (active || in_bg.a > 0) {
        rounded_rect(cr, x, by, bw, bh, btn_r);
        set_source(cr, active ? ac_bg : in_bg);
        cairo_fill(cr);
      }
      char label[4];
      std::snprintf(label, sizeof label, "%d", (i + 1) % 10);
      const double tw = m.text_w(label, active);
      draw_text(cr, label, x + (bw - tw) / 2.0, base, kFont, active ? ac_fg : in_fg, active);
      x += bw + 1;
    }

    // ---- clock ----
    clock_rect = {clock_x - kClockPad, 0, clock_w + 2 * kClockPad, static_cast<double>(H)};
    clock_w_drawn = clock_w;
    draw_text(cr, clock_text, clock_x, base, kFont, pal.accent, true);

    draw_status_group(cr, m, right_x, H, base, btn_r);
  }

  // The widget cluster shared by every bar style: CPU/RAM/GPU/Disk/Volume,
  // tray icons, power-mode glyph, battery/plug and the power button, laid out
  // left to right from `rx`. Fills the hit rects used by clicks and tooltips.
  // Returns the x where the cluster ends.
  double draw_status_group(cairo_t* cr, Metrics& m, double rx, int H, double base, double btn_r) {
    auto stat = [&](const std::string& s, Rect* rect) {
      const double tw = m.text_w(s);
      if (rect) *rect = {rx, 0, tw + kStatPad, static_cast<double>(H)};
      draw_stat(cr, m, s, rx + kStatPad / 2, base);
      rx += tw + kStatPad + kRightGap;
    };
    if (compact_stats()) {
      double col_a, col_b;
      grid_columns(m, &col_a, &col_b);
      const double cell_h = H / 2.0;
      const TextExtents se = measure_text(cr, "Ag", kSmallFont);
      const double r0 = (cell_h - se.height) / 2.0 + se.ascent, r1 = cell_h + r0;
      const double x0 = rx + kStatPad / 2, x1 = x0 + col_a + kGridGap;
      cpu_rect = {rx, 0, col_a + kGridGap, cell_h};
      gpu_rect = {rx, cell_h, col_a + kGridGap, cell_h};
      ram_rect = {x1 - kGridGap / 2, 0, col_b + kStatPad, cell_h};
      disk_rect = {x1 - kGridGap / 2, cell_h, col_b + kStatPad, cell_h};
      draw_stat(cr, m, cpu_text, x0, r0, kSmallFont);
      draw_stat(cr, m, gpu_text, x0, r1, kSmallFont);
      draw_stat(cr, m, ram_text, x1, r0, kSmallFont);
      draw_stat(cr, m, disk_text, x1, r1, kSmallFont);
      rx += col_a + col_b + kGridGap + kStatPad + kRightGap;
      stat(vol_text, &vol_rect);
    } else {
      stat(cpu_text, &cpu_rect);
      stat(ram_text, &ram_rect);
      stat(gpu_text, &gpu_rect);
      stat(disk_text, &disk_rect);
      stat(vol_text, &vol_rect);
    }

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

    if (!kb_layouts.empty()) {
      const double lw = layout_pill_w(m);
      layout_rect = {rx, 0, lw, static_cast<double>(H)};
      draw_layout_pill(cr, m, rx, H / 2.0, lw);
      rx += lw + kRightGap;
    } else {
      layout_rect = {};
    }
    if (net_have) {
      net_rect = {rx, 0, kNetW, static_cast<double>(H)};
      draw_net_glyph(cr, rx + kNetW / 2, H / 2.0, 16);
      rx += kNetW + kRightGap;
    } else {
      net_rect = {};
    }
    draw_mode_glyph(cr, rx + kModeW / 2, H / 2.0, icon_fg());
    rx += kModeW + kRightGap;
    const double bw_total = battery_w(m);
    battery_rect = {rx, 0, bw_total, static_cast<double>(H)};
    if (battery.available) {
      if (battery.charging) draw_bolt(cr, rx + kStatPad / 2 + kBoltW / 2.0, H / 2.0);
      draw_battery(cr, rx + kStatPad / 2 + kBoltW, (H - 14) / 2.0, icon_fg());
      // The percentage as readable text beside the icon.
      draw_text(cr, battery_percent_text(), rx + kBatteryW + 2, base, kFont, pal.fg_primary);
    } else {
      draw_plug(cr, rx + kBatteryW / 2, H / 2.0, icon_fg());
    }
    rx += bw_total + kRightGap;

    // Power button: a bare glyph that lights up on hover (no filled box).
    const double pbh = kWsH, pby = (H - pbh) / 2.0;
    power_rect = {rx, pby, kPowerW, pbh};
    if (hover_power) {
      rounded_rect(cr, rx, pby, kPowerW, pbh, btn_r);
      Color hot = pal.accent;
      hot.a = 0.18;
      set_source(cr, hot);
      cairo_fill(cr);
    }
    draw_power_glyph(cr, rx + kPowerW / 2, H / 2.0, hover_power ? pal.accent : icon_fg());
      return rx + kPowerW;
  }

  // The keyboard layout in use as a pill ("US", "CZ"); width follows the text.
  std::string layout_text() const {
    if (kb_layouts.empty()) return "";
    return layout_pill_text(kb_layouts[static_cast<size_t>(std::clamp(kb_current, 0, static_cast<int>(kb_layouts.size()) - 1))]);
  }
  double layout_pill_w(Metrics& m) { return std::max(30.0, m.text_w(layout_text()) + 16); }
  void draw_layout_pill(cairo_t* cr, Metrics& m, double x, double cy, double w) {
    const double h = 20;
    rounded_rect(cr, x, cy - h / 2, w, h, pal.rounded ? h / 2 : 3);
    set_source(cr, with_alpha(pal.accent, 0.22));
    cairo_fill(cr);
    const std::string t = layout_text();
    const TextExtents te = measure_text(cr, t, 11.5, true);
    draw_text(cr, t, x + (w - te.width) / 2, cy - te.height / 2 + te.ascent, 11.5, pal.fg_primary, true);
  }
  std::string layout_tooltip_text() {
    const std::vector<LayoutInfo> info = load_xkb_layouts();
    std::string t;
    for (size_t i = 0; i < kb_layouts.size(); ++i) {
      if (i) t += "\n";
      t += (static_cast<int>(i) == kb_current ? "> " : "   ") + describe_layout(info, kb_layouts[i].layout, kb_layouts[i].variant);
    }
    return t + "\n\nClick: next layout   Right-click: keyboard settings";
  }
  bool parse_layouts_line(const std::string& line) {  // LAYOUTS <current> us: cz:qwerty
    std::istringstream in(line.substr(8));
    int idx = 0;
    if (!(in >> idx)) return false;
    std::vector<KeyboardLayout> parsed;
    std::string tok;
    while (in >> tok) {
      const size_t colon = tok.find(':');
      parsed.push_back({tok.substr(0, colon), colon == std::string::npos ? "" : tok.substr(colon + 1)});
    }
    const bool changed = idx != kb_current || !(parsed == kb_layouts);
    kb_current = idx;
    kb_layouts = parsed;
    return changed;
  }

  // The network icon: a Wi-Fi fan lit to the signal strength, or an Ethernet port, by what is in use.
  void draw_net_glyph(cairo_t* cr, double cx, double cy, double size) {
    const bool up = net_dev.state == net::State::Connected;
    const bool off = net_dev.state == net::State::Unavailable;
    const Color ic = icon_fg();
    const net::GlyphColor fg{ic.r, ic.g, ic.b, 1.0};
    if (net_dev.kind == net::Kind::Wifi)
      net::draw_wifi_glyph(cr, cx, cy, size, up ? net::wifi_arcs_lit(net_dev.signal > 0 ? net_dev.signal : 100) : 0, fg, off);
    else
      net::draw_ethernet_glyph(cr, cx, cy, size, up, fg, off);
  }

  bool update_network() {
    const std::vector<net::Device> devices = net::read_system_devices();
    const net::Device* p = net::primary_device(devices);
    const bool have = p != nullptr;
    const bool changed = have != net_have || (have && (p->name != net_dev.name || p->state != net_dev.state ||
                                                       p->signal != net_dev.signal || p->kind != net_dev.kind));
    net_have = have;
    if (have) net_dev = *p;
    if (changed) apply_layout_if_island();
    return changed;
  }

  // Asked fresh on every hover so it reflects NetworkManager, wpa_supplicant or the bare kernel view.
  std::string net_tooltip_text() {
    net::Snapshot snap = net::make_backend()->snapshot();
    const net::Device* primary = net::primary_device(snap.devices);
    if (!primary) return "No network card found";
    std::string t;
    for (const net::Device& d : snap.devices) {
      if (&d != primary && d.state != net::State::Connected) continue;
      if (!t.empty()) t += "\n\n";
      t += std::string(d.kind == net::Kind::Wifi ? "Wi-Fi: " : "Ethernet: ") + net::describe_device(d) + "\n" + d.name;
      for (const std::string& a : d.addresses) t += "  " + a;
      if (!d.gateway.empty()) t += "\nGateway " + d.gateway;
    }
    return t + "\n\nClick to open network settings";
  }

  // ------------------------------------------------------------- taskbar --
  static std::string lower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v;
  }

  static Color with_alpha(Color c, double a) {
    c.a = a;
    return c;
  }

  // Icon for a window, found through the desktop entries (id / Exec / Name) and
  // falling back to the app id as an icon name. Cached; nullptr = none found.
  cairo_surface_t* window_icon(const std::string& app_id) {
    const auto cached = win_icons.find(app_id);
    if (cached != win_icons.end()) return cached->second;
    if (!app_icons_loaded) {
      app_icons_loaded = true;
      for (const DesktopEntry& de : load_desktop_entries()) {
        if (de.icon.empty()) continue;
        std::string id = lower(de.id);
        if (id.size() > 8 && id.compare(id.size() - 8, 8, ".desktop") == 0) id.resize(id.size() - 8);
        app_icon_names.emplace(id, de.icon);
        app_icon_names.emplace(lower(exec_basename(de)), de.icon);
        app_icon_names.emplace(lower(de.name), de.icon);
      }
    }
    cairo_surface_t* icon = nullptr;
    const auto named = app_icon_names.find(lower(app_id));
    if (named != app_icon_names.end()) icon = load_icon(named->second, 48);
    if (!icon && !app_id.empty()) icon = load_icon(app_id, 48);
    win_icons[app_id] = icon;
    return icon;
  }

  void draw_window_icon(cairo_t* cr, const WindowEntry& w, double x, double y, double size) {
    const double alpha = w.minimized ? 0.5 : 1.0;
    if (cairo_surface_t* icon = window_icon(w.app_id)) {
      cairo_save(cr);
      cairo_translate(cr, x, y);
      cairo_scale(cr, size / cairo_image_surface_get_width(icon), size / cairo_image_surface_get_height(icon));
      cairo_set_source_surface(cr, icon, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
      cairo_paint_with_alpha(cr, alpha);
      cairo_restore(cr);
      return;
    }
    // No icon: the first letter on a soft disc.
    cairo_arc(cr, x + size / 2, y + size / 2, size / 2, 0, 2 * M_PI);
    set_source(cr, with_alpha(pal.accent, 0.22 * alpha));
    cairo_fill(cr);
    std::string src = !w.app_id.empty() ? w.app_id : w.title;
    const size_t dot = src.rfind('.');  // org.example.Files -> Files
    if (dot != std::string::npos && dot + 1 < src.size()) src = src.substr(dot + 1);
    std::string letter = src.empty() ? "?" : src.substr(0, 1);
    if (!letter.empty()) letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
    const TextExtents le = measure_text(cr, letter, size * 0.55, true);
    draw_text(cr, letter, x + (size - le.width) / 2, y + (size - le.height) / 2 + le.ascent, size * 0.55,
              with_alpha(pal.accent, alpha), true);
  }

  static std::string fit_text(cairo_t* cr, const std::string& text, double px, double max_w) {
    if (max_w <= 0) return "";
    std::string t = text;
    if (measure_text(cr, t, px).width <= max_w) return t;
    while (!t.empty() && measure_text(cr, t + "...", px).width > max_w) {
      size_t end = t.size() - 1;
      while (end > 0 && (static_cast<unsigned char>(t[end]) & 0xC0) == 0x80) --end;
      t.erase(end);
    }
    return t + "...";
  }

  void draw_start_button(cairo_t* cr, const Rect& r) {
    if (hover_start) {
      rounded_rect(cr, r.x, r.y, r.w, r.h, pal.rounded ? 8 : 2);
      set_source(cr, with_alpha(pal.accent, 0.18));
      cairo_fill(cr);
    }
    // Four rounded squares: a small "apps" glyph in the accent colour.
    const double sq = 7, gap = 3, ox = r.x + (r.w - (2 * sq + gap)) / 2, oy = r.y + (r.h - (2 * sq + gap)) / 2;
    for (int i = 0; i < 4; ++i) {
      rounded_rect(cr, ox + (i % 2) * (sq + gap), oy + (i / 2) * (sq + gap), sq, sq, 2);
      set_source(cr, pal.accent);
      cairo_fill(cr);
    }
  }

  void draw_window_button(cairo_t* cr, const Rect& r, const WindowEntry& w, bool hover, bool with_title) {
    const bool active = w.focused && !w.minimized;
    const double radius = config.taskbar_rounded && pal.rounded ? 8 : 0;
    if (active || hover) {
      rounded_rect(cr, r.x, r.y, r.w, r.h, radius);
      set_source(cr, with_alpha(pal.accent, active ? 0.22 : 0.12));
      cairo_fill(cr);
      if (glass) {  // a sheen over the button and a light rim
        cairo_save(cr);
        rounded_rect(cr, r.x, r.y, r.w, r.h, radius);
        cairo_clip(cr);
        cairo_pattern_t* sheen = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
        cairo_pattern_add_color_stop_rgba(sheen, 0, 1, 1, 1, active ? 0.30 : 0.18);
        cairo_pattern_add_color_stop_rgba(sheen, 0.5, 1, 1, 1, 0.04);
        cairo_pattern_add_color_stop_rgba(sheen, 1, 1, 1, 1, 0.0);
        cairo_set_source(cr, sheen);
        cairo_paint(cr);
        cairo_pattern_destroy(sheen);
        cairo_restore(cr);
        rounded_rect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, radius);
        set_source(cr, with_alpha({1, 1, 1, 1}, active ? 0.40 : 0.25));
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
      }
    }
    const double isz = vertical() ? 28 : 22;
    const double ix = with_title ? r.x + 9 : r.x + (r.w - isz) / 2, iy = r.y + (r.h - isz) / 2;
    draw_window_icon(cr, w, ix, iy, isz);
    if (with_title) {
      const double tx = ix + isz + 8;
      const std::string label = fit_text(cr, w.title.empty() ? w.app_id : w.title, kFont, r.x + r.w - 8 - tx);
      const TextExtents te = measure_text(cr, label, kFont);
      Color c = active ? pal.fg_primary : soft_fg();
      if (w.minimized) c.a = 0.6;
      draw_text(cr, label, tx, r.y + (r.h - te.height) / 2 + te.ascent, kFont, c, active);
    }
    // Indicator on the edge facing the desktop-side of the button.
    set_source(cr, active ? pal.accent : with_alpha(icon_fg(), w.minimized ? 0.4 : 0.7));
    if (!vertical() && active) {
      // The active button's line runs the full width of the bottom edge and follows the corner
      // curve, instead of stopping short of it or poking out past it.
      cairo_save(cr);
      rounded_rect(cr, r.x, r.y, r.w, r.h, radius);
      cairo_clip(cr);
      cairo_rectangle(cr, r.x, r.y + r.h - 3, r.w, 3);
      cairo_fill(cr);
      cairo_restore(cr);
      return;
    } else if (!vertical()) {
      const double iw = std::min(r.w - 18, 14.0);
      rounded_rect(cr, r.x + (r.w - iw) / 2, r.y + r.h - 3, iw, 2, 1);
    } else {
      const double ih = active ? r.h - 16 : std::min(r.h - 16, 14.0);
      const double bx = tb_pos == TaskbarPosition::Left ? r.x - 5 : r.x + r.w + 3;
      rounded_rect(cr, bx, r.y + (r.h - ih) / 2, 2, ih, 1);
    }
    cairo_fill(cr);
  }

  void draw_taskbar(cairo_t* cr, int W, int H) {
    Metrics m{cr};
    rebuild_shown();
    {
      const int mw = monitor_width(), mh = monitor_height();
      double sx = 0, sy = 0;  // where the taskbar sits on the screen
      if (tb_pos == TaskbarPosition::Bottom) sy = mh - H;
      else if (tb_pos == TaskbarPosition::Right) sx = mw - W;
      bar_surface(cr, 0, 0, W, H, 0, sx, sy, 0.97);
    }
    // Hairline on the edge that faces the desktop.
    set_source(cr, with_alpha(pal.fg_secondary, 0.22));
    switch (tb_pos) {
      case TaskbarPosition::Bottom: cairo_rectangle(cr, 0, 0, W, 1); break;
      case TaskbarPosition::Top: cairo_rectangle(cr, 0, H - 1, W, 1); break;
      case TaskbarPosition::Left: cairo_rectangle(cr, W - 1, 0, 1, H); break;
      case TaskbarPosition::Right: cairo_rectangle(cr, 0, 0, 1, H); break;
    }
    cairo_fill(cr);
    if (vertical()) draw_taskbar_vertical(cr, m, W, H);
    else draw_taskbar_horizontal(cr, m, W, H);
  }

  void draw_taskbar_horizontal(cairo_t* cr, Metrics& m, int W, int H) {
    const TextExtents fe = measure_text(cr, "Ag", kFont);
    const double base = (H - fe.height) / 2.0 + fe.ascent;
    const double btn_r = pal.rounded ? kWsH / 2.0 : 0;

    // Clock: time on top, date below (just the time, centered, when no date is shown).
    const size_t split = clock_text.find("  ");
    const std::string time_line = split == std::string::npos ? clock_text : clock_text.substr(0, split);
    const std::string date_line = split == std::string::npos ? "" : clock_text.substr(split + 2);
    const double time_w = measure_text(cr, time_line, kFont, true).width;
    const double date_w = date_line.empty() ? 0 : measure_text(cr, date_line, kSmallFont).width;
    const double clock_w = std::max(time_w, date_w);
    const double right_w = right_total(m);
    const double right_x = W - kMargin - right_w, clock_x = right_x - 18 - clock_w;
    clock_rect = {clock_x - kClockPad, 0, clock_w + 2 * kClockPad, static_cast<double>(H)};
    clock_w_drawn = clock_w;
    if (date_line.empty()) {
      draw_text(cr, time_line, clock_x + (clock_w - time_w) / 2, base, kFont, pal.accent, true);
    } else {
      const TextExtents te = measure_text(cr, time_line, kFont, true), de = measure_text(cr, date_line, kSmallFont);
      const double block = te.height + 1 + de.height, top = (H - block) / 2.0;
      draw_text(cr, time_line, clock_x + (clock_w - time_w) / 2, top + te.ascent, kFont, pal.accent, true);
      draw_text(cr, date_line, clock_x + (clock_w - date_w) / 2, top + te.height + 1 + de.ascent, kSmallFont,
                with_alpha(icon_fg(), 0.95));
    }
    draw_status_group(cr, m, right_x, H, base, btn_r);

    start_rect = {6, 4, 46, static_cast<double>(H - 8)};
    draw_start_button(cr, start_rect);

    // Workspace buttons right after the start button, then the window list.
    const double pager_end = draw_pager(cr, start_rect.x + start_rect.w + 10, (H - 26) / 2.0, 26, 26, false);
    const double x0 = pager_end + 12, avail = clock_x - 16 - x0;
    win_rects.assign(shown.size(), Rect{});
    if (shown.empty() || avail < 44) return;
    const geom::TaskbarSlots slots = geom::taskbar_slots(avail, shown.size());
    const double bw = slots.bw;
    for (size_t i = 0; i < slots.fit; ++i) {
      win_rects[i] = {x0 + i * (bw + 4), 4, bw, static_cast<double>(H - 8)};
      draw_window_button(cr, win_rects[i], shown[i], static_cast<int>(i) == hover_win, bw >= 96);
    }
  }

  // Compact two-line stat for the vertical taskbar: dim label over bright value.
  void draw_stat_vertical(cairo_t* cr, Metrics& m, const std::string& s, const Rect& r) {
    const size_t sp = s.find(' ');
    const std::string label = sp == std::string::npos ? s : s.substr(0, sp);
    const std::string value = sp == std::string::npos ? "" : s.substr(sp + 1);
    const bool na = value == "N/A" || value == "--%";
    const TextExtents le = measure_text(cr, label, 10.5), ve = measure_text(cr, value, kFont);
    draw_text(cr, label, r.x + (r.w - le.width) / 2, r.y + 4 + le.ascent, 10.5,
              with_alpha(soft_fg(), 1.0));
    draw_text(cr, value, r.x + (r.w - m.text_w(value)) / 2, r.y + 6 + le.height + ve.ascent - 2, kFont,
              na ? with_alpha(soft_fg(), 1.0) : pal.fg_primary);
  }

  void draw_taskbar_vertical(cairo_t* cr, Metrics& m, int W, int H) {
    const double bx = 6, bw = W - 12;
    constexpr double kBtn = 44, kStat = 38, kClock = 46, kBat = 30, kTrayRow = 28, kNetRow = 28, kLayoutRow = 28;
    const size_t tray_n = tray->items().size();
    const double cluster_h = kBtn + kClock + kBat + kStat * 3 + tray_n * kTrayRow + (net_have ? kNetRow : 0) +
                             (kb_layouts.empty() ? 0 : kLayoutRow);  // metrics: 2x2 grid + volume
    double y = H - 6 - cluster_h;

    // Status cluster, top-down from `y`.
    cpu_rect = {bx, y, bw / 2, kStat};
    ram_rect = {bx + bw / 2, y, bw / 2, kStat};
    gpu_rect = {bx, y + kStat, bw / 2, kStat};
    disk_rect = {bx + bw / 2, y + kStat, bw / 2, kStat};
    vol_rect = {bx, y + 2 * kStat, bw, kStat};
    draw_stat_vertical(cr, m, cpu_text, cpu_rect);
    draw_stat_vertical(cr, m, ram_text, ram_rect);
    draw_stat_vertical(cr, m, gpu_text, gpu_rect);
    draw_stat_vertical(cr, m, disk_text, disk_rect);
    draw_stat_vertical(cr, m, vol_text, vol_rect);
    y += 3 * kStat;
    const auto& items = tray->items();
    tray_rects.assign(items.size(), Rect{});
    for (size_t i = 0; i < items.size(); ++i) {
      const double ix = (W - kTrayIcon) / 2.0, iy = y + (kTrayRow - kTrayIcon) / 2;
      tray_rects[i] = {ix, iy, kTrayIcon, kTrayIcon};
      if (items[i].icon) {
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        cairo_scale(cr, kTrayIcon / cairo_image_surface_get_width(items[i].icon),
                    kTrayIcon / cairo_image_surface_get_height(items[i].icon));
        cairo_set_source_surface(cr, items[i].icon, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
      }
      y += kTrayRow;
    }
    if (!kb_layouts.empty()) {
      layout_rect = {bx, y, bw, kLayoutRow};
      Metrics pm{cr};
      const double lw = std::min(bw, layout_pill_w(pm));
      draw_layout_pill(cr, pm, (W - lw) / 2.0, y + kLayoutRow / 2, lw);
      y += kLayoutRow;
    } else {
      layout_rect = {};
    }
    if (net_have) {
      net_rect = {bx, y, bw, kNetRow};
      draw_net_glyph(cr, W / 2.0, y + kNetRow / 2, 16);
      y += kNetRow;
    } else {
      net_rect = {};
    }
    // Power-mode glyph and battery/plug on one row.
    draw_mode_glyph(cr, W / 2.0 - 17, y + kBat / 2, icon_fg());
    battery_rect = {W / 2.0 - 4, y, W / 2.0 - 2, kBat};
    const double bcx = W / 2.0 + 12;
    if (battery.available) {
      if (battery.charging) draw_bolt(cr, bcx - 15, y + kBat / 2);
      draw_battery(cr, bcx - 13, y + (kBat - 14) / 2.0, icon_fg());
    } else {
      draw_plug(cr, bcx, y + kBat / 2, icon_fg());
    }
    y += kBat;
    // Clock: time over date.
    const size_t gap = clock_text.find("  ");
    const std::string time_line = gap == std::string::npos ? clock_text : clock_text.substr(0, gap);
    const std::string date_line = gap == std::string::npos ? "" : clock_text.substr(gap + 2);
    const TextExtents te = measure_text(cr, time_line, kFont, true);
    draw_text(cr, time_line, (W - te.width) / 2, y + 8 + te.ascent, kFont, pal.accent, true);
    if (!date_line.empty()) {
      const double dw = measure_text(cr, date_line, 10.5).width;
      draw_text(cr, date_line, (W - dw) / 2, y + 10 + te.height + 8, 10.5, with_alpha(icon_fg(), 0.95));
    }
    y += kClock;
    // Power button.
    power_rect = {bx, y + 2, bw, kBtn - 4};
    if (hover_power) {
      rounded_rect(cr, power_rect.x, power_rect.y, power_rect.w, power_rect.h, pal.rounded ? 8 : 2);
      set_source(cr, with_alpha(pal.accent, 0.18));
      cairo_fill(cr);
    }
    draw_power_glyph(cr, W / 2.0, y + kBtn / 2, hover_power ? pal.accent : icon_fg());

    // Start button and window buttons.
    start_rect = {bx, 6, bw, kBtn};
    draw_start_button(cr, start_rect);
    // Workspace buttons in two columns under the start button, then the window list.
    const double pager_end = draw_pager(cr, bx + (bw - 2 * 30 - 3) / 2, start_rect.y + start_rect.h + 10, 30, 26, true);
    const double top = pager_end + 12, limit = H - 6 - cluster_h - 8;
    win_rects.assign(shown.size(), Rect{});
    for (size_t i = 0; i < shown.size(); ++i) {
      const double wy = top + i * (kBtn + 4);
      if (wy + kBtn > limit) break;
      win_rects[i] = {bx + 4, wy, bw - 8, kBtn};
      draw_window_button(cr, win_rects[i], shown[i], static_cast<int>(i) == hover_win, false);
    }
  }

  // The launcher toggles itself (a second launch closes the first) and reads the
  // taskbar position from bar.toml, so there is nothing to pass.
  void spawn_start_menu() {
    const char* argv[] = {"fleetwm-launcher", "--start-menu", nullptr};
    pid_t pid;
    if (posix_spawnp(&pid, "fleetwm-launcher", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0)
      std::fprintf(stderr, "fleetwm-bar: failed to launch the start menu\n");
  }

  std::string window_tooltip(uint32_t id) const {
    for (const WindowEntry& w : windows)
      if (w.id == id) return w.title.empty() ? w.app_id : w.title;
    return "";
  }

  // -------------------------------------------------------------- sources --
  std::string format_clock() {
    const ClockFormat& fmt = config.clock;
    // The bar's own zone override (if any) wins over the system zone; tzset()
    // each time also picks up a system zone change made while we run.
    if (!fmt.timezone.empty()) setenv("TZ", fmt.timezone.c_str(), 1);
    else unsetenv("TZ");
    tzset();
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char tb[24];
    const char* tfmt = fmt.use_24h ? (fmt.show_seconds ? "%H:%M:%S" : "%H:%M")
                                   : (fmt.show_seconds ? "%-I:%M:%S %p" : "%-I:%M %p");
    std::strftime(tb, sizeof tb, tfmt, &lt);
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

  static std::string read_word(const std::string& path) {
    std::string w;
    std::ifstream(path) >> w;
    return w;
  }
  static long long read_number(const std::string& path) {
    long long v = -1;
    std::ifstream(path) >> v;
    return v;
  }

  void init_gpu() {
    for (int card = 0; card < 8; ++card) {
      const std::string base = "/sys/class/drm/card" + std::to_string(card);
      const std::string vendor_id = read_word(base + "/device/vendor");
      if (vendor_id.empty()) continue;
      std::error_code ec;
      const std::string driver = std::filesystem::read_symlink(base + "/device/driver", ec).filename().string();
      Gpu g;
      g.driver = driver;
      if (vendor_id == "0x1002") g.vendor = "AMD";
      else if (vendor_id == "0x8086") g.vendor = "Intel";
      else if (vendor_id == "0x10de") g.vendor = "NVIDIA";
      else g.vendor = vendor_id;
      if (std::ifstream(base + "/device/gpu_busy_percent").good()) {
        g.kind = Gpu::Kind::AmdBusy;
        g.path = base + "/device/gpu_busy_percent";
      } else if (vendor_id == "0x8086") {
        for (const char* rel : {"/gt/gt0/rc6_residency_ms", "/power/rc6_residency_ms", "/device/tile0/gt0/gtidle/idle_residency_ms"})
          if (std::ifstream(base + rel).good()) {
            g.kind = Gpu::Kind::IntelIdle;
            g.path = base + rel;
            break;
          }
        for (const char* rel : {"/gt/gt0/rps_act_freq_mhz", "/gt_act_freq_mhz", "/device/tile0/gt0/freq0/act_freq"})
          if (std::ifstream(base + rel).good()) {
            g.freq_path = base + rel;
            break;
          }
        for (const char* rel : {"/gt/gt0/rps_RP0_freq_mhz", "/gt_RP0_freq_mhz", "/device/tile0/gt0/freq0/max_freq"})
          if (std::ifstream(base + rel).good()) {
            g.freq_max_path = base + rel;
            break;
          }
      }
      if (!g.path.empty()) gpus.push_back(std::move(g));
    }
    if (const char* path = std::getenv("PATH")) {
      std::string p = path;
      size_t pos = 0;
      while (pos <= p.size()) {
        size_t e = p.find(':', pos);
        if (e == std::string::npos) e = p.size();
        const std::string cand = p.substr(pos, e - pos) + "/nvidia-smi";
        if (access(cand.c_str(), X_OK) == 0) {
          has_nvidia_smi = true;
          break;
        }
        pos = e + 1;
      }
    }
    if (gpus.empty() && !has_nvidia_smi) gpu_text = "GPU N/A";
  }

  // "GPU 12%" for one GPU; "GPU1 12%  GPU2 40%" for several.
  std::string build_gpu_text() const {
    struct Item {
      int percent;
    };
    std::vector<int> values;
    for (const Gpu& g : gpus) values.push_back(g.percent);
    for (int i = 0; i < nvidia_cards; ++i) values.push_back(nvidia_percent.size() > static_cast<size_t>(i) ? nvidia_percent[static_cast<size_t>(i)] : -1);
    if (values.empty()) return "GPU N/A";
    auto one = [](int v) { return v < 0 ? std::string("--%") : std::to_string(v) + "%"; };
    if (values.size() == 1) return "GPU " + one(values[0]);
    std::string t;
    for (size_t i = 0; i < values.size(); ++i) t += (i ? "  GPU" : "GPU") + std::to_string(i + 1) + " " + one(values[i]);
    return t;
  }
  std::vector<int> nvidia_percent;

  bool update_gpu() {
    bool changed = false;
    for (Gpu& g : gpus) {
      int pct = g.percent;
      if (g.kind == Gpu::Kind::AmdBusy) {
        if (g.fd < 0) g.fd = open(g.path.c_str(), O_RDONLY | O_CLOEXEC);
        if (g.fd < 0) continue;
        char buf[32];
        const ssize_t got = pread(g.fd, buf, sizeof buf - 1, 0);
        if (got <= 0) continue;
        buf[got] = 0;
        pct = std::clamp(std::atoi(buf), 0, 100);
      } else {
        const long long idle_ms = read_number(g.path);
        if (idle_ms < 0) continue;
        const auto now = std::chrono::steady_clock::now();
        if (g.idle_prev_ms >= 0) {
          const double wall_ms = std::chrono::duration<double, std::milli>(now - g.idle_prev_time).count();
          if (wall_ms < 50) continue;
          pct = static_cast<int>(100.0 * (1.0 - std::clamp((idle_ms - g.idle_prev_ms) / wall_ms, 0.0, 1.0)) + 0.5);
        }
        g.idle_prev_ms = idle_ms;
        g.idle_prev_time = now;
      }
      if (pct != g.percent) {
        g.percent = pct;
        changed = true;
      }
    }
    if (has_nvidia_smi && ++gpu_tick >= 3 && !gpu_query_running) {
      gpu_tick = 0;
      gpu_query_running = true;
      // nvidia-smi can take a while; keep it off the UI thread. It prints one line per card.
      std::thread([this] {
        std::vector<int> pcts;
        if (FILE* p = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r")) {
          int v = 0;
          while (std::fscanf(p, "%d", &v) == 1) pcts.push_back(std::clamp(v, 0, 100));
          pclose(p);
        }
        app.post([this, pcts] {
          gpu_query_running = false;
          nvidia_percent = pcts;
          nvidia_cards = static_cast<int>(pcts.size());
          if (set_if_changed(gpu_text, build_gpu_text())) redraw();
        });
      }).detach();
    }
    return (changed && set_if_changed(gpu_text, build_gpu_text()));
  }

  struct MemInfo {
    unsigned long long total = 0, available = 0, cached = 0, buffers = 0, swap_total = 0, swap_free = 0;
  };
  static bool read_meminfo(MemInfo* m) {
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return false;
    char line[128];
    while (std::fgets(line, sizeof line, f)) {
      unsigned long long v = 0;
      if (std::sscanf(line, "MemTotal: %llu", &v) == 1) m->total = v;
      else if (std::sscanf(line, "MemAvailable: %llu", &v) == 1) m->available = v;
      else if (std::sscanf(line, "Cached: %llu", &v) == 1) m->cached = v;
      else if (std::sscanf(line, "Buffers: %llu", &v) == 1) m->buffers = v;
      else if (std::sscanf(line, "SwapTotal: %llu", &v) == 1) m->swap_total = v;
      else if (std::sscanf(line, "SwapFree: %llu", &v) == 1) m->swap_free = v;
    }
    std::fclose(f);
    return m->total > 0;
  }

  bool update_ram() {
    MemInfo m;
    if (!read_meminfo(&m)) return set_if_changed(ram_text, "RAM N/A");
    const int pct = static_cast<int>(100.0 * static_cast<double>(m.total - m.available) /
                                         static_cast<double>(m.total) + 0.5);
    return set_if_changed(ram_text, "RAM " + std::to_string(pct) + "%");
  }

  static std::string fmt_kib(unsigned long long kib) {
    char b[32];
    if (kib >= 1024ull * 1024) std::snprintf(b, sizeof b, "%.1f GiB", kib / 1048576.0);
    else std::snprintf(b, sizeof b, "%llu MiB", kib / 1024);
    return b;
  }

  std::string ram_tooltip_text() {
    MemInfo m;
    if (!read_meminfo(&m)) return "Memory info unavailable";
    const unsigned long long used = m.total - m.available;
    std::string t = "Memory: " + fmt_kib(used) + " used of " + fmt_kib(m.total) + "\n" +
                    "Available: " + fmt_kib(m.available) + "\n" +
                    "Cache + buffers: " + fmt_kib(m.cached + m.buffers);
    if (m.swap_total > 0)
      t += "\nSwap: " + fmt_kib(m.swap_total - m.swap_free) + " of " + fmt_kib(m.swap_total);
    else
      t += "\nSwap: none";
    return t;
  }

  std::string cpu_tooltip_text() {
    std::string t;
    double l1 = 0, l5 = 0, l15 = 0;
    if (std::FILE* f = std::fopen("/proc/loadavg", "r")) {
      if (std::fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) == 3) {
        char b[64];
        std::snprintf(b, sizeof b, "Load average: %.2f  %.2f  %.2f", l1, l5, l15);
        t = b;
      }
      std::fclose(f);
    }
    if (t.empty()) t = "Load average unavailable";
    t += "\nCores: " + std::to_string(std::max(1L, sysconf(_SC_NPROCESSORS_ONLN)));
    if (std::FILE* f = std::fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r")) {
      long khz = 0;
      if (std::fscanf(f, "%ld", &khz) == 1 && khz > 0) {
        char b[48];
        std::snprintf(b, sizeof b, "\nCore 0 clock: %.2f GHz", khz / 1.0e6);
        t += b;
      }
      std::fclose(f);
    }
    return t;
  }

  std::string gpu_tooltip_text() {
    std::string t;
    int n = 0;
    for (const Gpu& g : gpus) {
      if (n++) t += "\n\n";
      t += (gpus.size() + nvidia_cards > 1 ? "GPU" + std::to_string(n) + ": " : std::string()) + g.vendor + " (" + g.driver + ")  " +
           (g.percent < 0 ? std::string("--") : std::to_string(g.percent)) + "%";
      if (g.kind == Gpu::Kind::IntelIdle) {
        t += " active";
        const long long cur = g.freq_path.empty() ? -1 : read_number(g.freq_path);
        const long long max = g.freq_max_path.empty() ? -1 : read_number(g.freq_max_path);
        if (cur >= 0) t += "\nClock: " + std::to_string(cur) + (max > 0 ? " of " + std::to_string(max) : std::string()) + " MHz";
      } else {
        t += " busy";
      }
    }
    for (int i = 0; i < nvidia_cards; ++i) {
      if (n++) t += "\n\n";
      const int v = nvidia_percent.size() > static_cast<size_t>(i) ? nvidia_percent[static_cast<size_t>(i)] : -1;
      t += (gpus.size() + nvidia_cards > 1 ? "GPU" + std::to_string(n) + ": " : std::string()) + "NVIDIA  " +
           (v < 0 ? std::string("--") : std::to_string(v)) + "% busy";
    }
    if (t.empty()) return "No GPU utilisation source found\n(software rendering or unsupported driver)";
    return t;
  }

  std::string disk_tooltip_text() {
    struct statvfs v {};
    if (statvfs("/", &v) != 0) return "Disk usage unavailable";
    const unsigned long long fr = v.f_frsize ? v.f_frsize : v.f_bsize;
    const unsigned long long total = v.f_blocks * fr / 1024, free_b = v.f_bavail * fr / 1024;
    return "Root filesystem (/)\nUsed: " + fmt_kib(total - v.f_bfree * fr / 1024) + " of " + fmt_kib(total) +
           "\nFree: " + fmt_kib(free_b);
  }

  std::string vol_tooltip_text() { return vol_text + "\nClick to open the audio mixer"; }

  std::string tooltip_text(int kind) {
    switch (kind) {
      case 1: return battery_tooltip_text();
      case 2: return ram_tooltip_text();
      case 3: return cpu_tooltip_text();
      case 4: return gpu_tooltip_text();
      case 5: return disk_tooltip_text();
      case 7: return net_tooltip_text();
      case 8: return layout_tooltip_text();
      default: return vol_tooltip_text();
    }
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

  // The clock and the CPU/GPU stats run on separate timers so an idle desktop
  // is not woken (and the compositor not made to composite a frame) once a
  // second: with seconds hidden the clock redraws once a minute, aligned to
  // the minute boundary; stats refresh every 2 s and only redraw on change.
  int clock_timer = 0;

  void clock_update() {
    if (set_if_changed(clock_text, format_clock())) {
      const bool resized = island_resize_if_needed();
      if (!resized && surface && clock_rect.w > 0 && std::abs(clock_width_now() - clock_w_drawn) < 0.01)
        surface->queue_draw_rect(static_cast<int>(std::floor(clock_rect.x)), 0, static_cast<int>(std::ceil(clock_rect.w)) + 1,
                                 surface->height());
      else
        redraw();
    }
  }

  void schedule_clock() {
    if (clock_timer) {
      app.unwatch(clock_timer);
      clock_timer = 0;
    }
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    const long period = config.clock.show_seconds ? 1000 : 60000;
    const long now_ms = static_cast<long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    long wait = period - now_ms % period;
    if (wait < 30) wait += period;
    clock_timer = app.add_oneshot(static_cast<int>(wait), [this] {
      clock_timer = 0;
      clock_update();
      schedule_clock();
    });
  }

  void stats_tick() {
    bool ch = update_cpu();
    ch |= update_ram();
    ch |= update_gpu();
    if (ch) {
      island_resize_if_needed();  // text widths changed, island width follows
      redraw();
    }
  }

  void clock_tick() {  // full refresh, used at start-up and after config changes
    clock_update();
    stats_tick();
    schedule_clock();
  }

  // ------------------------------------------------------------------ ipc --
  void handle_ipc_line(const std::string& line) {
    if (line.rfind("LAYOUTS ", 0) == 0) {
      if (parse_layouts_line(line)) {
        if (island) apply_layout();
        redraw();
      }
      return;
    }
    if (line.rfind("WINDOWS", 0) == 0) {
      std::vector<WindowEntry> parsed;
      if (parse_window_list(line, &parsed) && parsed != windows) {
        windows = std::move(parsed);
        hover_win = -1;
        ws_list_changed();
        redraw();
      }
      return;
    }
    int ws = -1;
    try {
      if (line.rfind("WORKSPACE_CHANGED ", 0) == 0) ws = std::stoi(line.substr(18));
      else if (!line.empty() && line.find_first_not_of("0123456789") == std::string::npos) ws = std::stoi(line);
    } catch (...) {
      return;
    }
    if (ws >= 0 && ws != active_workspace) {
      active_workspace = ws;
      ws_list_changed();
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
      ipc.send_command("LAYOUTS?");
      ipc.send_command("SUBSCRIBE_WINDOWS");
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
    if (taskbar) {
      if (b == kBtnLeft && start_rect.hit(x, y)) return spawn_start_menu();
      for (size_t i = 0; i < win_rects.size() && i < shown.size(); ++i) {
        if (!win_rects[i].hit(x, y)) continue;
        if (b == kBtnLeft) ipc.send_command("WINDOW_TOGGLE " + std::to_string(shown[i].id));
        else if (b == kBtnMiddle) ipc.send_command("WINDOW_CLOSE " + std::to_string(shown[i].id));
        return;
      }
    }
    if (!kb_layouts.empty() && layout_rect.hit(x, y)) {
      if (b == kBtnLeft) ipc.send_command("LAYOUT_NEXT");
      else if (b == 0x111) spawn_settings_page("keyboard");
      return;
    }
    if (b == kBtnLeft) {
      for (int i = 0; i < 10; ++i)
        if (ws_rect[i].hit(x, y)) {
          ipc.send_command("WORKSPACE " + std::to_string(i));
          return;
        }
      if (vol_rect.hit(x, y)) return spawn("fleetwm-audiomixer");
      if (battery_rect.hit(x, y)) return spawn_settings_page("power");
      if (net_have && net_rect.hit(x, y)) return spawn_settings_page("network");
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
    const BatteryText t = describe_battery(battery, on_ac);
    return t.detail.empty() ? t.headline : t.headline + "\n" + t.detail;
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
    if (taskbar) {
      int hw = -1;
      for (size_t i = 0; i < win_rects.size() && i < shown.size(); ++i)
        if (win_rects[i].hit(x, y)) hw = static_cast<int>(i);
      int hws = -1;
      for (int i = 0; i < 10; ++i)
        if (ws_rect[i].w > 0 && ws_rect[i].hit(x, y)) hws = i;
      if (hws != hover_workspace) {
        hover_workspace = hws;
        redraw();
      }
      const bool hs = start_rect.hit(x, y);
      if (hw != hover_win || hs != hover_start) {
        hover_win = hw;
        hover_start = hs;
        redraw();
      }
    }
    const Rect* rects[] = {&battery_rect, &ram_rect, &cpu_rect, &gpu_rect, &disk_rect, &vol_rect, &net_rect, &layout_rect};
    int want = 0;  // 1..6 = rects above (index + 1); 1000 + id = a window button
    Rect r;
    for (int i = 0; i < 8; ++i)
      if (rects[i]->hit(x, y) && (i != 6 || net_have) && (i != 7 || !kb_layouts.empty())) {
        want = i + 1;
        r = *rects[i];
      }
    for (size_t i = 0; i < tray_rects.size(); ++i)
      if (tray_rects[i].hit(x, y)) {
        want = 2000 + static_cast<int>(i);
        r = tray_rects[i];
      }
    if (taskbar && hover_win >= 0 && hover_win < static_cast<int>(shown.size())) {
      want = 1000 + static_cast<int>(shown[static_cast<size_t>(hover_win)].id);
      r = win_rects[static_cast<size_t>(hover_win)];
    }
    if (want != tooltip_for) {
      hide_tooltip();
      tooltip_for = want;
    }
    if (want && !tooltip && !tooltip_timer) {
      tooltip_timer = app.add_oneshot(400, [this, r, want] {
        tooltip_timer = 0;
        show_tooltip(r, want);
      });
    }
  }

  void show_tooltip(const Rect& r, int kind) {
    if (kind >= 2000) {  // a tray item: ask it for its text at this moment
      const size_t index = static_cast<size_t>(kind - 2000);
      tray->tooltip(index, [this, r, kind](std::string text) {
        if (tooltip_for == kind && !tooltip && !text.empty()) place_tooltip(r, text);
      });
      return;
    }
    const std::string text = kind >= 1000 ? window_tooltip(static_cast<uint32_t>(kind - 1000)) : tooltip_text(kind);
    if (text.empty()) return;
    place_tooltip(r, text);
  }

  void place_tooltip(const Rect& r, const std::string& text) {
    const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int tx, ty;
    Tooltip::Placement pl = Tooltip::Placement::Below;
    if (taskbar) {
      const int mw = monitor_width(), mh = monitor_height();
      switch (tb_pos) {
        case TaskbarPosition::Bottom:
          tx = static_cast<int>(cx);
          ty = mh - kTaskbarThickness - 4;
          pl = Tooltip::Placement::Above;
          break;
        case TaskbarPosition::Top:
          tx = static_cast<int>(cx);
          ty = kTaskbarThickness + 4;
          break;
        case TaskbarPosition::Left:
          tx = kTaskbarWidth + 4;
          ty = static_cast<int>(cy);
          pl = Tooltip::Placement::Right;
          break;
        default:  // Right
          tx = mw - kTaskbarWidth - 4;
          ty = static_cast<int>(cy);
          pl = Tooltip::Placement::Left;
          break;
      }
    } else {
      const int left = island ? island_left() : layout_style == BarLayout::Capsules ? kCapsuleSideMargin : 0;
      tx = left + static_cast<int>(cx);
      ty = (island ? kIslandTopMargin : layout_style == BarLayout::Capsules ? kCapsuleTopMargin : 0) + kBarHeight + 4;
    }
    tooltip = std::make_unique<Tooltip>(app, pal, text, tx, ty, pl);
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
    refresh_glass();
    apply_layout();
    clock_tick();  // show a changed hour format / time zone right away
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
  B.refresh_glass();
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
    if (B.hover_power || B.hover_win >= 0 || B.hover_start || B.hover_workspace >= 0) {
      B.hover_workspace = -1;
      B.hover_power = 0;
      B.hover_win = -1;
      B.hover_start = false;
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
  B.update_ram();
  B.battery_dir = find_battery_dir();
  if (const char* d = std::getenv("FLEETWM_BATTERY_DIR")) B.battery_dir = d;  // test hook
  B.update_battery();
  B.update_network();
  B.clock_tick();
  B.apply_layout();
  B.try_connect();

  B.app.add_timer(2000, [&B] { B.stats_tick(); });
  B.app.add_timer(5000, [&B] {
    if (B.update_disk()) B.redraw();
  });
  B.app.add_timer(15000, [&B] {
    if (B.update_battery()) B.redraw();
  });
  B.app.add_timer(5000, [&B] {
    if (B.update_network()) B.redraw();
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
