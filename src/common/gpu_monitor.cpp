#include "gpu_monitor.hpp"

#include <fcntl.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fleetwm {

namespace {
std::string read_word(const std::string& path) {
  std::string w;
  std::ifstream(path) >> w;
  return w;
}
}  // namespace

GpuMonitor::~GpuMonitor() {
  for (GpuDevice& g : devices_) {
    if (g.fd >= 0) close(g.fd);
    if (g.pmu_fd >= 0) close(g.pmu_fd);
  }
}

bool GpuMonitor::parse_pmu_config(const std::string& text, unsigned long long* config) {
  const size_t at = text.find("config=");
  if (at == std::string::npos) return false;
  char* end = nullptr;
  const unsigned long long v = std::strtoull(text.c_str() + at + 7, &end, 0);
  if (end == text.c_str() + at + 7) return false;
  *config = v;
  return true;
}

namespace {
// perf_event_open on the i915 PMU's rcs0-busy event (nanoseconds the render engine was busy), as a system-wide counter on cpu 0 (how the driver wants it).
int open_i915_render_busy() {
  const std::string root = "/sys/bus/event_source/devices/i915";
  std::ifstream type_file(root + "/type");
  int type = -1;
  type_file >> type;
  if (type < 0) return -1;
  std::ifstream ev(root + "/events/rcs0-busy");
  std::string text;
  std::getline(ev, text);
  unsigned long long config = 0;
  if (!GpuMonitor::parse_pmu_config(text, &config)) return -1;
  perf_event_attr attr{};
  attr.type = static_cast<uint32_t>(type);
  attr.size = sizeof attr;
  attr.config = config;
  return static_cast<int>(syscall(SYS_perf_event_open, &attr, -1, 0, -1, PERF_FLAG_FD_CLOEXEC));
}
}  // namespace

long long GpuMonitor::read_number(const std::string& path) {
  long long v = -1;
  std::ifstream(path) >> v;
  return v;
}

void GpuMonitor::discover(const std::string& drm_root, bool check_nvidia, bool use_pmu) {
  devices_.clear();
  for (int card = 0; card < 8; ++card) {
    const std::string base = drm_root + "/card" + std::to_string(card);
    const std::string vendor_id = read_word(base + "/device/vendor");
    if (vendor_id.empty()) continue;
    std::error_code ec;
    const std::string driver = std::filesystem::read_symlink(base + "/device/driver", ec).filename().string();
    GpuDevice g;
    g.driver = driver;
    g.device_dir = base + "/device";
    {
      std::ifstream speed(base + "/device/current_link_speed");
      std::getline(speed, g.pcie_speed);
      std::ifstream width(base + "/device/current_link_width");
      width >> g.pcie_width;
    }
    if (vendor_id == "0x1002") g.vendor = "AMD";
    else if (vendor_id == "0x8086") g.vendor = "Intel";
    else if (vendor_id == "0x10de") g.vendor = "NVIDIA";
    else g.vendor = vendor_id;
    if (std::ifstream(base + "/device/gpu_busy_percent").good()) {
      g.kind = GpuDevice::Kind::AmdBusy;
      g.path = base + "/device/gpu_busy_percent";
    } else if (vendor_id == "0x8086") {
      for (const char* rel : {"/gt/gt0/rc6_residency_ms", "/power/rc6_residency_ms", "/device/tile0/gt0/gtidle/idle_residency_ms"})
        if (std::ifstream(base + rel).good()) {
          g.kind = GpuDevice::Kind::IntelIdle;
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
    if (!g.path.empty() && use_pmu && g.kind == GpuDevice::Kind::IntelIdle && vendor_id == "0x8086" && drm_root == "/sys/class/drm") {
      g.pmu_fd = open_i915_render_busy();
      g.engine_busy = g.pmu_fd >= 0;
    }
    if (!g.path.empty()) devices_.push_back(std::move(g));
  }
  has_nvidia_smi_ = false;
  if (check_nvidia && drm_root == "/sys/class/drm") discover_nvidia_pci();  // the cards are listed now, a sleeping one is never woken to be counted
  if (check_nvidia)
    if (const char* path = std::getenv("PATH")) {
      std::string p = path;
      size_t pos = 0;
      while (pos <= p.size()) {
        size_t e = p.find(':', pos);
        if (e == std::string::npos) e = p.size();
        const std::string cand = p.substr(pos, e - pos) + "/nvidia-smi";
        if (access(cand.c_str(), X_OK) == 0) {
          has_nvidia_smi_ = true;
          break;
        }
        pos = e + 1;
      }
    }
}

void GpuMonitor::discover_nvidia_pci(const std::string& pci_root, const std::string& proc_root) {
  namespace fs = std::filesystem;
  nvidia_pci_.clear();
  std::error_code ec;
  std::vector<fs::path> dirs;
  for (const fs::directory_entry& e : fs::directory_iterator(pci_root, ec)) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());
  for (const fs::path& d : dirs) {
    std::string vendor, cls, status;
    std::ifstream(d / "vendor") >> vendor;
    std::ifstream(d / "class") >> cls;
    if (vendor != "0x10de" || cls.rfind("0x03", 0) != 0) continue;
    NvidiaPci c;
    c.address = d.filename().string();
    std::ifstream(d / "power/runtime_status") >> status;
    c.suspended = status == "suspended";
    c.model = "NVIDIA";
    std::ifstream info(fs::path(proc_root) / c.address / "information");  // readable while the card sleeps
    std::string line;
    while (std::getline(info, line))
      if (line.rfind("Model:", 0) == 0) {
        size_t b = line.find_first_not_of(" \t", 6);
        if (b != std::string::npos) c.model = line.substr(b);
        break;
      }
    nvidia_pci_.push_back(c);
  }
  nvidia_cards_ = static_cast<int>(nvidia_pci_.size());
  nvidia_percent_.assign(nvidia_pci_.size(), -1);
  nvidia_detail_.assign(nvidia_pci_.size(), GpuDetail{});
  for (size_t i = 0; i < nvidia_pci_.size(); ++i)
    if (nvidia_pci_[i].suspended) {
      nvidia_percent_[i] = 0;
      nvidia_detail_[i].percent = 0;
      nvidia_detail_[i].asleep = true;
    }
}

bool GpuMonitor::nvidia_all_suspended(const std::string& pci_root) {
  namespace fs = std::filesystem;
  bool any = false;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(pci_root, ec)) {
    std::string vendor, cls, status;
    std::ifstream(e.path() / "vendor") >> vendor;
    std::ifstream(e.path() / "class") >> cls;
    if (vendor != "0x10de" || cls.rfind("0x03", 0) != 0) continue;  // NVIDIA, display controller
    std::ifstream(e.path() / "power/runtime_status") >> status;
    if (status != "suspended") return false;
    any = true;
  }
  return any;
}

