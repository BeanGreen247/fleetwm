#include "spectrum_source.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

#if FLEETWM_HAVE_PIPEWIRE
#include <spa/param/audio/format-utils.h>
#endif

namespace fleetwm::bar {

#if FLEETWM_HAVE_PIPEWIRE

namespace {
long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

SpectrumSource::SpectrumSource() {
  pw_init(nullptr, nullptr);
  loop_ = pw_thread_loop_new("fleetwm-bar-spectrum", nullptr);
  if (loop_ && pw_thread_loop_start(loop_) < 0) {
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
  }
}

SpectrumSource::~SpectrumSource() {
  stop();
  if (loop_) {
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
  }
}

bool SpectrumSource::available() const { return loop_ != nullptr; }

void SpectrumSource::start() {
  if (!loop_ || running_) return;
  pw_thread_loop_lock(loop_);
  pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                           PW_KEY_MEDIA_ROLE, "Music", PW_KEY_STREAM_CAPTURE_SINK, "true",
                                           PW_KEY_NODE_NAME, "fleetwm-bar-spectrum",
                                           PW_KEY_NODE_LATENCY, "1024/48000", nullptr);
  static const pw_stream_events events = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = &SpectrumSource::on_process;
    return e;
  }();
  stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "fleetwm-bar-spectrum", props, &events, this);
  if (stream_) {
    uint8_t buffer[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = 48000;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
    const int rc = pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                                     static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                                  PW_STREAM_FLAG_RT_PROCESS),
                                     params, 1);
    if (rc < 0) {
      pw_stream_destroy(stream_);
      stream_ = nullptr;
    }
  }
  running_ = stream_ != nullptr;
  pw_thread_loop_unlock(loop_);
}

void SpectrumSource::stop() {
  if (!loop_ || !running_) return;
  pw_thread_loop_lock(loop_);
  pw_stream_destroy(stream_);
  stream_ = nullptr;
  pw_thread_loop_unlock(loop_);
  running_ = false;
  last_audio_ns_ = 0;
  analyzer_.decay();
}

void SpectrumSource::on_process(void* data) {
  auto* self = static_cast<SpectrumSource*>(data);
  pw_buffer* buf = pw_stream_dequeue_buffer(self->stream_);
  if (!buf) return;
  spa_data& d = buf->buffer->datas[0];
  if (d.data && d.chunk->size > 0) {
    const auto* s = static_cast<const float*>(d.data) + d.chunk->offset / sizeof(float);
    const size_t frames = d.chunk->size / (sizeof(float) * 2);
    float mono[2048];
    for (size_t i = 0; i < frames;) {
      const size_t n = std::min<size_t>(frames - i, 2048);
      for (size_t k = 0; k < n; ++k) mono[k] = 0.5f * (s[(i + k) * 2] + s[(i + k) * 2 + 1]);
      self->analyzer_.feed(mono, n);
      i += n;
    }
    self->last_audio_ns_.store(now_ns(), std::memory_order_relaxed);
  }
  pw_stream_queue_buffer(self->stream_, buf);
}

bool SpectrumSource::levels(float* out) {
  if (!running_) return false;
  const long long last = last_audio_ns_.load(std::memory_order_relaxed);
  if (last == 0 || now_ns() - last > 300'000'000) {  // the stream went quiet: let the bars fall
    analyzer_.decay();
    analyzer_.levels(out);
    return last != 0;
  }
  analyzer_.levels(out);
  return true;
}

#else

SpectrumSource::SpectrumSource() = default;
SpectrumSource::~SpectrumSource() = default;
void SpectrumSource::start() {}
void SpectrumSource::stop() {}
bool SpectrumSource::available() const { return false; }
bool SpectrumSource::levels(float*) { return false; }

#endif

}  // namespace fleetwm::bar
