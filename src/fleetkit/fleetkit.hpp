#pragma once

// fleetwm "fleetkit" toolkit: a tiny GTK-free layer for fleetwm's own Wayland
// clients (wallpaper aside, which needs none of it): one wl_display
// connection + poll() loop, layer-shell surfaces backed by wl_shm buffers
// drawn with cairo, keyboard (xkbcommon, with key repeat) and pointer input,
// timers, extra fds, and the theme palette read from themes/*.css.
// Idle cost is zero: nothing redraws unless queue_draw() was called.

#include <cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "theme.hpp"

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace fleetwm::kit {

struct Color {
  double r = 0, g = 0, b = 0, a = 1;
};

// "#rrggbb" / "#rrggbbaa" -> Color; `fallback` on malformed input.
Color parse_color(const std::string& hex, Color fallback = {});
void set_source(cairo_t* cr, const Color& c);
void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);

// Colors the GTK themes expose as @define-color (themes/<theme>.css), with
// the accent override from theme.toml applied the same way app_style.cpp does.
struct Palette {
  Color bg_primary{0.118, 0.118, 0.180}, bg_secondary{0.094, 0.094, 0.145};
  Color fg_primary{0.804, 0.839, 0.957}, fg_secondary{0.651, 0.678, 0.784};
  Color accent{0.537, 0.706, 0.980};
  bool rounded = true;  // theme.toml corner_style
};
Palette load_palette(const ThemeConfig& theme);

// ---- text helpers (cairo toy text API: fontconfig "Sans", no Pango/GLib) ----
struct TextExtents {
  double width = 0, height = 0, ascent = 0;
};
TextExtents measure_text(cairo_t* cr, const std::string& s, double px, bool bold = false);
// Draws with the baseline at y. Returns the advance width.
double draw_text(cairo_t* cr, const std::string& s, double x, double y, double px,
                 const Color& c, bool bold = false);

// ---- input events ----
enum Mods : uint32_t { kShift = 1, kCtrl = 2, kAlt = 4, kSuper = 8 };

struct KeyEvent {
  xkb_keysym_t sym = 0;
  std::string utf8;
  uint32_t mods = 0;
  bool pressed = true;
  bool repeat = false;
};

class App;

// A layer-shell surface with shm double buffering and a cairo draw callback.
class Surface {
 public:
  struct Config {
    uint32_t layer = 0;     // ZWLR_LAYER_SHELL_V1_LAYER_*
    uint32_t anchor = 0;    // ZWLR_LAYER_SURFACE_V1_ANCHOR_* bitmask
    int width = 0, height = 0;  // 0 = fill (needs both opposite anchors)
    int exclusive_zone = 0;
    int margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
    uint32_t keyboard_mode = 0;  // ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_*
    std::string name = "fleetwm";
    wl_output* output = nullptr;
    // Regular application window (xdg_toplevel) instead of a layer surface.
    // width/height are the initial size; the compositor may reconfigure it.
    bool toplevel = false;
    std::string app_id, title;
    int min_width = 0, min_height = 0;
  };

  Surface(App& app, const Config& cfg);
  ~Surface();
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;

  // Draw callback, called with a cleared (transparent) ARGB32 cairo context
  // already scaled so coordinates are logical pixels.
  std::function<void(cairo_t*, int w, int h)> on_draw;
  std::function<void(const KeyEvent&)> on_key;
  std::function<void(double x, double y)> on_motion;
  std::function<void(double x, double y, uint32_t button, bool pressed)> on_button;
  std::function<void()> on_leave;
  // Keyboard focus moved to another surface or client (e.g. the user clicked or
  // focused another window): popups use it to close themselves.
  std::function<void()> on_keyboard_leave;
  std::function<void(double dx, double dy)> on_scroll;
  std::function<void(int w, int h)> on_configure;
  std::function<void()> on_closed;

  void queue_draw();
  // Redraws only this rectangle (logical px): on_draw runs with a clip set to it and the rest of
  // the picture is carried over from the last frame, so a ticking clock costs a strip, not a bar.
  // Any queue_draw() before the next frame upgrades it to a full redraw.
  void queue_draw_rect(int x, int y, int w, int h);
  void set_size(int w, int h);
  void set_title(const std::string& title);
  void set_anchor(uint32_t anchor);
  void set_exclusive_zone(int z);
  void set_margins(int top, int right, int bottom, int left);
  void set_keyboard_mode(uint32_t mode);
  // Makes the surface invisible to the pointer (clicks/hover fall through).
  void set_input_passthrough();
  int width() const { return width_; }
  int height() const { return height_; }
  int scale() const { return scale_; }
  bool configured() const { return configured_; }
  wl_surface* wl() const { return surface_; }

 private:
  friend class App;
  struct Buffer {
    wl_buffer* buf = nullptr;
    void* data = nullptr;
    size_t size = 0;
    int w = 0, h = 0;
    bool busy = false;
  };
  void render();   // called by App when dirty and idle
  bool wants_render() const { return dirty_ && configured_ && !frame_pending_; }
  bool alloc(Buffer& b, int w, int h);
  void free_buf(Buffer& b);


