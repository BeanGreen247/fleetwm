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
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
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
constexpr int kWindowW = 980, kWindowH = 680;
constexpr double kFont = 14, kSmall = 12, kRowH = 22, kHeaderH = 28, kTabsH = 40, kFooterH = 46, kSideW = 224;

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

class CacheCounters {
 public:
  ~CacheCounters() {
    if (references_ >= 0) close(references_);
    if (misses_ >= 0) close(misses_);
  }

  void open() {
    if (opened_) return;
    opened_ = true;
    references_ = create(PERF_COUNT_HW_CACHE_REFERENCES);
    misses_ = create(PERF_COUNT_HW_CACHE_MISSES);
    if (references_ < 0 || misses_ < 0) {
      status_ = "hardware counters unavailable";
      if (references_ >= 0) close(references_);
      if (misses_ >= 0) close(misses_);
      references_ = misses_ = -1;
    } else {
      status_ = "hardware counters available";
    }
  }

  void sample() {
    open();
    if (references_ < 0 || misses_ < 0) return;
    uint64_t refs = 0, misses = 0;
    if (read(references_, &refs, sizeof refs) != sizeof refs || read(misses_, &misses, sizeof misses) != sizeof misses) return;
    if (have_prev_ && refs >= prev_refs_ && misses >= prev_misses_ && refs > prev_refs_) {
      const uint64_t ref_delta = refs - prev_refs_, miss_delta = std::min(misses - prev_misses_, ref_delta);
      hit_rate_ = 100.0 * static_cast<double>(ref_delta - miss_delta) / static_cast<double>(ref_delta);
    }
    prev_refs_ = refs;
    prev_misses_ = misses;
    have_prev_ = true;
  }

  std::string status() const { return status_; }
  double hit_rate() const { return hit_rate_; }

 private:
  static int create(uint64_t event) {
    perf_event_attr attr{};
    attr.size = sizeof attr;
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = event;
    attr.disabled = 0;
    // The installer permits CPU-wide user-space counters. Keep kernel and hypervisor activity
    // out of the displayed Fleetwm/application measurements.
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    return static_cast<int>(syscall(__NR_perf_event_open, &attr, -1, 0, -1, 0));
  }
  int references_ = -1, misses_ = -1;
  bool opened_ = false, have_prev_ = false;
  uint64_t prev_refs_ = 0, prev_misses_ = 0;
  double hit_rate_ = -1;
  std::string status_ = "checking hardware counters";
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

enum class Tab { Processes, Performance, Cache, Count };
enum class Res { Cpu, Memory, Disk, Network, Gpu };
constexpr size_t kTabCount = static_cast<size_t>(Tab::Count);
constexpr std::array<const char*, kTabCount> kTabNames = {"Processes", "Performance", "Cache"};
static_assert(kTabNames.size() == kTabCount);

struct TaskMgr {
  App app;
  Palette pal;
  std::unique_ptr<Surface> surface;
  int width = kWindowW, height = kWindowH;

  Tab tab = Tab::Processes;
  Res res = Res::Cpu;
  int hovered_tab = -1;
  int hovered_res = -1;

  // ---- processes ----
  ProcessTable table;
  ProcColumn sort_col = ProcColumn::Cpu;
  bool sort_desc = true;
  int selected_pid = 0;
  double scroll = 0;
  std::unordered_map<unsigned, std::string> users;
  std::string status;
  Rect col_rect[6], end_btn, force_btn, tab_rect[kTabCount], res_rect[5];

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
  CpuInfo cpu_info;
  std::vector<CpuCacheInfo> cpu_caches;
  std::vector<MemoryModule> memory_modules;
  std::vector<DriveInfo> drives;
  std::string cache_counter_status;
  CacheCounters cache_counters;
  // The extra readings of the Performance pages: CPU time split (I/O wait ...), scheduler counts, disk latency, pressure, clocks, counts.
  CpuBreakdown prev_breakdown;
  CpuRates rates;
  bool have_breakdown = false;
  DiskActivity prev_activity;
  double disk_await = -1, disk_busy = 0;
  Pressure psi_cpu, psi_io, psi_mem;
  Reader psi_cpu_r, psi_io_r, psi_mem_r, filenr_r;
  std::vector<std::unique_ptr<Reader>> freq_r;  // one open scaling_cur_freq per logical processor
  double cpu_mhz_now = -1, cpu_mhz_max = -1;
  int proc_count = 0, thread_count = 0, running_now = 0, blocked_now = 0;
  long open_files = 0;
  int sockets = 1;
  std::string virtualization = "--";
  int ncpu = 1;
  long hz = 100;
  Clock::time_point last_sample;
  int ticks = 0;
  int tick_timer = 0;

  void read_inventory() {
    std::ifstream cpu("/proc/cpuinfo");
    std::ostringstream cpu_text;
    cpu_text << cpu.rdbuf();
    cpu_info = parse_cpuinfo(cpu_text.str());
    if (cpu_info.logical_threads == 0) cpu_info.logical_threads = std::max(1, static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN)));
    if (cpu_info.physical_cores == 0) cpu_info.physical_cores = cpu_info.logical_threads;

