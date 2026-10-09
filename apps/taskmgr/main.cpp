// fleetwm-taskmgr: the Task Manager. Windows 7 layout with the Windows 10 Performance tab, drawn in code like the other Fleetwm programs.
//   Processes:   name, PID, CPU, memory, disk, user; click a header to sort; End task / Force quit; Delete ends the selected one.
//   Performance: CPU (overall and per core), memory, disk, network and GPU graphs with the numbers the bar shows, plus scheduler and audio latency.
// It reads what the bar reads (hw_stats, GpuMonitor), so the two never disagree.
//
// Cost rules: it samples only while the window is shown (a window that gets no frame callbacks, minimised or covered, stops the 1 s tick's
// work), reads /proc through files kept open (pread, no reopening), scans per-process data only on the Processes tab, and redraws only
// when a sample changed something.

#include <pwd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "fleetkit.hpp"
#include "gpu_monitor.hpp"
#include "hw_stats.hpp"
#include "malloc_tuning.hpp"
#include "prewarm.hpp"
#include "proc_stats.hpp"
#include "quit_signals.hpp"
#include "theme.hpp"
#include "version.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kBtnLeft = 0x110;
constexpr int kWindowW = 880, kWindowH = 620;
constexpr double kFont = 13, kSmall = 11.5, kRowH = 22, kHeaderH = 26, kTabsH = 38, kFooterH = 46, kSideW = 214;

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// A /proc or sysfs file kept open and read again with pread.
struct Reader {
  int fd = -1;
  std::string buf;
  ~Reader() {
    if (fd >= 0) close(fd);
  }
  const std::string& read(const char* path) {
    if (fd < 0) fd = open(path, O_RDONLY | O_CLOEXEC);
    buf.clear();
    if (fd < 0) return buf;
    char chunk[8192];
    off_t off = 0;
    for (;;) {
      const ssize_t n = pread(fd, chunk, sizeof chunk, off);
      if (n <= 0) break;
      buf.append(chunk, static_cast<size_t>(n));
      off += n;
    }
    return buf;
  }
};

enum class Tab { Processes, Performance };
enum class Res { Cpu, Memory, Disk, Network, Gpu };

struct TaskMgr {
  App app;
  Palette pal;
  std::unique_ptr<Surface> surface;
  int width = kWindowW, height = kWindowH;

  Tab tab = Tab::Processes;
  Res res = Res::Cpu;

  // ---- processes ----
  ProcessTable table;
  std::vector<ProcRow> rows;
  ProcColumn sort_col = ProcColumn::Cpu;
  bool sort_desc = true;
  int selected_pid = 0;
  double scroll = 0;
  std::unordered_map<unsigned, std::string> users;
  std::string status;
  Rect col_rect[6], end_btn, force_btn, tab_rect[2], res_rect[5];

  // ---- machine ----
  Reader stat_r, mem_r, disk_r, net_r, load_r, up_r;
  CpuTimes prev_total;
  std::vector<CpuTimes> prev_cores;
  bool have_prev = false;
  History cpu_h{60}, mem_h{60}, disk_read_h{60}, disk_write_h{60}, net_rx_h{60}, net_tx_h{60}, gpu_h{60};
  std::vector<History> core_h;
  std::vector<int> core_pct;
  int cpu_pct = 0;
  MemDetail mem;
  DiskCounters prev_disk;
  NetCounters prev_net;
  double disk_read_bps = 0, disk_write_bps = 0, net_rx_bps = 0, net_tx_bps = 0;
  double sched_latency_us = -1, audio_latency_ms = -1;
  std::string load_avg;
  long uptime_s = 0;
  GpuMonitor gpu;
  int ncpu = 1;
  long hz = 100;
  Clock::time_point last_sample;
  int ticks = 0;
  int tick_timer = 0;

