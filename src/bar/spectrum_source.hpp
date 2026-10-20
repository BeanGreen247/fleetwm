#pragma once

#include <memory>

#include <atomic>

#include "spectrum.hpp"

#if FLEETWM_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#endif

namespace fleetwm::bar {

// Listens to what the speakers play (a capture stream on the default output's monitor) and
// turns it into band levels for the media waveform. Only listens between start() and stop(),
// so nothing is captured while the waveform is not on screen. Without PipeWire, available()
// is false and the bar falls back to its clock-driven animation.
class SpectrumSource {
 public:
  static constexpr int kBands = 32;

  SpectrumSource();
  ~SpectrumSource();
  SpectrumSource(const SpectrumSource&) = delete;
  SpectrumSource& operator=(const SpectrumSource&) = delete;

  void start();
  void stop();
  bool available() const;
  // True while audio is arriving; fills kBands levels (0..1).
  bool levels(float* out);

 private:
#if FLEETWM_HAVE_PIPEWIRE
  static void on_process(void* data);
  static void on_state_changed(void* data, pw_stream_state old_state, pw_stream_state state, const char* error);

  pw_thread_loop* loop_ = nullptr;
  pw_stream* stream_ = nullptr;
  spa_hook stream_listener_{};
  bool running_ = false;
  std::atomic<long long> last_audio_ns_{0};
#endif
  fleetwm::SpectrumAnalyzer analyzer_{kBands};
};

}  // namespace fleetwm::bar
