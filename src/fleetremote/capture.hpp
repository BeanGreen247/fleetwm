#pragma once

// The Wayland side of fleetwm-remote: grabs the first output with wlr-screencopy (only the damaged parts, only
// while someone is connected) and plays remote input back through the virtual pointer and keyboard protocols.

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "hub.hpp"

struct zwlr_screencopy_manager_v1;
struct zwlr_screencopy_frame_v1;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

namespace fleetremote {

class Capture {
 public:
  explicit Capture(Hub& hub) : hub_(hub) {}
  ~Capture();

  bool connect();            // false when there is no compositor or it lacks the protocols (message on stderr)
  int fd() const { return display_ ? wl_display_get_fd(display_) : -1; }
  // Reads and dispatches pending Wayland events; false when the connection is gone.
  bool dispatch();
  void flush() { if (display_) wl_display_flush(display_); }

  void set_active(bool on);  // capture frames only while a client is connected
  // Milliseconds until a failed grab should be retried, or -1.
  int retry_timeout_ms() const;
  void retry_if_due();
  void handle_input(const InputEvent& ev);

  // Frame callbacks (public for the C listener tables).
  void on_buffer(uint32_t format, uint32_t w, uint32_t h, uint32_t stride);
  void on_flags(uint32_t flags) { y_invert_ = flags & 1; }
  void on_damage(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { damage_.push_back({int(x), int(y), int(w), int(h)}); }
  void on_buffer_done() { copy_now(); }
  void on_ready();
  void on_failed();
  void on_global(uint32_t name, const char* iface, uint32_t version);

 private:
  void request_frame();
  void copy_now();
  bool ensure_buffer();
  void drop_buffer();
  void drop_frame();
  bool build_keymap();
  void release_all();
  uint32_t now_ms() const;
  void key(uint32_t code, bool down);

  Hub& hub_;
  wl_display* display_ = nullptr;
  wl_registry* registry_ = nullptr;
  wl_shm* shm_ = nullptr;
  wl_seat* seat_ = nullptr;
  wl_output* output_ = nullptr;
  zwlr_screencopy_manager_v1* screencopy_ = nullptr;
  uint32_t screencopy_version_ = 1;
  zwlr_virtual_pointer_manager_v1* vptr_mgr_ = nullptr;
  uint32_t vptr_version_ = 1;
  zwp_virtual_keyboard_manager_v1* vkbd_mgr_ = nullptr;
  zwlr_virtual_pointer_v1* pointer_ = nullptr;
  zwp_virtual_keyboard_v1* keyboard_ = nullptr;

  bool active_ = false;
  bool first_ = true;  // the first grab after start copies the whole frame; later ones wait for damage
  zwlr_screencopy_frame_v1* frame_ = nullptr;
  uint32_t fmt_ = 0, w_ = 0, h_ = 0, stride_ = 0;
  bool y_invert_ = false;
  std::vector<Hub::Rect> damage_;
  std::chrono::steady_clock::time_point retry_at_{};
  bool retry_pending_ = false;

  wl_shm_pool* pool_ = nullptr;
  wl_buffer* buffer_ = nullptr;
  uint8_t* map_ = nullptr;
  size_t map_size_ = 0;
  uint32_t buf_w_ = 0, buf_h_ = 0, buf_stride_ = 0, buf_fmt_ = 0;

  // keyboard
  struct KeyInfo {
    uint32_t code;  // evdev
    int level;      // 0 plain, 1 needs Shift
  };
  xkb_context* xkb_ctx_ = nullptr;
  xkb_keymap* xkb_map_ = nullptr;
  xkb_state* xkb_state_ = nullptr;  // mirrors the modifiers the virtual keyboard must announce (it does not track them itself)
  std::map<uint32_t, KeyInfo> sym_keys_;
  bool shift_held_ = false;
  std::set<uint32_t> auto_shifted_;
  std::set<uint32_t> pressed_keys_;
  std::set<uint32_t> pressed_buttons_;
  int last_x_ = 0, last_y_ = 0;
};

}  // namespace fleetremote