  // -------------------------------------------------------------- sampling --
  void sample_machine(double dt) {
    CpuTimes total;
    const std::string& st = stat_r.read("/proc/stat");
    if (parse_proc_stat_total(st, &total) && have_prev && total.total > prev_total.total)
      cpu_pct = static_cast<int>(100.0 * static_cast<double>(total.busy - prev_total.busy) / static_cast<double>(total.total - prev_total.total) + 0.5);
    prev_total = total;
    const std::vector<CpuTimes> cores = parse_proc_stat_cores(st);
    if (have_prev && cores.size() == prev_cores.size()) core_pct = core_percents(prev_cores, cores);
    prev_cores = cores;
    if (core_h.size() != cores.size()) core_h.assign(cores.size(), History(60));
    for (size_t i = 0; i < core_pct.size() && i < core_h.size(); ++i) core_h[i].push(std::max(0, core_pct[i]));
    cpu_h.push(cpu_pct);
    ncpu = std::max<int>(1, static_cast<int>(cores.size()));

    if (parse_meminfo(mem_r.read("/proc/meminfo"), &mem) && mem.total > 0)
      mem_h.push(100.0 * static_cast<double>(mem.total - mem.available) / static_cast<double>(mem.total));

    const DiskCounters d = parse_diskstats(disk_r.read("/proc/diskstats"));
    if (have_prev && dt > 0) {
      disk_read_bps = static_cast<double>(d.read_bytes >= prev_disk.read_bytes ? d.read_bytes - prev_disk.read_bytes : 0) / dt;
      disk_write_bps = static_cast<double>(d.write_bytes >= prev_disk.write_bytes ? d.write_bytes - prev_disk.write_bytes : 0) / dt;
    }
    prev_disk = d;
    disk_read_h.push(disk_read_bps);
    disk_write_h.push(disk_write_bps);

    const NetCounters n = parse_net_dev(net_r.read("/proc/net/dev"));
    if (have_prev && dt > 0) {
      net_rx_bps = static_cast<double>(n.rx_bytes >= prev_net.rx_bytes ? n.rx_bytes - prev_net.rx_bytes : 0) / dt;
      net_tx_bps = static_cast<double>(n.tx_bytes >= prev_net.tx_bytes ? n.tx_bytes - prev_net.tx_bytes : 0) / dt;
    }
    prev_net = n;
    net_rx_h.push(net_rx_bps);
    net_tx_h.push(net_tx_bps);

    gpu.sample([](std::function<void()> fn) { std::thread(std::move(fn)).detach(); }, [this](std::function<void()> fn) { app.post(std::move(fn)); });
    gpu_h.push(std::max(0, gpu.busiest_percent()));

    have_prev = true;
  }

  void sample_slow() {  // numbers that change slowly: latency, load, uptime
    if (tab != Tab::Performance) return;
    if (ticks % 5 == 1) sched_latency_us = timer_latency_us(3);  // three sleeps are three wake-ups: once in five ticks is enough for a number that barely moves
    std::string l = load_r.read("/proc/loadavg");
    double a = 0, b = 0, c = 0;
    if (std::sscanf(l.c_str(), "%lf %lf %lf", &a, &b, &c) == 3) {
      char t[64];
      std::snprintf(t, sizeof t, "%.2f  %.2f  %.2f", a, b, c);
      load_avg = t;
    }
    uptime_s = std::atol(up_r.read("/proc/uptime").c_str());
    if (ticks % 30 == 1) {  // the audio quantum changes rarely, and asking for it starts a program
      std::string out;
      if (FILE* p = popen("pw-metadata -n settings 2>/dev/null", "r")) {
        char buf[512];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        pclose(p);
      }
      audio_latency_ms = parse_audio_latency_ms(out);
    }
  }

  void tick() {
    // A window that is not shown gets no frame callbacks: skip the work until it is shown again.
    if (surface && surface->frame_stalled(1500)) return;
    const auto now = Clock::now();
    const double dt = have_prev ? std::chrono::duration<double>(now - last_sample).count() : 1.0;
    last_sample = now;
    ++ticks;
    sample_machine(dt);
    if (tab == Tab::Processes) {
      table.refresh(dt, hz, ncpu);
      rows = table.rows();
      sort_rows(&rows, sort_col, sort_desc, users);
      for (const ProcRow& r : rows)
        if (!users.count(r.info.uid)) {
          const passwd* pw = getpwuid(r.info.uid);
          users[r.info.uid] = pw ? pw->pw_name : std::to_string(r.info.uid);
        }
    }
    sample_slow();
    clamp_scroll();
    surface->queue_draw();
  }

