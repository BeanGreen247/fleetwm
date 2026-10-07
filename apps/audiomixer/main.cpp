// fleetwm-audiomixer: GTK-free volume popup. A small OVERLAY layer
// surface under the bar's top-right corner: master volume (mute button +
// slider) and one slider per playing application stream, driven live by
// PipeWire. Escape closes it.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "quit_signals.hpp"
#include "audio_mixer.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "bar_config.hpp"
#include "popup_namespaces.hpp"
#include "popup_spot.hpp"
#include "single_instance.hpp"
#include "theme.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kCardWidth = 280;
constexpr int kWindowWidth = kCardWidth + 24;
constexpr double kFont = 14.67;
constexpr uint32_t kBtnLeft = 0x110;

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct Slider {
  uint32_t node = 0;  // 0 = master
  Rect track;
  int value = 0;
};

struct Mixer {
  App app;
  Palette pal;
  std::unique_ptr<Surface> surface;
  common::AudioMixer mixer;

  bool master_available = false, master_muted = false;
  int master_value = 0;
  std::vector<common::AudioStream> streams;
  Rect mute_rect;
  std::vector<Slider> sliders;
  int drag_node = -1;   // -1 = none, 0 = master, else stream node id
  int focus = 0;        // index into sliders for keyboard control
  int last_height = 0;

  // ---------------------------------------------------------------- layout --
  static constexpr double kPad = 12, kTop = 10, kRowH = 28, kGap = 8, kStreamLabelH = 18;
  static constexpr double kStreamH = kStreamLabelH + 2 + 24;

  int content_height() const {
    double h = kTop + kRowH;
    if (!master_available) h += kGap + 20;
    h += kGap + 4 + 1 + 4 + kGap;  // separator block
    h += streams.empty() ? 0 : streams.size() * kStreamH + (streams.size() - 1) * 6;
    h += kTop;
    return static_cast<int>(std::ceil(h));
  }

  void resize_if_needed() {
    const int h = content_height();
    if (h != last_height) {
      last_height = h;
      surface->set_size(kWindowWidth, h);
    }
  }