bool GpuMonitor::sample(const std::function<void(std::function<void()>)>& run_in_background,
                        const std::function<void(std::function<void()>)>& post) {
  bool changed = false;
  for (GpuDevice& g : devices_) {
    int pct = g.percent;
    if (g.kind == GpuDevice::Kind::AmdBusy) {
      if (g.fd < 0) g.fd = open(g.path.c_str(), O_RDONLY | O_CLOEXEC);
      if (g.fd < 0) continue;
      char buf[32];
      const ssize_t got = pread(g.fd, buf, sizeof buf - 1, 0);
      if (got <= 0) continue;
      buf[got] = 0;
      pct = std::clamp(std::atoi(buf), 0, 100);
    } else if (g.engine_busy) {
      unsigned long long busy_ns = 0;
      if (read(g.pmu_fd, &busy_ns, sizeof busy_ns) != static_cast<ssize_t>(sizeof busy_ns)) continue;
      const auto now = std::chrono::steady_clock::now();
      if (g.pmu_have_prev) {
        const double wall_ns = std::chrono::duration<double, std::nano>(now - g.pmu_time).count();
        if (wall_ns < 50e6) continue;
        const double busy = busy_ns >= g.pmu_prev_ns ? static_cast<double>(busy_ns - g.pmu_prev_ns) : 0.0;
        pct = static_cast<int>(100.0 * std::clamp(busy / wall_ns, 0.0, 1.0) + 0.5);
      }
      g.pmu_prev_ns = busy_ns;
      g.pmu_have_prev = true;
      g.pmu_time = now;
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
  // A dedicated NVIDIA card in runtime suspend stays asleep: nvidia-smi would wake it (and keep a hybrid laptop's battery and fans busy) just to report 0 %.
  if (has_nvidia_smi_ && run_in_background && post && ++tick_ >= 3 && !query_running_ && nvidia_all_suspended()) {
    tick_ = 0;
    discover_nvidia_pci(pci_root_);  // still asleep: keep the cards listed, as asleep
  } else if (has_nvidia_smi_ && run_in_background && post && tick_ >= 3 && !query_running_) {
    tick_ = 0;
    query_running_ = true;
    // nvidia-smi can take a while; keep it off the UI thread. It prints one line per card.
    run_in_background([this, post] {
      std::string out;
      if (FILE* p = popen("nvidia-smi --query-gpu=utilization.gpu,power.draw,clocks.gr,clocks.mem,memory.used,memory.total,utilization.memory,clocks.max.gr,clocks.max.mem "
                          "--format=csv,noheader,nounits 2>/dev/null", "r")) {
        char buf[512];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        pclose(p);
      }
      std::vector<GpuDetail> cards = parse_nvidia_smi_csv(out);
      std::vector<int> pcts;
      for (const auto& c : cards) pcts.push_back(c.percent);
      post([this, pcts, cards = std::move(cards)] {
        query_running_ = false;
        const std::string before = text();
        nvidia_percent_ = pcts;
        nvidia_detail_ = cards;
        for (GpuDetail& d : nvidia_detail_) d.asleep = false;
        nvidia_cards_ = static_cast<int>(std::max(pcts.size(), nvidia_pci_.size()));
        for (size_t i = 0; i < nvidia_pci_.size(); ++i) nvidia_pci_[i].suspended = false;
        while (nvidia_percent_.size() < static_cast<size_t>(nvidia_cards_)) nvidia_percent_.push_back(-1);
        while (nvidia_detail_.size() < static_cast<size_t>(nvidia_cards_)) nvidia_detail_.emplace_back();
        if (text() != before && on_nvidia_update) on_nvidia_update();
      });
    });
  }
  return changed;
}

std::string GpuMonitor::text() const {
  std::vector<int> values;
  for (const GpuDevice& g : devices_) values.push_back(g.percent);
  for (int i = 0; i < nvidia_cards_; ++i) values.push_back(nvidia_percent_.size() > static_cast<size_t>(i) ? nvidia_percent_[static_cast<size_t>(i)] : -1);
  if (values.empty()) return "GPU N/A";
  auto one = [](int v) { return v < 0 ? std::string("--%") : std::to_string(v) + "%"; };
  if (values.size() == 1) return "GPU " + one(values[0]);
  std::string t;
  for (size_t i = 0; i < values.size(); ++i) t += (i ? "  GPU" : "GPU") + std::to_string(i + 1) + " " + one(values[i]);
  return t;
}

bool GpuMonitor::any_engine_busy() const {
  for (const GpuDevice& g : devices_)
    if (g.engine_busy) return true;
  return false;
}

bool GpuMonitor::any_awake_proxy() const {
  for (const GpuDevice& g : devices_)
    if (g.kind == GpuDevice::Kind::IntelIdle && !g.engine_busy) return true;
  return false;
}

GpuDetail GpuMonitor::detail(const GpuDevice& g) const {
  GpuDetail d;
  if (g.kind == GpuDevice::Kind::AmdBusy) {
    d = read_amd_gpu_detail(g.device_dir);
  } else {
    const long long cur = g.freq_path.empty() ? -1 : read_number(g.freq_path);
    const long long max = g.freq_max_path.empty() ? -1 : read_number(g.freq_max_path);
    if (cur >= 0) d.core_mhz = static_cast<int>(cur);
    if (max > 0) d.core_max_mhz = static_cast<int>(max);
  }
  d.percent = g.percent;
  return d;
}

std::vector<int> GpuMonitor::percents() const {
  std::vector<int> out;
  for (const GpuDevice& g : devices_) out.push_back(g.percent);
  for (int i = 0; i < nvidia_cards_; ++i) out.push_back(nvidia_percent_.size() > static_cast<size_t>(i) ? nvidia_percent_[static_cast<size_t>(i)] : -1);
  return out;
}

std::vector<std::string> GpuMonitor::names() const {
  std::vector<std::string> out;
  for (const GpuDevice& g : devices_) out.push_back(g.vendor + " (" + g.driver + ")");
  for (int i = 0; i < nvidia_cards_; ++i) {
    std::string n = static_cast<size_t>(i) < nvidia_pci_.size() ? nvidia_pci_[static_cast<size_t>(i)].model : std::string("NVIDIA");
    if (static_cast<size_t>(i) < nvidia_detail_.size() && nvidia_detail_[static_cast<size_t>(i)].asleep) n += " (asleep)";
    out.push_back(n);
  }
  return out;
}

int GpuMonitor::busiest_percent() const {
  int best = -1;
  for (const GpuDevice& g : devices_) best = std::max(best, g.percent);
  for (int p : nvidia_percent_) best = std::max(best, p);
  return best;
}

}  // namespace fleetwm