  // -------------------------------------------------------------- drawing --
  Color dim() const { return {pal.fg_secondary.r, pal.fg_secondary.g, pal.fg_secondary.b, 1}; }
  static Color alpha(Color c, double a) {
    c.a = a;
    return c;
  }

  double list_top() const { return kTabsH + kHeaderH; }
  double list_height() const { return height - list_top() - kFooterH; }
  void clamp_scroll() {
    const double max = std::max(0.0, static_cast<double>(rows.size()) * kRowH - list_height());
    scroll = std::clamp(scroll, 0.0, max);
  }

  void text_at(cairo_t* cr, const std::string& s, double x, double y_mid, double px, const Color& c, bool bold = false) {
    const TextExtents te = measure_text(cr, s, px, bold);
    draw_text(cr, s, x, y_mid - te.height / 2 + te.ascent, px, c, bold);
  }
  void text_right(cairo_t* cr, const std::string& s, double x_right, double y_mid, double px, const Color& c) {
    const TextExtents te = measure_text(cr, s, px);
    draw_text(cr, s, x_right - te.width, y_mid - te.height / 2 + te.ascent, px, c);
  }

  void button(cairo_t* cr, const Rect& r, const char* label, bool enabled, bool accent) {
    rounded_rect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, pal.rounded ? 5 : 0);
    set_source(cr, accent && enabled ? alpha(pal.accent, 0.9) : alpha(pal.bg_secondary, 1));
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.fg_secondary, enabled ? 0.45 : 0.2));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    const TextExtents te = measure_text(cr, label, kFont);
    draw_text(cr, label, r.x + (r.w - te.width) / 2, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
              enabled ? (accent ? pal.bg_primary : pal.fg_primary) : alpha(pal.fg_secondary, 0.5));
  }

  // A line graph, newest sample at the right edge. `ymax` is the value at the top.
  void graph(cairo_t* cr, const Rect& r, const History& h, double ymax, const Color& c, bool frame = true, const History* second = nullptr, const Color* c2 = nullptr) {
    cairo_save(cr);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_clip(cr);
    set_source(cr, alpha(pal.bg_primary, 1));
    cairo_paint(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.14));
    cairo_set_line_width(cr, 1);
    for (int i = 1; i < 4; ++i) {
      cairo_move_to(cr, r.x, std::floor(r.y + r.h * i / 4.0) + 0.5);
      cairo_line_to(cr, r.x + r.w, std::floor(r.y + r.h * i / 4.0) + 0.5);
    }
    cairo_stroke(cr);
    auto plot = [&](const History& hist, const Color& col) {
      const std::vector<double>& v = hist.values();
      if (v.size() < 2 || ymax <= 0) return;
      const double step = r.w / static_cast<double>(hist.capacity() - 1);
      const double x0 = r.x + r.w - step * static_cast<double>(v.size() - 1);
      cairo_new_path(cr);
      for (size_t i = 0; i < v.size(); ++i) {
        const double x = x0 + step * static_cast<double>(i), y = r.y + r.h - 1 - (r.h - 2) * std::clamp(v[i] / ymax, 0.0, 1.0);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
      }
      cairo_path_t* line = cairo_copy_path(cr);
      cairo_line_to(cr, r.x + r.w, r.y + r.h);
      cairo_line_to(cr, x0, r.y + r.h);
      cairo_close_path(cr);
      set_source(cr, alpha(col, 0.22));
      cairo_fill(cr);
      cairo_new_path(cr);
      cairo_append_path(cr, line);
      cairo_path_destroy(line);
      set_source(cr, alpha(col, 0.95));
      cairo_set_line_width(cr, 1.4);
      cairo_stroke(cr);
    };
    plot(h, c);
    if (second && c2) plot(*second, *c2);
    cairo_restore(cr);
    if (frame) {
      set_source(cr, alpha(pal.fg_secondary, 0.4));
      cairo_set_line_width(cr, 1);
      cairo_rectangle(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
      cairo_stroke(cr);
    }
  }

  void draw(cairo_t* cr, int w, int h) {
    width = w;
    height = h;
    set_source(cr, pal.bg_primary);
    cairo_paint(cr);
    // Tabs.
    const char* names[2] = {"Processes", "Performance"};
    double x = 12;
    for (int i = 0; i < 2; ++i) {
      const TextExtents te = measure_text(cr, names[i], 14, tab == static_cast<Tab>(i));
      tab_rect[i] = {x, 6, te.width + 28, kTabsH - 6};
      const bool on = tab == static_cast<Tab>(i);
      rounded_rect(cr, tab_rect[i].x, tab_rect[i].y, tab_rect[i].w, tab_rect[i].h + 6, pal.rounded ? 7 : 0);
      set_source(cr, on ? alpha(pal.accent, 0.20) : alpha(pal.bg_secondary, 0.0));
      cairo_fill(cr);
      text_at(cr, names[i], tab_rect[i].x + 14, tab_rect[i].y + tab_rect[i].h / 2, 14, on ? pal.fg_primary : dim(), on);
      x += tab_rect[i].w + 6;
    }
    set_source(cr, alpha(pal.fg_secondary, 0.25));
    cairo_rectangle(cr, 0, kTabsH - 1, w, 1);
    cairo_fill(cr);
    if (tab == Tab::Processes) draw_processes(cr, w, h);
    else draw_performance(cr, w, h);
  }

  void draw_processes(cairo_t* cr, int w, int h) {
    // Column layout: name takes what the numbers leave.
    const double wp = 64, wc = 70, wm = 92, wd = 92, wu = 110;
    const double wn = std::max(120.0, w - wp - wc - wm - wd - wu - 16);
    const double xs[7] = {8, 8 + wn, 8 + wn + wp, 8 + wn + wp + wc, 8 + wn + wp + wc + wm, 8 + wn + wp + wc + wm + wd, 8 + wn + wp + wc + wm + wd + wu};
    const char* heads[6] = {"Name", "PID", "CPU", "Memory", "Disk", "User"};
    const ProcColumn cols[6] = {ProcColumn::Name, ProcColumn::Pid, ProcColumn::Cpu, ProcColumn::Memory, ProcColumn::Disk, ProcColumn::User};
    const bool right[6] = {false, true, true, true, true, false};
    set_source(cr, alpha(pal.bg_secondary, 1));
    cairo_rectangle(cr, 0, kTabsH, w, kHeaderH);
    cairo_fill(cr);
    // Machine totals, shown in the footer.
    const int mem_pct = mem.total ? static_cast<int>(100.0 * static_cast<double>(mem.total - mem.available) / static_cast<double>(mem.total) + 0.5) : 0;
    char cpu_t[16], mem_t[16];
    std::snprintf(cpu_t, sizeof cpu_t, "%d%%", cpu_pct);
    std::snprintf(mem_t, sizeof mem_t, "%d%%", mem_pct);
    for (int i = 0; i < 6; ++i) {
      col_rect[i] = {xs[i], kTabsH, xs[i + 1] - xs[i], kHeaderH};
      std::string label = heads[i];
      if (sort_col == cols[i]) label += sort_desc ? " v" : " ^";
      if (right[i]) text_right(cr, label, xs[i + 1] - 8, kTabsH + kHeaderH / 2, kFont, sort_col == cols[i] ? pal.fg_primary : dim());
      else text_at(cr, label, xs[i] + 4, kTabsH + kHeaderH / 2, kFont, sort_col == cols[i] ? pal.fg_primary : dim());
      if (i > 0) {
        set_source(cr, alpha(pal.fg_secondary, 0.15));
        cairo_rectangle(cr, xs[i], kTabsH + 4, 1, kHeaderH - 8);
        cairo_fill(cr);
      }
    }

    // Rows (only the visible ones are drawn).
    const double top = list_top(), bottom = top + list_height();
    cairo_save(cr);
    cairo_rectangle(cr, 0, top, w, bottom - top);
    cairo_clip(cr);
    const size_t first = static_cast<size_t>(std::max(0.0, scroll / kRowH));
    for (size_t i = first; i < rows.size(); ++i) {
      const double y = top + static_cast<double>(i) * kRowH - scroll;
      if (y >= bottom) break;
      const ProcRow& r = rows[i];
      const bool sel = r.info.pid == selected_pid;
      if (sel) {
        set_source(cr, alpha(pal.accent, 0.30));
        cairo_rectangle(cr, 0, y, w, kRowH);
        cairo_fill(cr);
      } else if (i % 2) {
        set_source(cr, alpha(pal.bg_secondary, 0.35));
        cairo_rectangle(cr, 0, y, w, kRowH);
        cairo_fill(cr);
      }
      const double ym = y + kRowH / 2;
      const Color fg = r.info.state == 'Z' ? alpha(pal.fg_secondary, 0.6) : pal.fg_primary;
      text_at(cr, r.info.name, xs[0] + 4, ym, kFont, fg);
      text_right(cr, std::to_string(r.info.pid), xs[2] - 8, ym, kFont, dim());
      char b[32];
      std::snprintf(b, sizeof b, r.cpu_percent < 0.05 ? "0%%" : "%.1f%%", r.cpu_percent);
      // Busier rows get a warmer tint behind the number, like the Windows 10 heat map.
      if (r.cpu_percent >= 5) {
        set_source(cr, alpha(pal.accent, std::min(0.45, r.cpu_percent / 100.0 + 0.1)));
        cairo_rectangle(cr, xs[2] + 4, y + 1, xs[3] - xs[2] - 4, kRowH - 2);
        cairo_fill(cr);
      }
      text_right(cr, b, xs[3] - 8, ym, kFont, r.cpu_percent >= 0.05 ? pal.fg_primary : dim());
      text_right(cr, format_kib(r.info.rss_kb), xs[4] - 8, ym, kFont, pal.fg_primary);
      text_right(cr, r.disk_bps < 1 ? std::string("0 B/s") : format_rate(r.disk_bps), xs[5] - 8, ym, kFont, r.disk_bps >= 1 ? pal.fg_primary : dim());
      const auto u = users.find(r.info.uid);
      text_at(cr, u == users.end() ? std::to_string(r.info.uid) : u->second, xs[5] + 4, ym, kFont, dim());
    }
    cairo_restore(cr);

    // Scroll bar.
    const double total = static_cast<double>(rows.size()) * kRowH;
    if (total > bottom - top) {
      const double bar_h = std::max(24.0, (bottom - top) * (bottom - top) / total);
      const double bar_y = top + (bottom - top - bar_h) * (scroll / (total - (bottom - top)));
      rounded_rect(cr, w - 8, bar_y, 5, bar_h, 2.5);
      set_source(cr, alpha(pal.fg_secondary, 0.4));
      cairo_fill(cr);
    }

    // Footer: totals and the buttons.
    const double fy = h - kFooterH;
    set_source(cr, alpha(pal.bg_secondary, 1));
    cairo_rectangle(cr, 0, fy, w, kFooterH);
    cairo_fill(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.25));
    cairo_rectangle(cr, 0, fy, w, 1);
    cairo_fill(cr);
    char foot[128];
    std::snprintf(foot, sizeof foot, "Processes: %zu     CPU: %s     Memory: %s", rows.size(), cpu_t, mem_t);
    text_at(cr, foot, 12, fy + kFooterH / 2 - (status.empty() ? 0 : 8), kFont, pal.fg_primary);
    if (!status.empty()) text_at(cr, status, 12, fy + kFooterH / 2 + 9, kSmall, dim());
    const bool have = selected_pid > 1;
    force_btn = {w - 12.0 - 104, fy + 8, 104, kFooterH - 16};
    end_btn = {force_btn.x - 8 - 104, fy + 8, 104, kFooterH - 16};
    button(cr, end_btn, "End task", have, true);
    button(cr, force_btn, "Force quit", have, false);
  }

  // The five resources on the left, the selected one's detail on the right.
  void draw_performance(cairo_t* cr, int w, int h) {
    set_source(cr, alpha(pal.bg_secondary, 1));
    cairo_rectangle(cr, 0, kTabsH, kSideW, h - kTabsH);
    cairo_fill(cr);
    struct Item {
      const char* name;
      std::string value;
      const History* hist;
      double ymax;
      const History* hist2;
    };
    char b1[48], b2[48];
    const double mem_pct = mem.total ? 100.0 * static_cast<double>(mem.total - mem.available) / static_cast<double>(mem.total) : 0;
    std::snprintf(b1, sizeof b1, "%d%%", cpu_pct);
    std::snprintf(b2, sizeof b2, "%.0f%%  %s", mem_pct, format_kib(static_cast<long long>(mem.total - mem.available)).c_str());
    const double disk_max = std::max({1.0e6, disk_read_h.max(), disk_write_h.max()});
    const double net_max = std::max({1.0e5, net_rx_h.max(), net_tx_h.max()});
    const Item items[5] = {{"CPU", b1, &cpu_h, 100, nullptr},
                           {"Memory", b2, &mem_h, 100, nullptr},
                           {"Disk", "R " + format_rate(disk_read_bps) + " W " + format_rate(disk_write_bps), &disk_read_h, disk_max, &disk_write_h},
                           {"Network", "Rx " + format_rate(net_rx_bps) + " Tx " + format_rate(net_tx_bps), &net_rx_h, net_max, &net_tx_h},
                           {"GPU", gpu.any() ? (gpu.busiest_percent() < 0 ? std::string("--") : std::to_string(gpu.busiest_percent()) + "%") : std::string("not found"), &gpu_h, 100, nullptr}};
    const Color c1 = pal.accent, c2{0.95, 0.55, 0.25, 1};
    for (int i = 0; i < 5; ++i) {
      res_rect[i] = {0, kTabsH + 6.0 + i * 66, kSideW, 62};
      const bool on = res == static_cast<Res>(i);
      if (on) {
        set_source(cr, alpha(pal.accent, 0.20));
        cairo_rectangle(cr, 4, res_rect[i].y, kSideW - 8, res_rect[i].h);
        cairo_fill(cr);
      }
      graph(cr, {12, res_rect[i].y + 8, 62, 46}, *items[i].hist, items[i].ymax, c1, true, items[i].hist2, &c2);
      text_at(cr, items[i].name, 84, res_rect[i].y + 20, kFont, pal.fg_primary, true);
      cairo_save(cr);
      cairo_rectangle(cr, 84, res_rect[i].y, kSideW - 88, res_rect[i].h);
      cairo_clip(cr);
      text_at(cr, items[i].value, 84, res_rect[i].y + 40, kSmall, dim());
      cairo_restore(cr);
    }
    // Detail.
    const double dx = kSideW + 16, dw = w - dx - 16;
    double y = kTabsH + 14;
    auto line = [&](const char* label, const std::string& value) {
      text_at(cr, label, dx, y + 8, kSmall, dim());
      text_at(cr, value, dx + 150, y + 8, kFont, pal.fg_primary);
      y += 20;
    };
    switch (res) {
      case Res::Cpu: {
        text_at(cr, "CPU", dx, y + 8, 18, pal.fg_primary, true);
        y += 26;
        graph(cr, {dx, y, dw, 150}, cpu_h, 100, c1);
        y += 160;
        const size_t n = core_h.size();
        if (n > 0) {
          const int cols = n <= 4 ? static_cast<int>(n) : n <= 12 ? 4 : 8;
          const int rows_n = static_cast<int>((n + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
          const double cw = (dw - (cols - 1) * 6) / cols, ch = std::clamp((h - y - 190.0) / rows_n - 6, 22.0, 54.0);
          for (size_t i = 0; i < n; ++i) {
            const Rect r{dx + static_cast<double>(i % static_cast<size_t>(cols)) * (cw + 6), y + static_cast<double>(i / static_cast<size_t>(cols)) * (ch + 6), cw, ch};
            graph(cr, r, core_h[i], 100, c1);
          }
          y += rows_n * (ch + 6) + 6;
        }
        char t[64];
        std::snprintf(t, sizeof t, "%d%%", cpu_pct);
        line("Utilisation", t);
        line("Processes", std::to_string(rows.empty() ? 0 : rows.size()));
        line("Cores", std::to_string(ncpu));
        line("Load average", load_avg.empty() ? "--" : load_avg);
        {
          const long d = uptime_s / 86400, hh = uptime_s / 3600 % 24, mm = uptime_s / 60 % 60;
          std::snprintf(t, sizeof t, "%ld:%02ld:%02ld:%02ld", d, hh, mm, uptime_s % 60);
          line("Up time", uptime_s ? t : "--");
        }
        std::snprintf(t, sizeof t, "%.0f us (a 200 us sleep, best of 3)", sched_latency_us);
        line("Scheduler latency", sched_latency_us < 0 ? "--" : t);
        std::snprintf(t, sizeof t, "%.1f ms (PipeWire quantum)", audio_latency_ms);
        line("Audio latency", audio_latency_ms < 0 ? "not available" : t);
        break;
      }
      case Res::Memory: {
        text_at(cr, "Memory", dx, y + 8, 18, pal.fg_primary, true);
        y += 26;
        graph(cr, {dx, y, dw, 170}, mem_h, 100, c1);
        y += 184;
        const unsigned long long used = mem.total - mem.available;
        line("In use", format_kib(static_cast<long long>(used)));
        line("Available", format_kib(static_cast<long long>(mem.available)));
        line("Total", format_kib(static_cast<long long>(mem.total)));
        line("Cached", format_kib(static_cast<long long>(mem.cached)));
        line("Buffers", format_kib(static_cast<long long>(mem.buffers)));
        line("Swap in use", mem.swap_total ? format_kib(static_cast<long long>(mem.swap_total - mem.swap_free)) + " of " + format_kib(static_cast<long long>(mem.swap_total)) : "no swap");
        break;
      }
      case Res::Disk:
        text_at(cr, "Disk", dx, y + 8, 18, pal.fg_primary, true);
        y += 26;
        graph(cr, {dx, y, dw, 170}, disk_read_h, disk_max, c1, true, &disk_write_h, &c2);
        y += 184;
        line("Read", format_rate(disk_read_bps));
        line("Write", format_rate(disk_write_bps));
        text_at(cr, "Blue: read, orange: write. Whole disks only (partitions are not counted twice).", dx, y + 8, kSmall, dim());
        break;
      case Res::Network:
        text_at(cr, "Network", dx, y + 8, 18, pal.fg_primary, true);
        y += 26;
        graph(cr, {dx, y, dw, 170}, net_rx_h, net_max, c1, true, &net_tx_h, &c2);
        y += 184;
        line("Receive", format_rate(net_rx_bps));
        line("Send", format_rate(net_tx_bps));
        text_at(cr, "Blue: receive, orange: send. All interfaces except loopback.", dx, y + 8, kSmall, dim());
        break;
      case Res::Gpu: {
        text_at(cr, "GPU", dx, y + 8, 18, pal.fg_primary, true);
        y += 26;
        if (!gpu.any()) {
          text_at(cr, "No GPU utilisation source found (software rendering or an unsupported driver).", dx, y + 8, kFont, dim());
          break;
        }
        graph(cr, {dx, y, dw, 170}, gpu_h, 100, c1);
        y += 184;
        line("Utilisation", gpu.text());
        for (const GpuDevice& g : gpu.devices()) {
          line("Card", g.vendor + " (" + g.driver + ")");
          if (!g.freq_path.empty()) {
            const long long f = GpuMonitor::read_number(g.freq_path);
            if (f >= 0) line("Core clock", std::to_string(f) + " MHz");
          }
        }
        break;
      }
    }
  }

  // ---------------------------------------------------------------- input --
  void select_by_index(long i) {
    if (rows.empty()) return;
    i = std::clamp(i, 0L, static_cast<long>(rows.size()) - 1);
    selected_pid = rows[static_cast<size_t>(i)].info.pid;
    const double top = static_cast<double>(i) * kRowH;
    if (top < scroll) scroll = top;
    if (top + kRowH > scroll + list_height()) scroll = top + kRowH - list_height();
    clamp_scroll();
  }

  long selected_index() const {
    for (size_t i = 0; i < rows.size(); ++i)
      if (rows[i].info.pid == selected_pid) return static_cast<long>(i);
    return -1;
  }

  void end_task(bool force) {
    if (selected_pid <= 1) return;
    if (kill(selected_pid, force ? SIGKILL : SIGTERM) != 0) status = std::string("Could not end it: ") + std::strerror(errno);
    else status = (force ? "Killed process " : "Asked process to quit: ") + std::to_string(selected_pid);
    surface->queue_draw();
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (!pressed || b != kBtnLeft) return;
    for (int i = 0; i < 2; ++i)
      if (tab_rect[i].hit(x, y) && tab != static_cast<Tab>(i)) {
        tab = static_cast<Tab>(i);
        status.clear();
        ticks = 0;
        tick();  // fresh numbers for the page just opened
        return;
      }
    if (tab == Tab::Performance) {
      for (int i = 0; i < 5; ++i)
        if (res_rect[i].hit(x, y)) {
          res = static_cast<Res>(i);
          surface->queue_draw();
        }
      return;
    }
    if (end_btn.hit(x, y)) return end_task(false);
    if (force_btn.hit(x, y)) return end_task(true);
    const ProcColumn cols[6] = {ProcColumn::Name, ProcColumn::Pid, ProcColumn::Cpu, ProcColumn::Memory, ProcColumn::Disk, ProcColumn::User};
    for (int i = 0; i < 6; ++i)
      if (col_rect[i].hit(x, y)) {
        if (sort_col == cols[i]) sort_desc = !sort_desc;
        else {
          sort_col = cols[i];
          sort_desc = i != 0 && i != 5;  // numbers: biggest first; names: A to Z
        }
        sort_rows(&rows, sort_col, sort_desc, users);
        surface->queue_draw();
        return;
      }
    if (y >= list_top() && y < list_top() + list_height()) {
      const size_t i = static_cast<size_t>((y - list_top() + scroll) / kRowH);
      if (i < rows.size()) {
        selected_pid = rows[i].info.pid;
        status.clear();
        surface->queue_draw();
      }
    }
  }

  void on_scroll(double, double dy) {
    if (tab != Tab::Processes) return;
    scroll += dy * 0.5;  // wheel steps arrive as roughly 10-15 units
    clamp_scroll();
    surface->queue_draw();
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    switch (ev.sym) {
      case XKB_KEY_Tab:
        tab = tab == Tab::Processes ? Tab::Performance : Tab::Processes;
        ticks = 0;
        tick();
        break;
      case XKB_KEY_Down:
        if (tab == Tab::Processes) select_by_index(selected_index() + 1), surface->queue_draw();
        break;
      case XKB_KEY_Up:
        if (tab == Tab::Processes) select_by_index(selected_index() < 0 ? 0 : selected_index() - 1), surface->queue_draw();
        break;
      case XKB_KEY_Page_Down:
        if (tab == Tab::Processes) select_by_index(selected_index() + static_cast<long>(list_height() / kRowH)), surface->queue_draw();
        break;
      case XKB_KEY_Page_Up:
        if (tab == Tab::Processes) select_by_index(selected_index() - static_cast<long>(list_height() / kRowH)), surface->queue_draw();
        break;
      case XKB_KEY_Delete:
        if (tab == Tab::Processes) end_task(false);
        break;
      case XKB_KEY_Escape:
      case XKB_KEY_q:
        if (ev.sym == XKB_KEY_Escape || (ev.mods & kCtrl)) app.quit();
        break;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-taskmgr", "[--tab processes|performance]")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  fleetwm::prewarm::start("fleetwm-taskmgr");

  TaskMgr T;
  T.pal = load_palette(load_theme_config());
  T.hz = sysconf(_SC_CLK_TCK);
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--tab") == 0) T.tab = std::strcmp(argv[i + 1], "performance") == 0 ? Tab::Performance : Tab::Processes;
  T.gpu.discover();
  T.gpu.on_nvidia_update = [&T] { T.surface->queue_draw(); };
  if (!T.app.connect()) return 1;

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.TaskManager";
  cfg.title = "Task Manager";
  cfg.width = kWindowW;
  cfg.height = kWindowH;
  cfg.min_width = 760;
  cfg.min_height = 480;
  T.surface = std::make_unique<Surface>(T.app, cfg);
  T.surface->on_draw = [&T](cairo_t* cr, int w, int h) { T.draw(cr, w, h); };
  T.surface->on_button = [&T](double x, double y, uint32_t b, bool p) { T.on_button(x, y, b, p); };
  T.surface->on_scroll = [&T](double dx, double dy) { T.on_scroll(dx, dy); };
  T.surface->on_key = [&T](const KeyEvent& e) { T.on_key(e); };
  T.surface->on_closed = [&T] { T.app.quit(); };

  T.tick();                                      // the first sample sets the baselines
  T.tick_timer = T.app.add_timer(1000, [&T] { T.tick(); });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  T.app.watch_fd(sfd, [&T] { T.app.quit(); });

  T.app.run();
  return 0;
}
