#include "capture.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "keyboard_config.hpp"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

namespace fleetremote {

namespace {

constexpr uint32_t kFormatArgb8888 = 0, kFormatXrgb8888 = 1;
constexpr uint32_t kFormatXbgr8888 = 0x34324258, kFormatAbgr8888 = 0x34324241;  // fourcc "XB24" and "AB24"
constexpr uint32_t kKeyShift = 42;                                              // evdev KEY_LEFTSHIFT
constexpr uint32_t kSymShiftL = 0xffe1, kSymShiftR = 0xffe2;

void registry_global(void* data, wl_registry*, uint32_t name, const char* iface, uint32_t version) {
  static_cast<Capture*>(data)->on_global(name, iface, version);
}
void registry_remove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistryListener = {registry_global, registry_remove};

void frame_buffer(void* d, zwlr_screencopy_frame_v1*, uint32_t f, uint32_t w, uint32_t h, uint32_t s) {
  static_cast<Capture*>(d)->on_buffer(f, w, h, s);
}
void frame_flags(void* d, zwlr_screencopy_frame_v1*, uint32_t f) { static_cast<Capture*>(d)->on_flags(f); }
void frame_ready(void* d, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) { static_cast<Capture*>(d)->on_ready(); }
void frame_failed(void* d, zwlr_screencopy_frame_v1*) { static_cast<Capture*>(d)->on_failed(); }
void frame_damage(void* d, zwlr_screencopy_frame_v1*, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  static_cast<Capture*>(d)->on_damage(x, y, w, h);
}
void frame_dmabuf(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) {}
void frame_buffer_done(void* d, zwlr_screencopy_frame_v1*) { static_cast<Capture*>(d)->on_buffer_done(); }
const zwlr_screencopy_frame_v1_listener kFrameListener = {frame_buffer, frame_flags, frame_ready,      frame_failed,
                                                          frame_damage, frame_dmabuf, frame_buffer_done};

int make_memfd(size_t size) {
  int fd = memfd_create("fleetwm-remote", MFD_CLOEXEC);
  if (fd < 0) return -1;
  if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

}  // namespace

Capture::~Capture() {
  release_all();
  drop_frame();
  drop_buffer();
  if (xkb_state_) xkb_state_unref(xkb_state_);
  if (xkb_map_) xkb_keymap_unref(xkb_map_);
  if (xkb_ctx_) xkb_context_unref(xkb_ctx_);
  if (display_) {
    wl_display_flush(display_);
    wl_display_disconnect(display_);
  }
}

void Capture::on_global(uint32_t name, const char* iface, uint32_t version) {
  const std::string i = iface;
  if (i == "wl_shm") {
    shm_ = static_cast<wl_shm*>(wl_registry_bind(registry_, name, &wl_shm_interface, 1));
  } else if (i == "wl_seat" && !seat_) {
    seat_ = static_cast<wl_seat*>(wl_registry_bind(registry_, name, &wl_seat_interface, 1));
  } else if (i == "wl_output" && !output_) {
    output_ = static_cast<wl_output*>(wl_registry_bind(registry_, name, &wl_output_interface, 1));
  } else if (i == "zwlr_screencopy_manager_v1") {
    screencopy_version_ = std::min(version, 3u);
    screencopy_ = static_cast<zwlr_screencopy_manager_v1*>(
        wl_registry_bind(registry_, name, &zwlr_screencopy_manager_v1_interface, screencopy_version_));
  } else if (i == "zwlr_virtual_pointer_manager_v1") {
    vptr_version_ = std::min(version, 2u);
    vptr_mgr_ = static_cast<zwlr_virtual_pointer_manager_v1*>(
        wl_registry_bind(registry_, name, &zwlr_virtual_pointer_manager_v1_interface, vptr_version_));
  } else if (i == "zwp_virtual_keyboard_manager_v1") {
    vkbd_mgr_ = static_cast<zwp_virtual_keyboard_manager_v1*>(
        wl_registry_bind(registry_, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
  }
}

bool Capture::connect() {
  display_ = wl_display_connect(nullptr);
  if (!display_) {
    std::fprintf(stderr, "fleetwm-remote: no Wayland display to capture\n");
    return false;
  }
  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &kRegistryListener, this);
  wl_display_roundtrip(display_);
  wl_display_roundtrip(display_);
  if (!shm_ || !output_ || !screencopy_) {
    std::fprintf(stderr, "fleetwm-remote: the compositor offers no screen capture (wlr-screencopy)\n");
    return false;
  }
  if (seat_ && vptr_mgr_) {
    pointer_ = vptr_version_ >= 2 ? zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(vptr_mgr_, seat_, output_)
                                  : zwlr_virtual_pointer_manager_v1_create_virtual_pointer(vptr_mgr_, seat_);
  }
  if (seat_ && vkbd_mgr_ && build_keymap()) keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkbd_mgr_, seat_);
  if (keyboard_) {
    char* text = xkb_keymap_get_as_string(xkb_map_, XKB_KEYMAP_FORMAT_TEXT_V1);
    const size_t size = std::strlen(text) + 1;
    const int fd = make_memfd(size);
    if (fd >= 0) {
      void* m = mmap(nullptr, size, PROT_WRITE, MAP_SHARED, fd, 0);
      if (m != MAP_FAILED) {
        std::memcpy(m, text, size);
        munmap(m, size);
        zwp_virtual_keyboard_v1_keymap(keyboard_, 1 /* xkb v1 */, fd, static_cast<uint32_t>(size));
      }
      close(fd);
    }
    std::free(text);
  }
  if (!pointer_ || !keyboard_) std::fprintf(stderr, "fleetwm-remote: remote input is off (the compositor has no virtual pointer or keyboard)\n");
  wl_display_flush(display_);
  return true;
}

bool Capture::build_keymap() {
  xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  const fleetwm::XkbNames names = fleetwm::xkb_names_for(fleetwm::load_keyboard_config());
  xkb_rule_names rules = {};
  rules.layout = names.layout.c_str();
  rules.variant = names.variant.c_str();
  rules.model = names.model.empty() ? nullptr : names.model.c_str();
  rules.options = names.options.empty() ? nullptr : names.options.c_str();
  xkb_map_ = xkb_keymap_new_from_names(xkb_ctx_, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!xkb_map_) xkb_map_ = xkb_keymap_new_from_names(xkb_ctx_, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!xkb_map_) return false;
  xkb_state_ = xkb_state_new(xkb_map_);
  // keysym -> key: the first layout, plain level preferred over the Shift level.
  const xkb_keycode_t lo = xkb_keymap_min_keycode(xkb_map_), hi = xkb_keymap_max_keycode(xkb_map_);
  for (int level = 0; level <= 1; ++level) {
    for (xkb_keycode_t kc = lo; kc <= hi; ++kc) {
      if (xkb_keymap_num_layouts_for_key(xkb_map_, kc) == 0 || xkb_keymap_num_levels_for_key(xkb_map_, kc, 0) <= level) continue;
      const xkb_keysym_t* syms;
      if (xkb_keymap_key_get_syms_by_level(xkb_map_, kc, 0, level, &syms) != 1) continue;
      sym_keys_.emplace(syms[0], KeyInfo{kc - 8, level});
    }
  }
  return true;
}

bool Capture::dispatch() {
  if (!display_) return false;
  if (wl_display_prepare_read(display_) != 0) {
    wl_display_dispatch_pending(display_);
    return true;
  }
  wl_display_flush(display_);
  if (wl_display_read_events(display_) < 0) return false;
  if (wl_display_dispatch_pending(display_) < 0) return false;
  wl_display_flush(display_);
  return true;
}

void Capture::set_active(bool on) {
  if (on == active_) return;
  active_ = on;
  if (on) {
    first_ = true;
    request_frame();
  } else {
    release_all();
    drop_frame();
    drop_buffer();
    hub_.clear_frame();
    retry_pending_ = false;
  }
  flush();
}

void Capture::request_frame() {
  if (!active_ || frame_ || !screencopy_) return;
  last_request_ = std::chrono::steady_clock::now();
  damage_.clear();
  y_invert_ = false;
  frame_ = zwlr_screencopy_manager_v1_capture_output(screencopy_, 1 /* with cursor */, output_);
  zwlr_screencopy_frame_v1_add_listener(frame_, &kFrameListener, this);
}

void Capture::drop_frame() {
  if (frame_) {
    zwlr_screencopy_frame_v1_destroy(frame_);
    frame_ = nullptr;
  }
}

void Capture::on_buffer(uint32_t format, uint32_t w, uint32_t h, uint32_t stride) {
  fmt_ = format;
  w_ = w;
  h_ = h;
  stride_ = stride;
  if (screencopy_version_ < 3) copy_now();  // no buffer_done event before version 3
}

void Capture::drop_buffer() {
  if (buffer_) wl_buffer_destroy(buffer_);
  if (pool_) wl_shm_pool_destroy(pool_);
  if (map_) munmap(map_, map_size_);
  buffer_ = nullptr;
  pool_ = nullptr;
  map_ = nullptr;
  map_size_ = 0;
  buf_w_ = buf_h_ = buf_stride_ = buf_fmt_ = 0;
}

bool Capture::ensure_buffer() {
  if (buffer_ && buf_w_ == w_ && buf_h_ == h_ && buf_stride_ == stride_ && buf_fmt_ == fmt_) return true;
  drop_buffer();
  const size_t size = static_cast<size_t>(stride_) * h_;
  const int fd = make_memfd(size);
  if (fd < 0) return false;
  map_ = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
  if (map_ == MAP_FAILED) {
    map_ = nullptr;
    close(fd);
    return false;
  }
  map_size_ = size;
  pool_ = wl_shm_create_pool(shm_, fd, static_cast<int32_t>(size));
  close(fd);
  buffer_ = wl_shm_pool_create_buffer(pool_, 0, static_cast<int32_t>(w_), static_cast<int32_t>(h_), static_cast<int32_t>(stride_), fmt_);
  buf_w_ = w_;
  buf_h_ = h_;
  buf_stride_ = stride_;
  buf_fmt_ = fmt_;
  return true;
}

void Capture::copy_now() {
  if (!frame_) return;
  const bool known = fmt_ == kFormatArgb8888 || fmt_ == kFormatXrgb8888 || fmt_ == kFormatXbgr8888 || fmt_ == kFormatAbgr8888;
  if (!known || !ensure_buffer()) {
    std::fprintf(stderr, "fleetwm-remote: cannot capture this output (pixel format 0x%x)\n", fmt_);
    on_failed();
    return;
  }
  if (first_ || screencopy_version_ < 2) zwlr_screencopy_frame_v1_copy(frame_, buffer_);
  else zwlr_screencopy_frame_v1_copy_with_damage(frame_, buffer_);
}

void Capture::on_ready() {
  const bool swap = fmt_ == kFormatXbgr8888 || fmt_ == kFormatAbgr8888;
  if (first_) damage_.clear();  // the first grab is the whole frame
  first_ = false;
  hub_.publish(static_cast<int>(w_), static_cast<int>(h_), map_, static_cast<int>(stride_), swap, y_invert_, damage_);
  drop_frame();
  // Ask for the next frame no sooner than the fastest viewer can use one: a 24 fps viewer on a 60 Hz screen would otherwise make the
  // compositor copy, and this process publish, 60 frames a second of which most are dropped unseen.
  const auto due = last_request_ + std::chrono::microseconds(1000000 / std::clamp(hub_.max_viewer_fps(), 1, 240));
  if (std::chrono::steady_clock::now() < due) {
    retry_pending_ = active_;
    retry_at_ = due;
  } else {
    request_frame();
  }
  flush();
}

void Capture::on_failed() {
  drop_frame();
  first_ = true;
  retry_pending_ = active_;
  retry_at_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
}

int Capture::retry_timeout_ms() const {
  if (!retry_pending_) return -1;
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(retry_at_ - std::chrono::steady_clock::now()).count();
  return left < 0 ? 0 : static_cast<int>(left);
}

void Capture::retry_if_due() {
  if (retry_pending_ && retry_timeout_ms() == 0) {
    retry_pending_ = false;
    request_frame();
    flush();
  }
}

uint32_t Capture::now_ms() const {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void Capture::key(uint32_t code, bool down) {
  if (!keyboard_) return;
  zwp_virtual_keyboard_v1_key(keyboard_, now_ms(), code, down ? 1 : 0);
  if (xkb_state_ && xkb_state_update_key(xkb_state_, code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP) != 0) {
    zwp_virtual_keyboard_v1_modifiers(keyboard_, xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_DEPRESSED),
                                      xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_LATCHED),
                                      xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_LOCKED),
                                      xkb_state_serialize_layout(xkb_state_, XKB_STATE_LAYOUT_EFFECTIVE));
  }
  if (down) pressed_keys_.insert(code);
  else pressed_keys_.erase(code);
}