  // ------------------------------------------------------------------ draw --
  void draw_speaker(cairo_t* cr, double cx, double cy, int level, bool muted, const Color& c) {
    cairo_save(cr);
    cairo_new_path(cr);
    set_source(cr, c);
    cairo_set_line_width(cr, 1.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx - 7, cy - 2.5);
    cairo_line_to(cr, cx - 4, cy - 2.5);
    cairo_line_to(cr, cx, cy - 6);
    cairo_line_to(cr, cx, cy + 6);
    cairo_line_to(cr, cx - 4, cy + 2.5);
    cairo_line_to(cr, cx - 7, cy + 2.5);
    cairo_close_path(cr);
    cairo_fill(cr);
    if (muted) {
      cairo_move_to(cr, cx + 3, cy - 3);
      cairo_line_to(cr, cx + 8, cy + 3);
      cairo_move_to(cr, cx + 8, cy - 3);
      cairo_line_to(cr, cx + 3, cy + 3);
      cairo_stroke(cr);
    } else {
      for (int i = 0; i < level; ++i) {
        const double r = 3 + 3.2 * i;
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, -0.9, 0.9);
        cairo_stroke(cr);
      }
    }
    cairo_restore(cr);
  }

  void draw_slider(cairo_t* cr, Slider& s, double x, double y, double w, bool focused) {
    const double value_w = 44;
    s.track = {x, y, w - value_w, 24};
    const double ty = y + 12 - 2, tw = s.track.w;
    // trough
    Color trough = pal.fg_secondary;
    trough.a = 0.3;
    rounded_rect(cr, x, ty, tw, 4, 2);
    set_source(cr, trough);
    cairo_fill(cr);
    // fill
    const double fw = tw * std::clamp(s.value, 0, 100) / 100.0;
    if (fw > 0) {
      rounded_rect(cr, x, ty, fw, 4, 2);
      set_source(cr, pal.accent);
      cairo_fill(cr);
    }
    // knob
    cairo_arc(cr, x + fw, ty + 2, 7, 0, 2 * M_PI);
    set_source(cr, pal.fg_primary);
    cairo_fill_preserve(cr);
    if (focused) {
      set_source(cr, pal.accent);
      cairo_set_line_width(cr, 2);
      cairo_stroke(cr);
    } else {
      cairo_new_path(cr);
    }
    // value text
    const std::string v = std::to_string(s.value);
    const TextExtents te = measure_text(cr, v, 13);
    draw_text(cr, v, x + w - value_w + (value_w - te.width) / 2, y + 12 + te.ascent / 2 - 1, 13, pal.fg_secondary);
  }

  void draw(cairo_t* cr, int W, int H) {
    rounded_rect(cr, 0, 0, W, H, pal.rounded ? 8 : 0);
    set_source(cr, pal.bg_primary);
    cairo_fill(cr);

    sliders.clear();
    double y = kTop;
    const double x0 = kPad, cw = W - 2 * kPad;

    // Master row: mute button + slider.
    mute_rect = {x0, y, 28, kRowH};
    rounded_rect(cr, mute_rect.x, mute_rect.y, mute_rect.w, mute_rect.h, pal.rounded ? 6 : 0);
    set_source(cr, parse_color("#3c3c3c"));
    cairo_fill(cr);
    const int level = master_value >= 50 ? 3 : master_value > 0 ? 2 : 1;
    draw_speaker(cr, x0 + 14, y + kRowH / 2, level, master_muted, master_available ? pal.fg_primary : pal.fg_secondary);
    sliders.push_back({0, {}, master_value});
    if (master_available) {
      draw_slider(cr, sliders.back(), x0 + 36, y + 2, cw - 36, focus == 0);
    } else {
      sliders.back().track = {};
    }
    y += kRowH;

    if (!master_available) {
      y += kGap;
      const TextExtents te = measure_text(cr, "Audio unavailable (no PipeWire)", kFont);
      draw_text(cr, "Audio unavailable (no PipeWire)", x0, y + te.ascent, kFont, pal.fg_secondary);
      y += 20;
    }

    // Separator.
    y += kGap + 4;
    Color sep = pal.fg_secondary;
    sep.a = 0.35;
    cairo_rectangle(cr, x0, y, cw, 1);
    set_source(cr, sep);
    cairo_fill(cr);
    y += 1 + 4 + kGap;

    for (size_t i = 0; i < streams.size(); ++i) {
      const auto& st = streams[i];
      draw_text(cr, st.label, x0, y + measure_text(cr, "Ag", kFont).ascent, kFont, pal.fg_secondary);
      sliders.push_back({st.node_id, {}, st.volume_percent});
      draw_slider(cr, sliders.back(), x0, y + kStreamLabelH + 2, cw, focus == static_cast<int>(i) + 1);
      y += kStreamH + 6;
    }
    if (focus >= static_cast<int>(sliders.size())) focus = 0;
  }

  // ----------------------------------------------------------------- state --
  Slider* find_slider(int node) {
    for (auto& s : sliders)
      if (static_cast<int>(s.node) == node) return &s;
    return nullptr;
  }

  void set_value(Slider& s, int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent == s.value) return;
    if (s.node == 0) {
      master_value = percent;
      mixer.set_master_volume(percent);
    } else {
      for (auto& st : streams)
        if (st.node_id == s.node) st.volume_percent = percent;
      mixer.set_stream_volume(s.node, percent);
    }
    surface->queue_draw();
  }

  void drag_to(double x) {
    Slider* s = find_slider(drag_node);
    if (!s || s->track.w <= 0) return;
    const double frac = (x - s->track.x) / s->track.w;
    set_value(*s, static_cast<int>(std::lround(frac * 100.0)));
  }

  void on_master(int percent, bool muted, bool available) {
    master_available = available;
    master_muted = muted;
    if (drag_node != 0) master_value = percent;
    resize_if_needed();
    surface->queue_draw();
  }

  void on_streams(const std::vector<common::AudioStream>& s) {
    std::vector<common::AudioStream> next = s;
    if (drag_node > 0)  // keep the value the user is dragging
      for (auto& n : next)
        for (const auto& o : streams)
          if (n.node_id == static_cast<uint32_t>(drag_node) && o.node_id == n.node_id) n.volume_percent = o.volume_percent;
    streams = std::move(next);
    resize_if_needed();
    surface->queue_draw();
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (b != kBtnLeft) return;
    if (!pressed) {
      drag_node = -1;
      return;
    }
    if (master_available && mute_rect.hit(x, y)) {
      mixer.set_master_muted(!master_muted);
      return;
    }
    for (size_t i = 0; i < sliders.size(); ++i) {
      Rect r = sliders[i].track;
      r.x -= 8;
      r.w += 16;
      if (r.w > 16 && r.hit(x, y)) {
        focus = static_cast<int>(i);
        drag_node = static_cast<int>(sliders[i].node);
        drag_to(x);
        return;
      }
    }
  }

  void on_motion(double x, double) {
    if (drag_node >= 0) drag_to(x);
  }

  void on_scroll(double x, double y, double dy) {
    for (auto& s : sliders)
      if (s.track.w > 0 && s.track.hit(x, y)) {
        set_value(s, s.value + (dy < 0 ? 5 : -5));
        return;
      }
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    if (ev.sym == XKB_KEY_Escape) return app.quit();
    if (sliders.empty()) return;
    auto usable = [&](int i) { return sliders[static_cast<size_t>(i)].track.w > 0; };
    switch (ev.sym) {
      case XKB_KEY_Tab:
      case XKB_KEY_Down:
        for (int n = 1; n <= static_cast<int>(sliders.size()); ++n) {
          const int i = (focus + n) % static_cast<int>(sliders.size());
          if (usable(i)) {
            focus = i;
            break;
          }
        }
        surface->queue_draw();
        break;
      case XKB_KEY_ISO_Left_Tab:
      case XKB_KEY_Up:
        for (int n = 1; n <= static_cast<int>(sliders.size()); ++n) {
          const int i = (focus - n + 2 * static_cast<int>(sliders.size())) % static_cast<int>(sliders.size());
          if (usable(i)) {
            focus = i;
            break;
          }
        }
        surface->queue_draw();
        break;
      case XKB_KEY_Left: if (usable(focus)) set_value(sliders[static_cast<size_t>(focus)], sliders[static_cast<size_t>(focus)].value - 1); break;
      case XKB_KEY_Right: if (usable(focus)) set_value(sliders[static_cast<size_t>(focus)], sliders[static_cast<size_t>(focus)].value + 1); break;
      case XKB_KEY_Page_Down: if (usable(focus)) set_value(sliders[static_cast<size_t>(focus)], sliders[static_cast<size_t>(focus)].value - 10); break;
      case XKB_KEY_Page_Up: if (usable(focus)) set_value(sliders[static_cast<size_t>(focus)], sliders[static_cast<size_t>(focus)].value + 10); break;
      case XKB_KEY_m: mixer.set_master_muted(!master_muted); break;
      default: break;
    }
  }
};

}  // namespace