    std::string cache_text;
    for (int i = 0; i < 16; ++i) {
      const std::string base = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(i);
      std::ifstream level(base + "/level");
      if (!level.good()) continue;
      auto read = [&](const char* name) {
        std::ifstream f(base + "/" + name);
        std::string value;
        std::getline(f, value);
        return value;
      };
      cache_text += "level: " + read("level") + "\n";
      cache_text += "type: " + read("type") + "\n";
      cache_text += "size: " + read("size") + "\n";
      cache_text += "coherency_line_size: " + read("coherency_line_size") + "\n";
      cache_text += "ways_of_associativity: " + read("ways_of_associativity") + "\n";
      cache_text += "shared_cpu_list: " + read("shared_cpu_list") + "\n\n";
    }
    cpu_caches = parse_cpu_cache_info(cache_text);
    {  // sockets (distinct physical packages), virtualisation support, the highest clock the CPU may run at
      std::set<int> packages;
      for (int i = 0; i < cpu_info.logical_threads && i < 512; ++i) {
        std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/topology/physical_package_id");
        int id = -1;
        if (f >> id) packages.insert(id);
      }
      sockets = std::max<int>(1, static_cast<int>(packages.size()));
      std::string line;
      std::ifstream flags("/proc/cpuinfo");
      while (std::getline(flags, line))
        if (line.compare(0, 5, "flags") == 0) {
          const std::string f = " " + line.substr(line.find(':') + 1) + " ";
          virtualization = f.find(" hypervisor ") != std::string::npos ? "Virtual machine"
                           : (f.find(" vmx ") != std::string::npos || f.find(" svm ") != std::string::npos) ? "Enabled" : "Not available";
          break;
        }
      std::ifstream mx("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
      long khz = 0;
      if (mx >> khz) cpu_mhz_max = static_cast<double>(khz) / 1000.0;
    }
    std::ifstream paranoia("/proc/sys/kernel/perf_event_paranoid");
    int paranoid = 3;
    paranoia >> paranoid;
    cache_counter_status = paranoid <= 2 ? "hardware counters available" : "hardware counters restricted by kernel";

    // dmidecode is optional and is only called once at startup. It may be unavailable
    // for an unprivileged user; the page still shows kernel memory totals.
    if (FILE* p = popen("dmidecode -t memory 2>/dev/null", "r")) {
      char buf[1024];
      std::string text;
      while (std::fgets(buf, sizeof buf, p)) text += buf;
      pclose(p);
      memory_modules = parse_memory_devices(text);
    }

    std::string rows_text;
    if (DIR* d = opendir("/sys/block")) {
      while (const dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/block/") + e->d_name;
        std::ifstream size(base + "/size");
        long long sectors = 0;
        size >> sectors;
        if (sectors <= 0) continue;
        auto read_file = [](const std::string& path) {
          std::ifstream f(path);
          std::string v;
          std::getline(f, v);
          return v.empty() ? std::string("--") : v;
        };
        rows_text += std::string(e->d_name) + "\t" + std::to_string(sectors * 512) + "\t" +
                     read_file(base + "/device/model") + "\t" + read_file(base + "/device/vendor") + "\t" +
                     read_file(base + "/device/serial") + "\t" + read_file(base + "/device/subsystem") + "\n";
      }
      closedir(d);
    }
    drives = parse_drive_inventory(rows_text);
  }

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
    {
      CpuBreakdown bd;
      if (parse_proc_stat_breakdown(st, &bd)) {
        if (have_breakdown) rates = cpu_rates(prev_breakdown, bd, dt);
        prev_breakdown = bd;
        have_breakdown = true;
        running_now = bd.running;
        blocked_now = bd.blocked;
      }
    }
    cpu_h.push(cpu_pct);
    ncpu = std::max<int>(1, static_cast<int>(cores.size()));

    if (parse_meminfo(mem_r.read("/proc/meminfo"), &mem) && mem.total > 0)
      mem_h.push(100.0 * static_cast<double>(mem.total - mem.available) / static_cast<double>(mem.total));

    const std::string& ds = disk_r.read("/proc/diskstats");
    const DiskCounters d = parse_diskstats(ds);
    const DiskActivity act = parse_disk_activity(ds);
    if (have_prev && dt > 0) {
      disk_await = disk_await_ms(prev_activity, act);
      disk_busy = disk_busy_percent(prev_activity, act, dt);
      disk_read_bps = static_cast<double>(d.read_bytes >= prev_disk.read_bytes ? d.read_bytes - prev_disk.read_bytes : 0) / dt;
      disk_write_bps = static_cast<double>(d.write_bytes >= prev_disk.write_bytes ? d.write_bytes - prev_disk.write_bytes : 0) / dt;
    }
    prev_disk = d;
    prev_activity = act;
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
    cache_counters.sample();
    if (cache_counters.status() == "hardware counters available") cache_counter_status = cache_counters.status();

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
    {  // threads (the total in loadavg's 4th field), open files, processes, pressure, the current clock
      int running = 0, total = 0;
      if (std::sscanf(l.c_str(), "%*f %*f %*f %d/%d", &running, &total) == 2) thread_count = total;
      open_files = std::atol(filenr_r.read("/proc/sys/fs/file-nr").c_str());
      if (ticks % 2 == 1) {
        int n = 0;
        if (DIR* d = opendir("/proc")) {
          while (const dirent* e = readdir(d))
            if (e->d_name[0] >= '1' && e->d_name[0] <= '9') ++n;
          closedir(d);
        }
        proc_count = n;
      }
      parse_pressure(psi_cpu_r.read("/proc/pressure/cpu"), &psi_cpu);
      parse_pressure(psi_io_r.read("/proc/pressure/io"), &psi_io);
      parse_pressure(psi_mem_r.read("/proc/pressure/memory"), &psi_mem);
      if (freq_r.empty()) freq_r.resize(static_cast<size_t>(std::max(1, ncpu)));
      double sum = 0;
      int got = 0;
      for (size_t i = 0; i < freq_r.size(); ++i) {
        if (!freq_r[i]) freq_r[i] = std::make_unique<Reader>();
        const std::string& t = freq_r[i]->read(("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq").c_str());
        const long khz = std::atol(t.c_str());
        if (khz > 0) sum += static_cast<double>(khz) / 1000.0, ++got;
      }
      cpu_mhz_now = got ? sum / got : -1;
    }
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
      sort_rows(&table.rows(), sort_col, sort_desc, users);
      for (const ProcRow& r : table.rows())
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
    const double max = std::max(0.0, static_cast<double>(table.rows().size()) * kRowH - list_height());
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

