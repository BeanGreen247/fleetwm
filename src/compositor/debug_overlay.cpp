#include "debug_overlay.hpp"

#include <cairo.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

extern "C" {
#include <wlr/render/gles2.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_output_layout.h>
}

#include "fleetkit.hpp"
#include "output.hpp"
#include "pixel_buffer.hpp"
#include "server.hpp"

namespace fleetwm {

namespace {

constexpr double kOvIdleGapMs = 250;
constexpr int kOvPad = 8, kOvGraphH = 44, kOvLineH = 15, kOvBarW = 3, kOvBarGap = 0, kOvMargin = 8, kOvFont = 11;

double ov_ms_of(const timespec& t) { return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

const char* ov_renderer_name(wlr_renderer* r) {
  if (wlr_renderer_is_pixman(r)) return "PIXMAN";
  if (wlr_renderer_is_gles2(r)) return "GLES2";
  return "RENDER";
}

// Current (not peak) resident memory of this process, in MB, from statm: two numbers, no parsing of
// the long status file.
int ov_read_rss_mb() {
  std::FILE* f = std::fopen("/proc/self/statm", "r");
  if (!f) return -1;
  unsigned long size = 0, rss = 0;
  const int got = std::fscanf(f, "%lu %lu", &size, &rss);
  std::fclose(f);
  return got == 2 ? static_cast<int>(rss * static_cast<unsigned long>(sysconf(_SC_PAGESIZE)) / (1024 * 1024)) : -1;
}

// CPU time this process has used so far, in microseconds: one syscall, nothing to parse.
double ov_cpu_used_us() {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
  return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1e6 + ru.ru_utime.tv_usec + ru.ru_stime.tv_usec;
}

int ov_read_cpu_mhz() {
  std::ifstream freq("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
  long khz = 0;
  return (freq >> khz) ? static_cast<int>(khz / 1000) : -1;
}

}  // namespace

DebugOverlay::DebugOverlay(Server* server, Output* output) : server_(server), output_(output) {
  node_ = wlr_scene_buffer_create(server_->layer_debug(), nullptr);
}

DebugOverlay::~DebugOverlay() {
  if (idle_timer_) wl_event_source_remove(idle_timer_);
  if (node_) wlr_scene_node_destroy(&node_->node);  // parented to the server's debug layer, not to the output
}

double DebugOverlay::budget_ms() const {
  const int mhz = output_->wlr_output_ptr->refresh;  // mHz
  return mhz > 1000 ? 1.0e6 / mhz : 16.67;
}

void DebugOverlay::frame_begin(const timespec& now) {
  const double t = ov_ms_of(now);
  if (idle_shown_) {
    // The frame that showed "0 FPS" is ours; counting it would make an idle desktop look busy.
    idle_shown_ = false;
    last_frame_ms_ = -1;
    return;
  }
  if (last_frame_ms_ >= 0) {
    const double delta = t - last_frame_ms_;
    deltas_[next_] = static_cast<float>(delta);
    next_ = (next_ + 1) % kSamples;
    if (delta < kOvIdleGapMs) {  // a longer gap is the desktop waking up, not a slow frame
      ++win_.frames;
      win_.delta_sum += delta;
      win_.delta_max = std::max(win_.delta_max, delta);
      if (delta > 1.5 * budget_ms()) ++win_.late;
      if (win_.frames == 1) win_.first_ms = t;
    }
  }
  last_frame_ms_ = t;
  win_.last_ms = t;
  if (last_repaint_ms_ < 0 || t - last_repaint_ms_ >= kRefreshMs) repaint(t);
}

void DebugOverlay::frame_end(double cost_ms) {
  win_.cost_sum += cost_ms;
  win_.cost_max = std::max(win_.cost_max, cost_ms);
}

int DebugOverlay::idle_fired(void* data) {
  auto* self = static_cast<DebugOverlay*>(data);
  if (!self->server_->debug_overlay_enabled()) return 0;  // hidden meanwhile: nothing to show
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  self->win_ = Window{};  // nothing happened since the last repaint
  self->idle_shown_ = true;
  self->repaint(ov_ms_of(now));
  return 0;
}

void DebugOverlay::arm_idle_timer() {
  if (!idle_timer_)
    idle_timer_ = wl_event_loop_add_timer(wl_display_get_event_loop(server_->display()), idle_fired, this);
  if (idle_timer_) wl_event_source_timer_update(idle_timer_, 3 * kRefreshMs);
}

void DebugOverlay::repaint(double now_ms) {
  last_repaint_ms_ = now_ms;

  // Numbers for the picture.
  const Window w = win_;
  win_ = Window{};
  int fps = 0;
  if (w.frames >= 2 && w.last_ms > w.first_ms) fps = static_cast<int>((w.frames - 1) * 1000.0 / (w.last_ms - w.first_ms) + 0.5);
  const double avg_delta = w.frames ? w.delta_sum / w.frames : 0;
  const double avg_cost = w.frames ? w.cost_sum / w.frames : 0;
  const double cpu_us = ov_cpu_used_us();
  if (prev_cpu_us_ > 0 && now_ms > prev_cpu_ms_)
    cpu_pct_ = static_cast<int>(100.0 * (cpu_us - prev_cpu_us_) / 1000.0 / (now_ms - prev_cpu_ms_) + 0.5);
  prev_cpu_us_ = cpu_us;
  prev_cpu_ms_ = now_ms;
  if (slow_age_-- <= 0) {  // memory and clock speed move slowly: once a second is plenty
    mem_mb_ = ov_read_rss_mb();
    mhz_ = ov_read_cpu_mhz();
    slow_age_ = 1000 / kRefreshMs - 1;
  }

  // The picture. The panel behind everything never changes, so it is drawn once and copied in.
  const int graph_w = kSamples * (kOvBarW + kOvBarGap);
  const int W = graph_w + 2 * kOvPad, H = kOvPad + 4 * kOvLineH + 4 + kOvGraphH + kOvPad;
  const double budget = budget_ms();
  const double gy = H - kOvPad - kOvGraphH;
  void* pixels = nullptr;
  size_t stride = 0;
  wlr_buffer* buf = create_pixel_buffer(W, H, &pixels, &stride);
  if (!buf) return;
  if (background_.empty()) {
    cairo_surface_t* bs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t* bc = cairo_create(bs);
    kit::rounded_rect(bc, 0, 0, W, H, 8);
    kit::set_source(bc, {0.05, 0.05, 0.08, 0.72});
    cairo_fill(bc);
    kit::set_source(bc, {1, 1, 1, 0.25});  // one frame budget
    cairo_rectangle(bc, kOvPad, gy + kOvGraphH - kOvGraphH / 3.0, graph_w, 1);
    cairo_fill(bc);
    cairo_destroy(bc);
    cairo_surface_flush(bs);
    background_.assign(static_cast<size_t>(stride) * H, 0);
    for (int row = 0; row < H; ++row)
      std::memcpy(background_.data() + static_cast<size_t>(row) * stride,
                  cairo_image_surface_get_data(bs) + static_cast<size_t>(row) * cairo_image_surface_get_stride(bs),
                  static_cast<size_t>(W) * 4);
    cairo_surface_destroy(bs);
  }
  std::memcpy(pixels, background_.data(), background_.size());
  cairo_surface_t* cs = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, W,
                                                            H, static_cast<int>(stride));
  cairo_t* cr = cairo_create(cs);

  const kit::Color text{0.88, 0.89, 0.95, 0.95}, dim{0.62, 0.64, 0.72, 0.95};
  const bool ok = avg_cost < budget * 0.6 && w.late == 0;
  char line[96];
  double y = kOvPad + kOvLineH - 4;
  std::snprintf(line, sizeof line, "%d FPS  %.1f ms/frame", fps, avg_delta);
  kit::draw_text(cr, line, kOvPad, y, kOvFont + 1, fps == 0 ? dim : text, true);
  y += kOvLineH;
  std::snprintf(line, sizeof line, "compositor %.1f ms avg, %.1f max  (%.1f budget)", avg_cost, w.cost_max, budget);
  kit::draw_text(cr, line, kOvPad, y, kOvFont, ok ? kit::Color{0.45, 0.9, 0.45, 1} : kit::Color{0.95, 0.75, 0.3, 1});
  y += kOvLineH;
  std::snprintf(line, sizeof line, "late %d   CPU %d%%   MEM %d MB", w.late, cpu_pct_, mem_mb_);
  kit::draw_text(cr, line, kOvPad, y, kOvFont, text);
  y += kOvLineH;
  if (mhz_ >= 0) std::snprintf(line, sizeof line, "%s   %d MHz   %dx%d", ov_renderer_name(server_->renderer()), mhz_,
                               output_->wlr_output_ptr->width, output_->wlr_output_ptr->height);
  else std::snprintf(line, sizeof line, "%s   %dx%d", ov_renderer_name(server_->renderer()), output_->wlr_output_ptr->width,
                     output_->wlr_output_ptr->height);
  kit::draw_text(cr, line, kOvPad, y, kOvFont, dim);

  // Graph, written straight into the pixels (premultiplied ARGB, alpha 0.94): oldest on the left, full
  // height is three frame budgets. Bars are plain columns, so no path or fill is needed for them.
  constexpr uint32_t kGreen = 0xF0 << 24 | 0x2F << 16 | 0xCC << 8 | 0x2F, kYellow = 0xF0 << 24 | 0xD0 << 16 | 0xC8 << 8 | 0x18,
                     kRed = 0xF0 << 24 | 0xD0 << 16 | 0x24 << 8 | 0x24;
  for (int i = 0; i < kSamples; ++i) {
    const float ms = deltas_[(next_ + i) % kSamples];
    if (ms <= 0.0f || ms >= kOvIdleGapMs) continue;
    const int h = std::max(1, static_cast<int>(std::clamp(ms / (3.0 * budget), 0.04, 1.0) * kOvGraphH));
    const uint32_t c = ms <= 1.15 * budget ? kGreen : ms <= 2.05 * budget ? kYellow : kRed;
    for (int row = static_cast<int>(gy) + kOvGraphH - h; row < static_cast<int>(gy) + kOvGraphH; ++row) {
      auto* px = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(pixels) + static_cast<size_t>(row) * stride) + kOvPad + i * (kOvBarW + kOvBarGap);
      for (int x = 0; x < kOvBarW; ++x) px[x] = c;
    }
  }

  cairo_destroy(cr);
  cairo_surface_flush(cs);
  cairo_surface_destroy(cs);

  wlr_scene_buffer_set_buffer(node_, buf);
  wlr_buffer_drop(buf);
  // Bottom-right corner of the area no bar or taskbar reserves (layout coordinates).
  wlr_box box{};
  wlr_output_layout_get_box(server_->output_layout(), output_->wlr_output_ptr, &box);
  const wlr_box area = output_->usable_area.width > 0 ? output_->usable_area : box;
  wlr_scene_node_set_position(&node_->node, area.x + area.width - kOvMargin - W, area.y + area.height - kOvMargin - H);
  arm_idle_timer();
}

}  // namespace fleetwm