int main() {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  fleetwm::tune_malloc_for_low_rss();

  // A second launch (a second click on the volume readout) closes the open mixer instead of stacking another.
  const std::string pid_file = fleetwm::single_instance_pid_file("fleetwm-audiomixer");
  if (fleetwm::toggle_running_instance(pid_file, "fleetwm-audiomi")) return 0;
  fleetwm::write_pid_file(pid_file);

  Mixer M;
  M.pal = load_palette(load_theme_config());
  if (!M.app.connect()) return 1;

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  // Next to the bar or taskbar that opened it (see popup_spot.hpp), not always the top right.
  const fleetwm::PopupSpot spot = fleetwm::popup_spot_beside_bar(load_theme_config().window_layout, load_bar_config().layout,
                                                                 load_bar_config().taskbar_position);
  cfg.anchor = ((spot.anchor & fleetwm::kAnchorTop) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : 0) |
               ((spot.anchor & fleetwm::kAnchorBottom) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : 0) |
               ((spot.anchor & fleetwm::kAnchorLeft) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT : 0) |
               ((spot.anchor & fleetwm::kAnchorRight) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT : 0);
  cfg.width = kWindowWidth;
  M.last_height = M.content_height();
  cfg.height = M.last_height;
  cfg.margin_top = spot.top;
  cfg.margin_right = spot.right;
  cfg.margin_bottom = spot.bottom;
  cfg.margin_left = spot.left;
  cfg.exclusive_zone = -1;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
  cfg.name = fleetwm::kAudioMixerNamespace;  // the compositor closes it on a press outside (popup_namespaces.hpp)
  M.surface = std::make_unique<Surface>(M.app, cfg);
  M.surface->on_draw = [&M](cairo_t* cr, int w, int h) { M.draw(cr, w, h); };
  M.surface->on_key = [&M](const KeyEvent& e) { M.on_key(e); };
  M.surface->on_button = [&M](double x, double y, uint32_t b, bool p) { M.on_button(x, y, b, p); };
  M.surface->on_motion = [&M](double x, double y) { M.on_motion(x, y); };
  M.surface->on_scroll = [&M](double, double dy) {
    // pointer position unknown here; scroll adjusts the focused slider
    if (!M.sliders.empty() && M.sliders[static_cast<size_t>(M.focus)].track.w > 0)
      M.set_value(M.sliders[static_cast<size_t>(M.focus)], M.sliders[static_cast<size_t>(M.focus)].value + (dy < 0 ? 5 : -5));
  };
  M.surface->on_closed = [&M] { M.app.quit(); };

  M.mixer.start(
      [&M](int percent, bool muted, bool available) { M.on_master(percent, muted, available); },
      [&M](const std::vector<common::AudioStream>& s) { M.on_streams(s); },
      [&M](std::function<void()> fn) { M.app.post(std::move(fn)); });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  M.app.watch_fd(sfd, [&M] { M.app.quit(); });

  M.app.run();
  fleetwm::remove_pid_file(pid_file);
  return 0;
}