  App& app_;
  Config cfg_;
  wl_surface* surface_ = nullptr;
  zwlr_layer_surface_v1* ls_ = nullptr;
  xdg_surface* xs_ = nullptr;
  xdg_toplevel* xt_ = nullptr;
  int pending_w_ = 0, pending_h_ = 0;
  wl_callback* frame_cb_ = nullptr;
  Buffer bufs_[2];
  int width_ = 0, height_ = 0, scale_ = 1;
  bool configured_ = false, dirty_ = false, frame_pending_ = false;
  void mark_full() {
    dirty_ = true;
    partial_ = false;
  }
  bool partial_ = false;      // dirty_ is only the rectangle below
  int px_ = 0, py_ = 0, pw_ = 0, ph_ = 0;
  Buffer* last_ = nullptr;    // the buffer holding what is on screen now
  std::vector<wl_output*> entered_;
};

struct OutputInfo {
  wl_output* output = nullptr;
  uint32_t name = 0;
  int32_t scale = 1;
  int32_t width = 0, height = 0;  // current mode, physical px
  std::string description;
};

class App {
 public:
  App();
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  bool connect();  // false (message on stderr) if no compositor/protocols
  void run();
  void quit() { running_ = false; }

  // Extra fds polled alongside Wayland. Callback runs when readable.
  int watch_fd(int fd, std::function<void()> cb);
  void unwatch(int id);
  // Periodic timer (timerfd); returns id usable with unwatch(). The first
  // fire is after `ms` unless `fire_now` is set.
  int add_timer(int ms, std::function<void()> cb, bool fire_now = false);
  // One-shot timer.
  int add_oneshot(int ms, std::function<void()> cb);
  // Thread-safe: runs `fn` on the main loop thread (e.g. results coming from
  // PipeWire's own thread). Valid after connect().
  void post(std::function<void()> fn);
  // Reads the current clipboard (Wayland selection) as UTF-8 text and calls
  // `cb` on the main loop with it (empty string if there is none). Input
  // methods typically call this on Ctrl+V / Shift+Insert. Capped at 64 KiB.
  void paste_text(std::function<void(const std::string&)> cb);

  wl_display* display() const { return display_; }
  wl_compositor* compositor() const { return compositor_; }
  wl_shm* shm() const { return shm_; }
  zwlr_layer_shell_v1* layer_shell() const { return layer_shell_; }
  xdg_wm_base* wm_base() const { return wm_base_; }
  const std::vector<OutputInfo>& outputs() const { return outputs_; }
  int output_scale(wl_output* o) const;
  std::function<void()> on_outputs_changed;

 private:
  friend class Surface;
  struct Watch {
    int id;
    int fd;
    bool owned;
    bool oneshot;
    std::function<void()> cb;
  };
  void register_surface(wl_surface* s, Surface* ss) { surfaces_[s] = ss; }
  void unregister_surface(wl_surface* s);
  Surface* find(wl_surface* s) const;
  void setup_seat();
  void key_repeat_tick();


  wl_display* display_ = nullptr;
  wl_registry* registry_ = nullptr;
  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  zwlr_layer_shell_v1* layer_shell_ = nullptr;
  xdg_wm_base* wm_base_ = nullptr;
  wl_seat* seat_ = nullptr;
  wl_data_device_manager* data_manager_ = nullptr;
  wl_data_device* data_device_ = nullptr;
  wl_data_offer* selection_ = nullptr;          // current clipboard offer
  std::map<wl_data_offer*, std::vector<std::string>> offer_mimes_;
  wl_keyboard* keyboard_ = nullptr;
  wl_pointer* pointer_ = nullptr;
  std::vector<OutputInfo> outputs_;
  std::map<wl_surface*, Surface*> surfaces_;
  std::vector<Watch> watches_;
  int next_id_ = 1;
  int post_fd_ = -1;
  std::mutex post_mu_;
  std::vector<std::function<void()>> post_q_;
  bool running_ = true;

  xkb_context* xkb_ctx_ = nullptr;
  xkb_keymap* xkb_map_ = nullptr;
  xkb_state* xkb_state_ = nullptr;
  Surface* kb_focus_ = nullptr;
  Surface* ptr_focus_ = nullptr;
  double ptr_x_ = 0, ptr_y_ = 0;
  int repeat_rate_ = 25, repeat_delay_ = 400;
  uint32_t repeat_key_ = 0;
  int repeat_timer_ = 0;
  KeyEvent repeat_ev_;
};

// A small text tooltip: overlay layer surface anchored top-left, pointer
// transparent. `x` is the desired horizontal center and `y` the top edge, in
// screen-logical px.
class Tooltip {
 public:
  // Where (x, y) sits relative to the tooltip: Below = tooltip hangs under the
  // point, centered (default, for a top bar); Above = sits over it (bottom
  // bar); Right / Left = beside it, vertically centered (side bars).
  enum class Placement { Below, Above, Right, Left };
  Tooltip(App& app, const Palette& pal, const std::string& text, int x, int y,
          Placement placement = Placement::Below);

 private:
  std::unique_ptr<Surface> surface_;
};

// Calls `cb` (on the main loop) whenever a file in any of `dirs` is written,
// created, renamed into place or deleted. Watching directories (not files)
// survives editors/save routines that replace the file via rename. The
// directories are created if missing. Returns the watch id for App::unwatch.
int watch_dirs(App& app, const std::vector<std::string>& dirs, std::function<void()> cb);

}  // namespace fleetwm::kit
