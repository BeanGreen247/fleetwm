#pragma once

// GPU utilisation for every GPU in the machine, shared by the bar and the Task Manager so their numbers never disagree.
// AMD has a "busy percent" file. Intel has none, so its idle time (RC6 residency) over wall-clock time gives the share of time it
// was awake. NVIDIA is asked through nvidia-smi in the background (one line per card). `drm_root` exists so tests can point
// discovery at a fake /sys/class/drm tree.

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "hw_stats.hpp"

namespace fleetwm {

struct GpuDevice {
  enum class Kind { AmdBusy, IntelIdle } kind = Kind::AmdBusy;
  std::string vendor, driver;  // "AMD", "amdgpu"
  std::string path;            // the busy or idle-residency file
  std::string device_dir;      // /sys/class/drm/cardN/device
  std::string freq_path, freq_max_path;
  std::string pcie_speed;
  int pcie_width = 0;
  int fd = -1;
  long long idle_prev_ms = -1;
  std::chrono::steady_clock::time_point idle_prev_time;
  int percent = -1;
  // Intel with the i915 performance counters open: `percent` is the render engine's busy time (what intel_gpu_top calls Render/3D busy)
  // instead of the time the GPU was awake (RC6 residency), which counts awake-but-idle time as work.
  bool engine_busy = false;
  int pmu_fd = -1;
  unsigned long long pmu_prev_ns = 0;
  bool pmu_have_prev = false;
  std::chrono::steady_clock::time_point pmu_time;
};

class GpuMonitor {
 public:
  GpuMonitor() = default;
  ~GpuMonitor();
  GpuMonitor(const GpuMonitor&) = delete;
  GpuMonitor& operator=(const GpuMonitor&) = delete;

  // Finds the cards. `check_nvidia` also looks for nvidia-smi on PATH.
  // `use_pmu` opens the i915 render-engine busy counter for Intel cards when the kernel lets this user (perf_event_paranoid, CAP_PERFMON); it is
  // off by default because the driver does extra bookkeeping while any such counter is open, which the bar should not cause just to draw a number.
  void discover(const std::string& drm_root = "/sys/class/drm", bool check_nvidia = true, bool use_pmu = false);

  // Reads the AMD and Intel counters once; true when a percentage changed. NVIDIA is asked every third call: `run_in_background` is
  // given a function to run off the UI thread and returns once it has been handed over; `post` runs a function back on the UI thread.
  bool sample(const std::function<void(std::function<void()>)>& run_in_background = nullptr,
              const std::function<void(std::function<void()>)>& post = nullptr);

  // Called on the UI thread when an nvidia-smi answer arrived and the text changed.
  std::function<void()> on_nvidia_update;

  const std::vector<GpuDevice>& devices() const { return devices_; }
  int nvidia_cards() const { return nvidia_cards_; }
  bool nvidia_available() const { return has_nvidia_smi_ || nvidia_cards_ > 0; }
  const std::vector<GpuDetail>& nvidia_detail() const { return nvidia_detail_; }
  bool any() const { return !devices_.empty() || nvidia_available(); }

  // "GPU 12%" for one GPU, "GPU1 12%  GPU2 40%" for several, "GPU N/A" for none.
  std::string text() const;
  // The busiest GPU's percentage, -1 when none reports one.
  int busiest_percent() const;

  static long long read_number(const std::string& path);
  // True when every NVIDIA display card in `pci_root` (/sys/bus/pci/devices) is in runtime suspend (D3): asking nvidia-smi would wake it, so the caller
  // leaves it alone. False when there is no NVIDIA card or any one is awake.
  static bool nvidia_all_suspended(const std::string& pci_root = "/sys/bus/pci/devices");
  // "config=0x0000000000000001" (the content of an events/ file under /sys/bus/event_source/devices/i915): the config value; false when malformed.
  static bool parse_pmu_config(const std::string& text, unsigned long long* config);
  // Clocks, power and memory of one card, from the files that card has (AMD: hwmon and pp_dpm; Intel: the actual and maximum GT frequency).
  GpuDetail detail(const GpuDevice& g) const;
  bool any_engine_busy() const;  // some card reports real render-engine busy time
  bool any_awake_proxy() const;  // some card only reports the RC6-derived awake time

 private:
  std::vector<GpuDevice> devices_;
  std::vector<int> nvidia_percent_;
  std::vector<GpuDetail> nvidia_detail_;
  int nvidia_cards_ = 0, tick_ = 0;
  bool query_running_ = false, has_nvidia_smi_ = false;
};

}  // namespace fleetwm