void Capture::release_all() {
  if (keyboard_) {
    const std::set<uint32_t> keys = pressed_keys_;
    for (uint32_t k : keys) key(k, false);
  }
  if (pointer_) {
    for (uint32_t b : pressed_buttons_) zwlr_virtual_pointer_v1_button(pointer_, now_ms(), b, 0);
    zwlr_virtual_pointer_v1_frame(pointer_);
  }
  pressed_keys_.clear();
  pressed_buttons_.clear();
  auto_shifted_.clear();
  shift_held_ = false;
}

void Capture::handle_input(const InputEvent& ev) {
  switch (ev.kind) {
    case InputEvent::Motion:
      if (!pointer_ || w_ == 0) return;
      last_x_ = ev.a;
      last_y_ = ev.b;
      zwlr_virtual_pointer_v1_motion_absolute(pointer_, now_ms(), static_cast<uint32_t>(std::max(0, ev.a)),
                                              static_cast<uint32_t>(std::max(0, ev.b)), buf_w_ ? buf_w_ : w_, buf_h_ ? buf_h_ : h_);
      zwlr_virtual_pointer_v1_frame(pointer_);
      break;
    case InputEvent::Button:
      if (!pointer_) return;
      zwlr_virtual_pointer_v1_button(pointer_, now_ms(), static_cast<uint32_t>(ev.a), ev.down ? 1 : 0);
      zwlr_virtual_pointer_v1_frame(pointer_);
      if (ev.down) pressed_buttons_.insert(static_cast<uint32_t>(ev.a));
      else pressed_buttons_.erase(static_cast<uint32_t>(ev.a));
      break;
    case InputEvent::Wheel:
      if (!pointer_) return;
      zwlr_virtual_pointer_v1_axis_source(pointer_, 0 /* wheel */);
      zwlr_virtual_pointer_v1_axis_discrete(pointer_, now_ms(), 0 /* vertical */, wl_fixed_from_int(15 * ev.a), ev.a);
      zwlr_virtual_pointer_v1_frame(pointer_);
      break;
    case InputEvent::KeyCode:
      key(static_cast<uint32_t>(ev.a), ev.down);
      break;
    case InputEvent::KeySym: {
      auto it = sym_keys_.find(ev.sym);
      if (it == sym_keys_.end()) return;
      const KeyInfo info = it->second;
      if (ev.sym == kSymShiftL || ev.sym == kSymShiftR) shift_held_ = ev.down;
      if (ev.down) {
        if (info.level == 1 && !shift_held_) {  // the client sent '@' or 'A' without Shift: hold it for this key only
          key(kKeyShift, true);
          auto_shifted_.insert(info.code);
        }
        key(info.code, true);
      } else {
        key(info.code, false);
        if (auto_shifted_.erase(info.code)) key(kKeyShift, false);
      }
      break;
    }
  }
}

}  // namespace fleetremote
