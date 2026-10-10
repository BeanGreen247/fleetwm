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
};

class GpuMonitor {
 public:
  GpuMonitor() = default;
  ~GpuMonitor();
  GpuMonitor(const GpuMonitor&) = delete;
  GpuMonitor& operator=(const GpuMonitor&) = delete;

  // Finds the cards. `check_nvidia` also looks for nvidia-smi on PATH.
  void discover(const std::string& drm_root = "/sys/class/drm", bool check_nvidia = true);

  // Reads the AMD and Intel counters once; true when a percentage changed. NVIDIA is asked every third call: `run_in_background` is
  // given a function to run off the UI thread and returns once it has been handed over; `post` runs a function back on the UI thread.
  bool sample(const std::function<void(std::function<void()>)>& run_in_background = nullptr,
              const std::function<void(std::function<void()>)>& post = nullptr);

  // Called on the UI thread when an nvidia-smi answer arrived and the text changed.
  std::function<void()> on_nvidia_update;

  const std::vector<GpuDevice>& devices() const { return devices_; }
  int nvidia_cards() const { return nvidia_cards_; }
  const std::vector<GpuDetail>& nvidia_detail() const { return nvidia_detail_; }
  bool any() const { return !devices_.empty() || has_nvidia_smi_; }

  // "GPU 12%" for one GPU, "GPU1 12%  GPU2 40%" for several, "GPU N/A" for none.
  std::string text() const;
  // The busiest GPU's percentage, -1 when none reports one.
  int busiest_percent() const;

  static long long read_number(const std::string& path);

 private:
  std::vector<GpuDevice> devices_;
  std::vector<int> nvidia_percent_;
  std::vector<GpuDetail> nvidia_detail_;
  int nvidia_cards_ = 0, tick_ = 0;
  bool query_running_ = false, has_nvidia_smi_ = false;
};

}  // namespace fleetwm