  void info_grid(cairo_t* cr, double x, double y, double w,
                 const std::vector<std::pair<std::string, std::string>>& entries) {
    const double gap = 18, col_w = (w - gap) / 2.0, row_h = 38;
    for (size_t i = 0; i < entries.size(); ++i) {
      const size_t col = i % 2, row = i / 2;
      const double cx = x + static_cast<double>(col) * (col_w + gap);
      const double cy = y + static_cast<double>(row) * row_h;
      text_at(cr, entries[i].first, cx, cy + 9, kSmall, dim());
      cairo_save(cr);
      cairo_rectangle(cr, cx, cy + 16, col_w, row_h - 16);
      cairo_clip(cr);
      text_at(cr, entries[i].second.empty() ? "--" : entries[i].second, cx, cy + 28, kFont, pal.fg_primary, true);
      cairo_restore(cr);
    }
  }

  // A Windows-style figure: a small dim label above a large value.
  void big_stat(cairo_t* cr, double x, double y, const std::string& label, const std::string& value) {
    text_at(cr, label, x, y + 7, kSmall, dim());
    text_at(cr, value.empty() ? "--" : value, x, y + 29, 22, pal.fg_primary);
  }
  // Label and value on one row (the value starts at x + label_w); returns the y of the next row.
  double kv_row(cairo_t* cr, double x, double y, double label_w, const std::string& label, const std::string& value) {
    text_at(cr, label, x, y + 8, kSmall, dim());
    text_at(cr, value.empty() ? "--" : value, x + label_w, y + 8, kFont, pal.fg_primary);
    return y + 19;
  }
  // A heading in the accent colour with a hairline running on to the right edge.
  void section_label(cairo_t* cr, double x, double y, double w, const char* title) {
    text_at(cr, title, x, y + 8, kSmall, pal.accent, true);
    const double tw = measure_text(cr, title, kSmall, true).width;
    set_source(cr, alpha(pal.fg_secondary, 0.22));
    cairo_rectangle(cr, x + tw + 10, y + 8, std::max(0.0, w - tw - 10), 1);
    cairo_fill(cr);
  }
  // Small label over a normal-size value, for the dense grids.
  void small_cell(cairo_t* cr, double x, double y, double w, const std::string& label, const std::string& value) {
    text_at(cr, label, x, y + 7, kSmall, dim());
    cairo_save(cr);
    cairo_rectangle(cr, x, y + 14, w, 22);
    cairo_clip(cr);
    text_at(cr, value.empty() ? "--" : value, x, y + 27, kFont, pal.fg_primary, true);
    cairo_restore(cr);
  }
  static std::string num1(double v, const char* unit = "") {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f%s", v, unit);
    return b;
  }
  static std::string count_per_s(double v) {
    char b[32];
    if (v >= 100000) std::snprintf(b, sizeof b, "%.0fk/s", v / 1000);
    else if (v >= 10000) std::snprintf(b, sizeof b, "%.1fk/s", v / 1000);
    else std::snprintf(b, sizeof b, "%.0f/s", v);
    return b;
  }
  static std::string ghz(double mhz) {
    char b[32];
    if (mhz <= 0) return "--";
    std::snprintf(b, sizeof b, "%.2f GHz", mhz / 1000.0);
    return b;
  }
  std::string cache_size(int level) const {  // "32 KB x 2" style: the size of the level's biggest instance, as the kernel reports it
    for (const CpuCacheInfo& c : cpu_caches)
      if (c.level == level && c.type != "Instruction") return c.size.empty() ? "--" : c.size;
    return "--";
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
    double x = 12;
    for (size_t i = 0; i < kTabNames.size(); ++i) {
      const TextExtents te = measure_text(cr, kTabNames[i], 14, tab == static_cast<Tab>(i));
      tab_rect[i] = {x, 0, te.width + 28, kTabsH};
      const bool on = tab == static_cast<Tab>(i);
      if (on || hovered_tab == static_cast<int>(i)) {
        rounded_rect(cr, tab_rect[i].x + 3, 4, tab_rect[i].w - 6, kTabsH - 8, pal.rounded ? 6 : 0);
        set_source(cr, on ? alpha(pal.accent, 0.16) : alpha(pal.bg_secondary, 0.8));
        cairo_fill(cr);
      }
      if (on) {
        rounded_rect(cr, tab_rect[i].x + 11, kTabsH - 3, tab_rect[i].w - 22, 2, 1);
        set_source(cr, pal.accent);
        cairo_fill(cr);
      }
      text_at(cr, kTabNames[i], tab_rect[i].x + 14, kTabsH / 2, 14, on ? pal.fg_primary : dim(), on);
      x += tab_rect[i].w + 6;
    }
    set_source(cr, alpha(pal.fg_secondary, 0.25));
    cairo_rectangle(cr, 0, kTabsH - 1, w, 1);
    cairo_fill(cr);
    if (tab == Tab::Processes) draw_processes(cr, w, h);
    else if (tab == Tab::Performance) draw_performance(cr, w, h);
    else draw_cache(cr, w, h);
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
    for (size_t i = first; i < table.rows().size(); ++i) {
      const double y = top + static_cast<double>(i) * kRowH - scroll;
      if (y >= bottom) break;
      const ProcRow& r = table.rows()[i];
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
    const double total = static_cast<double>(table.rows().size()) * kRowH;
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
    std::snprintf(foot, sizeof foot, "Processes: %zu     CPU: %s     Memory: %s", table.rows().size(), cpu_t, mem_t);
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
    const bool intel_awake_proxy = std::any_of(gpu.devices().begin(), gpu.devices().end(), [](const GpuDevice& device) {
      return device.kind == GpuDevice::Kind::IntelIdle;
    });
    const bool only_intel_awake_proxy =
        intel_awake_proxy && !gpu.nvidia_available() &&
        std::all_of(gpu.devices().begin(), gpu.devices().end(), [](const GpuDevice& device) {
          return device.kind == GpuDevice::Kind::IntelIdle;
        });
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
      const bool hot = hovered_res == i;
      if (on || hot) {
        rounded_rect(cr, 4, res_rect[i].y, kSideW - 8, res_rect[i].h, pal.rounded ? 5 : 0);
        set_source(cr, on ? alpha(pal.accent, 0.20) : alpha(pal.bg_primary, 0.55));
        cairo_fill(cr);
      }
      graph(cr, {12, res_rect[i].y + 8, 62, 46}, *items[i].hist, items[i].ymax, c1, true, items[i].hist2, &c2);
      text_at(cr, items[i].name, 84, res_rect[i].y + 20, kFont, pal.fg_primary, true);
      cairo_save(cr);
      cairo_rectangle(cr, 84, res_rect[i].y, kSideW - 88, res_rect[i].h);
      cairo_clip(cr);
      text_at(cr, items[i].value, 84, res_rect[i].y + 40, kSmall, dim());
      cairo_restore(cr);
      if (hot) {
        rounded_rect(cr, 4.5, res_rect[i].y + 0.5, kSideW - 9, res_rect[i].h - 1, pal.rounded ? 5 : 0);
        set_source(cr, alpha(pal.accent, 0.65));
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
      }
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
        text_right(cr, cpu_info.model.empty() ? "Unknown processor" : cpu_info.model, dx + dw, y + 8, kFont, dim());
        y += 28;
        text_at(cr, "% Utilization", dx, y + 6, kSmall, dim());
        text_right(cr, "100%", dx + dw, y + 6, kSmall, dim());
        y += 14;
        // What sits below the graph: the per-core graphs, the figures and the scheduler grid. The graph takes what is left.
        const size_t n = core_h.size();
        const int cols = n <= 4 ? std::max(1, static_cast<int>(n)) : n <= 12 ? 4 : 8;
        const int rows_n = n ? static_cast<int>((n + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols)) : 0;
        const double ch = std::clamp((h - y - 330.0) / std::max(1, rows_n) - 6, 20.0, 36.0);
        const double below = 8 + rows_n * (ch + 6) + 6 + 3 * 46 + 12 + 22 + 3 * 38;
        const double gh = std::clamp(h - y - below - 12, 56.0, 150.0);
        graph(cr, {dx, y, dw, gh}, cpu_h, 100, c1);
        y += gh + 8;
        if (n > 0) {
          const double cw = (dw - (cols - 1) * 6) / cols;
          for (size_t i = 0; i < n; ++i) {
            const Rect r{dx + static_cast<double>(i % static_cast<size_t>(cols)) * (cw + 6), y + static_cast<double>(i / static_cast<size_t>(cols)) * (ch + 6), cw, ch};
            graph(cr, r, core_h[i], 100, c1);
          }
          y += rows_n * (ch + 6) + 6;
        }
        // Windows' arrangement: the live figures at the left, the fixed facts of the processor at the right.
        const double half = dw * 0.5;
        char t[64];
        std::snprintf(t, sizeof t, "%d%%", cpu_pct);
        big_stat(cr, dx, y, "Utilization", t);
        big_stat(cr, dx + half * 0.5, y, "Speed", ghz(cpu_mhz_now));
        big_stat(cr, dx, y + 46, "Processes", proc_count ? std::to_string(proc_count) : std::string());
        big_stat(cr, dx + half * 0.5 - 28, y + 46, "Threads", thread_count ? std::to_string(thread_count) : std::string());
        big_stat(cr, dx + half * 0.5 + 62, y + 46, "Open files", open_files ? std::to_string(open_files) : std::string());
        {
          const long d = uptime_s / 86400, hh = uptime_s / 3600 % 24, mm = uptime_s / 60 % 60;
          std::snprintf(t, sizeof t, "%ld:%02ld:%02ld:%02ld", d, hh, mm, uptime_s % 60);
          big_stat(cr, dx, y + 92, "Up time", uptime_s ? t : "");
        }
        {
          const double kx = dx + half + 16, lw = 124;
          double ky = y;
          ky = kv_row(cr, kx, ky, lw, "Maximum speed:", ghz(cpu_mhz_max > 0 ? cpu_mhz_max : static_cast<double>(cpu_info.mhz)));
          ky = kv_row(cr, kx, ky, lw, "Sockets:", std::to_string(sockets));
          ky = kv_row(cr, kx, ky, lw, "Cores:", std::to_string(cpu_info.physical_cores));
          ky = kv_row(cr, kx, ky, lw, "Logical processors:", std::to_string(cpu_info.logical_threads));
          ky = kv_row(cr, kx, ky, lw, "Virtualization:", virtualization);
          ky = kv_row(cr, kx, ky, lw, "L1 cache:", cache_size(1));
          ky = kv_row(cr, kx, ky, lw, "L2 cache:", cache_size(2));
          kv_row(cr, kx, ky, lw, "L3 cache:", cache_size(3));
        }
        y += 3 * 46 + 12;
        section_label(cr, dx, y, dw, "Scheduler and I/O");
        y += 22;
        const double cw4 = dw / 4.0;
        const CpuRates& r = rates;
        auto cell = [&](int col, int row, const std::string& label, const std::string& value) {
          small_cell(cr, dx + col * cw4, y + row * 38.0, cw4 - 8, label, value);
        };
        cell(0, 0, "I/O wait", r.valid ? num1(r.iowait, "%") : std::string());
        cell(1, 0, "Disk latency", disk_await >= 0 ? num1(disk_await, " ms") : std::string("idle"));
        cell(2, 0, "Disk busy", num1(disk_busy, "%"));
        cell(3, 0, "I/O pressure", psi_io.some10 >= 0 ? num1(psi_io.some10, "%") : std::string());
        cell(0, 1, "Context switches", r.valid ? count_per_s(r.ctxt_per_s) : std::string());
        cell(1, 1, "Interrupts", r.valid ? count_per_s(r.intr_per_s) : std::string());
        cell(2, 1, "New tasks", r.valid ? count_per_s(r.forks_per_s) : std::string());
        cell(3, 1, "CPU pressure", psi_cpu.some10 >= 0 ? num1(psi_cpu.some10, "%") : std::string());
        cell(0, 2, "Running / blocked", std::to_string(running_now) + " / " + std::to_string(blocked_now));
        cell(1, 2, "Timer latency", sched_latency_us < 0 ? std::string() : std::to_string(static_cast<int>(sched_latency_us)) + " us");
        cell(2, 2, "Audio latency", audio_latency_ms < 0 ? std::string("n/a") : num1(audio_latency_ms, " ms"));
        cell(3, 2, "Load average", load_avg);
        break;
      }
      case Res::Memory: {
        const MemComposition comp = mem_composition(mem);
        text_at(cr, "Memory", dx, y + 8, 18, pal.fg_primary, true);
        text_right(cr, format_kib(static_cast<long long>(mem.total)), dx + dw, y + 8, kFont, dim());
        y += 28;
        text_at(cr, "Memory usage", dx, y + 6, kSmall, dim());
        text_right(cr, format_kib(static_cast<long long>(mem.total)), dx + dw, y + 6, kSmall, dim());
        y += 14;
        graph(cr, {dx, y, dw, 120}, mem_h, 100, c1);
        y += 128;
        // The composition bar: in use, shared, cache and buffers, free (they add up to the installed memory).
        section_label(cr, dx, y, dw, "Memory composition");
        y += 22;
        {
          const Color seg_colors[4] = {c1, c2, alpha(pal.accent, 0.38), alpha(pal.fg_secondary, 0.14)};
          const unsigned long long vals[4] = {comp.used, comp.shared, comp.cache_buffers, comp.free};
          const char* names[4] = {"In use", "Shared", "Cache + buffers", "Free"};
          double bx = dx;
          const double total = std::max(1.0, static_cast<double>(comp.total));
          for (int i = 0; i < 4; ++i) {
            const double bw = dw * static_cast<double>(vals[i]) / total;
            if (bw >= 0.5) {
              set_source(cr, seg_colors[i]);
              cairo_rectangle(cr, bx, y, bw, 20);
              cairo_fill(cr);
            }
            bx += bw;
          }
          set_source(cr, alpha(pal.fg_secondary, 0.4));
          cairo_set_line_width(cr, 1);
          cairo_rectangle(cr, dx + 0.5, y + 0.5, dw - 1, 19);
          cairo_stroke(cr);
          double lx = dx;
          for (int i = 0; i < 4; ++i) {
            set_source(cr, seg_colors[i]);
            cairo_rectangle(cr, lx, y + 29, 10, 10);
            cairo_fill(cr);
            const std::string label = std::string(names[i]) + "  " + format_kib(static_cast<long long>(vals[i]));
            text_at(cr, label, lx + 15, y + 34, kSmall, pal.fg_primary);
            lx += 15 + measure_text(cr, label, kSmall).width + 18;
          }
          y += 48;
        }
        {
          const double half = dw * 0.5, lw = 118;
          double ly = y, ry = y;
          ly = kv_row(cr, dx, ly, lw, "In use:", format_kib(static_cast<long long>(comp.used)));
          ly = kv_row(cr, dx, ly, lw, "Shared:", format_kib(static_cast<long long>(comp.shared)));
          ly = kv_row(cr, dx, ly, lw, "Cache + buffers:", format_kib(static_cast<long long>(comp.cache_buffers)));
          ly = kv_row(cr, dx, ly, lw, "Available:", format_kib(static_cast<long long>(mem.available)));
          ly = kv_row(cr, dx, ly, lw, "Free:", format_kib(static_cast<long long>(comp.free)));
          ly = kv_row(cr, dx, ly, lw, "Swap in use:", mem.swap_total ? format_kib(static_cast<long long>(mem.swap_total - mem.swap_free)) + " of " + format_kib(static_cast<long long>(mem.swap_total)) : "no swap");
          const double rx = dx + half + 16;
          ry = kv_row(cr, rx, ry, lw, "Programs:", format_kib(static_cast<long long>(mem.anon)));
          ry = kv_row(cr, rx, ry, lw, "Kernel slab:", format_kib(static_cast<long long>(mem.sreclaimable + mem.sunreclaim)));
          ry = kv_row(cr, rx, ry, lw, "Mapped files:", format_kib(static_cast<long long>(mem.mapped)));
          ry = kv_row(cr, rx, ry, lw, "Dirty + writeback:", format_kib(static_cast<long long>(mem.dirty + mem.writeback)));
          ry = kv_row(cr, rx, ry, lw, "Committed:", mem.commit_limit ? format_kib(static_cast<long long>(mem.committed)) + " of " + format_kib(static_cast<long long>(mem.commit_limit)) : "--");
          ry = kv_row(cr, rx, ry, lw, "Memory pressure:", psi_mem.some10 >= 0 ? num1(psi_mem.some10, "%") : "--");
          y = std::max(ly, ry) + 8;
        }
        if (!memory_modules.empty() && y + 60 < h) {
          std::vector<std::pair<std::string, std::string>> module_info;
          for (const MemoryModule& m : memory_modules) {
            std::string label = m.locator.empty() ? "Module" : m.locator;
            label += " " + format_kib(m.size_mb * 1024);
            module_info.emplace_back(label, (m.manufacturer.empty() ? std::string() : m.manufacturer + " ") +
                                             (m.part_number.empty() ? std::string() : m.part_number) +
                                             (m.speed_mhz > 0 ? " " + std::to_string(m.speed_mhz) + " MT/s" : ""));
          }
          section_label(cr, dx, y, dw, "Installed modules");
          info_grid(cr, dx, y + 22, dw, module_info);
        }
        break;
      }
      case Res::Disk: {
        text_at(cr, "Disk", dx, y + 8, 18, pal.fg_primary, true);
        y += 26;
        graph(cr, {dx, y, dw, 170}, disk_read_h, disk_max, c1, true, &disk_write_h, &c2);
        y += 184;
        std::vector<std::pair<std::string, std::string>> drive_info{{"Read", format_rate(disk_read_bps)},
                                                                     {"Write", format_rate(disk_write_bps)}};
        for (const DriveInfo& d : drives) {
          std::string label = d.name + " " + format_kib(static_cast<long long>(d.size_bytes / 1024 / 1024));
          drive_info.emplace_back(label, d.model == "--" ? "drive" : d.model);
        }
        info_grid(cr, dx, y, dw, drive_info);
        y += (static_cast<double>((drive_info.size() + 1) / 2) * 38) + 8;
        text_at(cr, "Blue: read, orange: write. Whole disks only (partitions are not counted twice).", dx, y + 8, kSmall, dim());
        break;
      }
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
        if (!gpu.devices().empty()) text_right(cr, gpu.devices().front().vendor + " (" + gpu.devices().front().driver + ")", dx + dw, y + 8, kFont, dim());
        else if (gpu.nvidia_available()) text_right(cr, "NVIDIA", dx + dw, y + 8, kFont, dim());
        y += 28;
        if (!gpu.any()) {
          text_at(cr, "No GPU utilisation source found (software rendering or an unsupported driver).", dx, y + 8, kFont, dim());
          break;
        }
        text_at(cr, gpu.any_engine_busy() ? "% Render/3D engine busy" : "% Utilization", dx, y + 6, kSmall, dim());
        text_right(cr, "100%", dx + dw, y + 6, kSmall, dim());
        y += 14;
        graph(cr, {dx, y, dw, std::clamp(h - y - 270.0, 70.0, 150.0)}, gpu_h, 100, c1);
        y += std::clamp(h - y - 270.0, 70.0, 150.0) + 10;
        // One block per card: the figures a driver exposes; what it does not expose is said so instead of drawn as zero.
        auto gpu_block = [&](const std::string& name, const GpuDetail& d, bool uma, const GpuDevice* dev) {
          section_label(cr, dx, y, dw, name.c_str());
          y += 22;
          const double cw3 = dw / 3.0;
          auto cell = [&](int col, int row, const std::string& label, const std::string& value) { small_cell(cr, dx + col * cw3, y + row * 38.0, cw3 - 8, label, value); };
          const std::string util = d.percent >= 0 ? std::to_string(d.percent) + "%" : std::string();
          cell(0, 0, dev && dev->engine_busy ? "Render/3D busy" : "Utilization", util);
          cell(1, 0, "Core clock", d.core_mhz > 0 ? std::to_string(d.core_mhz) + " MHz" + (d.core_max_mhz > 0 ? " of " + std::to_string(d.core_max_mhz) : std::string()) : std::string());
          cell(2, 0, "Power", d.power_w >= 0 ? num1(d.power_w, " W") : std::string("not exposed"));
          if (uma) {
            cell(0, 1, "Memory", "shared with system RAM");
            cell(1, 1, "Memory clock", "that of system RAM");
            cell(2, 1, "Memory bandwidth", "not exposed by this GPU");
          } else {
            cell(0, 1, "Dedicated memory", d.vram_total > 0 ? format_kib(d.vram_used / 1024) + " of " + format_kib(d.vram_total / 1024) : std::string());
            cell(1, 1, "Memory clock", d.mem_mhz > 0 ? std::to_string(d.mem_mhz) + " MHz" + (d.mem_max_mhz > 0 ? " of " + std::to_string(d.mem_max_mhz) : std::string()) : std::string());
            cell(2, 1, "Memory controller busy", d.mem_busy_percent >= 0 ? std::to_string(d.mem_busy_percent) + "%" : std::string("not exposed"));
          }
          cell(0, 2, "PCIe link", dev && !dev->pcie_speed.empty() && dev->pcie_speed != "Unknown" ? dev->pcie_speed + (dev->pcie_width > 0 ? " x" + std::to_string(dev->pcie_width) : std::string()) : std::string("n/a"));
          y += 3 * 38 + 8;
        };
        int idx = 0;
        for (const GpuDevice& g : gpu.devices()) {
          gpu_block((gpu.devices().size() + static_cast<size_t>(gpu.nvidia_cards()) > 1 ? "GPU " + std::to_string(++idx) + ": " : std::string()) + g.vendor + " (" + g.driver + ")",
                    gpu.detail(g), g.kind == GpuDevice::Kind::IntelIdle, &g);
        }
        for (int i = 0; i < gpu.nvidia_cards() && static_cast<size_t>(i) < gpu.nvidia_detail().size(); ++i)
          gpu_block((gpu.devices().size() + static_cast<size_t>(gpu.nvidia_cards()) > 1 ? "GPU " + std::to_string(++idx) + ": " : std::string()) + "NVIDIA", gpu.nvidia_detail()[static_cast<size_t>(i)], false, nullptr);
        if (gpu.any_awake_proxy())
          text_at(cr, "This kernel does not let this user read the render-engine counter, so the figure is the GPU's awake time (RC6).", dx, y + 8, kSmall, dim());
        break;
      }
    }
    if (hovered_res >= 0) draw_resource_hover(cr, hovered_res);
  }

  void draw_resource_hover(cairo_t* cr, int index) {
    std::array<std::string, 3> details;
    const char* title = "";
    switch (static_cast<Res>(index)) {
      case Res::Cpu:
        title = "CPU DETAILS";
        details = {cpu_info.model.empty() ? "Processor model unavailable" : cpu_info.model,
                   std::to_string(cpu_info.logical_threads) + " logical processors / " +
                       std::to_string(cpu_info.physical_cores) + " physical cores",
                   "Clock: " + (cpu_info.mhz > 0 ? std::to_string(cpu_info.mhz) + " MHz" : "unavailable") +
                       "  |  Load: " + (load_avg.empty() ? "--" : load_avg)};
        break;
      case Res::Memory: {
        title = "MEMORY DETAILS";
        const unsigned long long used = mem.total > mem.available ? mem.total - mem.available : 0;
        details = {"In use: " + format_kib(static_cast<long long>(used)) + " / " +
                       format_kib(static_cast<long long>(mem.total)),
                   "Available: " + format_kib(static_cast<long long>(mem.available)),
                   "Cached: " + format_kib(static_cast<long long>(mem.cached)) + "  |  Swap: " +
                       (mem.swap_total ? format_kib(static_cast<long long>(mem.swap_total - mem.swap_free)) : "none")};
        break;
      }
      case Res::Disk:
        title = "DISK DETAILS";
        details = {"Read: " + format_rate(disk_read_bps),
                   "Write: " + format_rate(disk_write_bps),
                   "Physical drives: " + std::to_string(drives.size())};
        break;
      case Res::Network:
        title = "NETWORK DETAILS";
        details = {"Receive: " + format_rate(net_rx_bps),
                   "Send: " + format_rate(net_tx_bps),
                   "All active interfaces except loopback"};
        break;
      case Res::Gpu: {
        title = "GPU DETAILS";
        details[0] = gpu.text();
        const bool intel_awake_proxy = std::any_of(gpu.devices().begin(), gpu.devices().end(), [](const GpuDevice& device) {
          return device.kind == GpuDevice::Kind::IntelIdle;
        });
        if (gpu.devices().empty() && gpu.nvidia_cards() == 0) {
          details[1] = "Hardware utilisation source unavailable";
        } else if (gpu.devices().empty()) {
          details[1] = "NVIDIA (nvidia-smi)";
          const auto& nvidia = gpu.nvidia_detail();
          details[2] = !nvidia.empty() && nvidia.front().core_mhz > 0
                           ? "Clock: " + std::to_string(nvidia.front().core_mhz) + " MHz"
                           : "Clock: unavailable";
        } else {
          const GpuDevice& device = gpu.devices().front();
          details[1] = device.vendor + " (" + device.driver + ")";
          const long long freq = device.freq_path.empty() ? -1 : GpuMonitor::read_number(device.freq_path);
          details[2] = intel_awake_proxy
                           ? "GT awake estimate; not render-engine busy"
                           : freq >= 0 ? "Clock: " + std::to_string(freq) + " MHz" : "Clock: unavailable";
        }
        break;
      }
    }

    const Rect panel{8, kTabsH + 6.0 + 5 * 66 + 10, kSideW - 16.0, 90};
    rounded_rect(cr, panel.x + 0.5, panel.y + 0.5, panel.w - 1, panel.h - 1, pal.rounded ? 6 : 0);
    set_source(cr, alpha(pal.bg_primary, 0.9));
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.accent, 0.5));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    text_at(cr, title, panel.x + 9, panel.y + 14, kSmall, pal.accent, true);
    cairo_save(cr);
    cairo_rectangle(cr, panel.x + 8, panel.y + 22, panel.w - 16, panel.h - 26);
    cairo_clip(cr);
    for (size_t i = 0; i < details.size(); ++i)
      text_at(cr, details[i], panel.x + 9, panel.y + 36 + i * 16, kSmall, i == 0 ? pal.fg_primary : dim());
    cairo_restore(cr);
  }

  void draw_cache(cairo_t* cr, int w, int h) {
    const double dx = 24, dw = w - 48;
    double y = kTabsH + 24;
    text_at(cr, "CPU cache and bandwidth", dx, y, 20, pal.fg_primary, true);
    y += 34;
    text_at(cr, cpu_info.model.empty() ? "Unknown processor" : cpu_info.model, dx, y, kFont, dim());
    text_right(cr, cache_counter_status, dx + dw, y, kSmall, dim());
    y += 28;
    std::vector<std::pair<std::string, std::string>> entries;
    for (const CpuCacheInfo& c : cpu_caches) {
      std::string label = "L" + std::to_string(c.level) + " " + c.type;
      std::string value = c.size;
      if (c.line_bytes > 0) value += "  " + std::to_string(c.line_bytes) + " B line";
      if (c.ways > 0) value += "  " + std::to_string(c.ways) + " way";
      entries.emplace_back(label, value);
    }
    if (entries.empty()) entries.emplace_back("CPU cache", "topology unavailable");
    info_grid(cr, dx, y, dw, entries);
    y += static_cast<double>((entries.size() + 1) / 2) * 38 + 16;
    text_at(cr, "Memory throughput", dx, y, 16, pal.fg_primary, true);
    y += 24;
    std::vector<std::pair<std::string, std::string>> memory;
    for (const MemoryModule& m : memory_modules) {
      const std::string label = m.locator.empty() ? "Memory module" : m.locator;
      const double gbs = m.speed_mhz > 0 ? static_cast<double>(m.speed_mhz) * 8.0 / 1000.0 : 0;
      memory.emplace_back(label, m.speed_mhz > 0 ? std::to_string(m.speed_mhz) + " MT/s  " + format_bandwidth_gbs(gbs * 1e9) : "speed unavailable");
    }
    if (memory.empty()) memory.emplace_back("RAM", "module speed unavailable");
    info_grid(cr, dx, y, dw, memory);
    y += static_cast<double>((memory.size() + 1) / 2) * 38 + 16;
    text_at(cr, "Cache hit rate scopes", dx, y, 16, pal.fg_primary, true);
    y += 24;
    const std::string hit = cache_counters.hit_rate() >= 0 ? std::to_string(static_cast<int>(cache_counters.hit_rate() + 0.5)) + "% hit" : cache_counter_status;
    info_grid(cr, dx, y, dw, {{"Overall", hit},
                              {"Fleetwm apps", "requires perf access or instrumentation"},
                              {"Per module", "requires symbols or instrumentation"},
                              {"Per item", "requires explicit counters"}});
    y += 88;
    text_at(cr, "PCIe and GPU memory bandwidth", dx, y, 16, pal.fg_primary, true);
    y += 24;
    std::vector<std::pair<std::string, std::string>> gpus;
    for (const GpuDevice& g : gpu.devices()) {
      std::string value = g.pcie_speed.empty() ? "PCIe link unavailable" : g.pcie_speed + " x" + std::to_string(g.pcie_width);
      gpus.emplace_back(g.vendor, value);
    }
    for (const GpuDetail& g : gpu.nvidia_detail())
      gpus.emplace_back("NVIDIA memory", g.mem_mhz > 0 ? std::to_string(g.mem_mhz) + " MHz (bandwidth depends on bus width)" : "speed unavailable");
    if (gpus.empty()) gpus.emplace_back("GPU", "PCIe and memory link data unavailable");
    info_grid(cr, dx, y, dw, gpus);
    (void)h;
  }

  // ---------------------------------------------------------------- input --
  void select_by_index(long i) {
    if (table.rows().empty()) return;
    i = std::clamp(i, 0L, static_cast<long>(table.rows().size()) - 1);
    selected_pid = table.rows()[static_cast<size_t>(i)].info.pid;
    const double top = static_cast<double>(i) * kRowH;
    if (top < scroll) scroll = top;
    if (top + kRowH > scroll + list_height()) scroll = top + kRowH - list_height();
    clamp_scroll();
  }

  long selected_index() const {
    for (size_t i = 0; i < table.rows().size(); ++i)
      if (table.rows()[i].info.pid == selected_pid) return static_cast<long>(i);
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
    for (size_t i = 0; i < kTabNames.size(); ++i)
      if (tab_rect[i].hit(x, y) && tab != static_cast<Tab>(i)) {
        tab = static_cast<Tab>(i);
        hovered_res = -1;
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
    if (tab == Tab::Cache) return;
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
        sort_rows(&table.rows(), sort_col, sort_desc, users);
        surface->queue_draw();
        return;
      }
    if (y >= list_top() && y < list_top() + list_height()) {
      const size_t i = static_cast<size_t>((y - list_top() + scroll) / kRowH);
      if (i < table.rows().size()) {
        selected_pid = table.rows()[i].info.pid;
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

  void on_motion(double x, double y) {
    int next_tab = -1;
    for (size_t i = 0; i < kTabNames.size(); ++i)
      if (tab_rect[i].hit(x, y)) {
        next_tab = static_cast<int>(i);
        break;
      }

    int next_res = -1;
    if (tab == Tab::Performance)
      for (int i = 0; i < 5; ++i)
        if (res_rect[i].hit(x, y)) {
          next_res = i;
          break;
        }

    if (next_tab != hovered_tab || next_res != hovered_res) {
      hovered_tab = next_tab;
      hovered_res = next_res;
      surface->queue_draw();
    }
  }

  void on_leave() {
    if (hovered_tab >= 0 || hovered_res >= 0) {
      hovered_tab = -1;
      hovered_res = -1;
      surface->queue_draw();
    }
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    switch (ev.sym) {
      case XKB_KEY_Tab:
        tab = tab == Tab::Processes ? Tab::Performance : tab == Tab::Performance ? Tab::Cache : Tab::Processes;
        hovered_res = -1;
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
    if (std::strcmp(argv[i], "--tab") == 0) {
      T.tab = std::strcmp(argv[i + 1], "performance") == 0 ? Tab::Performance :
              std::strcmp(argv[i + 1], "cache") == 0 ? Tab::Cache : Tab::Processes;
    }
  T.gpu.discover("/sys/class/drm", true, true);  // the Task Manager may open the i915 render-busy counter: it is only open while this window lives
  T.read_inventory();
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
  T.surface->on_motion = [&T](double x, double y) { T.on_motion(x, y); };
  T.surface->on_leave = [&T] { T.on_leave(); };
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
