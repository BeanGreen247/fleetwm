#include "gpu_monitor.hpp"

#include <fcntl.h>
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
  for (GpuDevice& g : devices_)
    if (g.fd >= 0) close(g.fd);
}

long long GpuMonitor::read_number(const std::string& path) {
  long long v = -1;
  std::ifstream(path) >> v;
  return v;
}

void GpuMonitor::discover(const std::string& drm_root, bool check_nvidia) {
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
    if (!g.path.empty()) devices_.push_back(std::move(g));
  }
  has_nvidia_smi_ = false;
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
  if (has_nvidia_smi_ && run_in_background && post && ++tick_ >= 3 && !query_running_) {
    tick_ = 0;
    query_running_ = true;
    // nvidia-smi can take a while; keep it off the UI thread. It prints one line per card.
    run_in_background([this, post] {
      std::string out;
      if (FILE* p = popen("nvidia-smi --query-gpu=utilization.gpu,power.draw,clocks.gr,clocks.mem,memory.used,memory.total "
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
        nvidia_cards_ = static_cast<int>(pcts.size());
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

int GpuMonitor::busiest_percent() const {
  int best = -1;
  for (const GpuDevice& g : devices_) best = std::max(best, g.percent);
  for (int p : nvidia_percent_) best = std::max(best, p);
  return best;
}

}  // namespace fleetwm
