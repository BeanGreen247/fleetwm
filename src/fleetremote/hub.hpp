#pragma once

// Shared state between the Wayland thread (screen capture, input injection) and the session threads
// (one per connected VNC or RDP client). The captured screen lives here once; every session keeps a bitmap
// of the 64x64 tiles that changed since it last sent them, so a quiet desktop costs nothing and a busy one
// sends only what moved.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace fleetremote {

constexpr int kTile = 64;

struct InputEvent {
  enum Kind { Motion, Button, Wheel, KeySym, KeyCode } kind = Motion;
  int a = 0;  // Motion: x; Button: evdev button code; Wheel: +1 down, -1 up; KeyCode: evdev key code
  int b = 0;  // Motion: y
  uint32_t sym = 0;  // KeySym: X11 keysym
  bool down = false;
};

struct Tile {
  int x = 0, y = 0, w = 0, h = 0;
  std::vector<uint32_t> px;  // w*h, 0x00RRGGBB
};

// Tiles handed to a session. `v` keeps its elements (and their pixel buffers) between updates; only the first `n` are valid.
struct TileBatch {
  std::vector<Tile> v;
  size_t n = 0;
};

// After a big update (the first picture is every tile) give back the tile buffers a quiet desktop will not need again.
inline void trim_batch(TileBatch& b, size_t keep = 32) {
  if (b.v.size() > keep) {
    b.v.resize(keep);
    b.v.shrink_to_fit();
  }
}

// What a session may cost; read live by its thread, so moving a slider in Settings needs no reconnect.
struct Tuning {
  std::atomic<int> quality{90};  // 1..100
  std::atomic<int> max_kbps{0};  // KB/s, 0 = unlimited
  std::atomic<int> res_w{0}, res_h{0};  // largest picture to send, 0 = the screen's own size
  std::atomic<int> depth{0};            // colour depth to send: 0 = decided by quality, else 8, 16, 24 or 32 bits
};

class Hub;

class Session {
 public:
  Session(Hub& hub, std::string protocol);
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  Hub& hub;
  const std::string protocol;
  const Tuning& tuning;
  int wake_fd = -1;  // eventfd: written when tiles became dirty or the session must stop
  std::atomic<bool> stop{false};
  // What this viewer has cost so far (the session thread keeps cpu_ns and mem up to date; Conn counts the bytes).
  std::atomic<uint64_t> tx{0}, rx{0}, cpu_ns{0}, mem{0};

 private:
  friend class Hub;
  std::vector<uint8_t> dirty_;  // cols*rows, guarded by Hub::mutex_
  bool resized_ = false;
  int out_w_ = 0, out_h_ = 0;   // the size the session was last given
};

class Hub {
 public:
  Hub();
  ~Hub();

  // ---- session threads ----
  void add(Session* s);
  void remove(Session* s);
  // Moves the tiles s has not seen into `out`. `*resized` is set when the screen size changed since the last call
  // (then every tile is returned). Returns the current size in `*w`, `*h`; false while there is no frame yet.
  // With a target (`tw` x `th`, 0 = none) smaller than the screen the tiles are of the shrunk picture and `*w`, `*h` is its size.
  bool take_dirty(Session& s, TileBatch* out, int* w, int* h, bool* resized, int tw = 0, int th = 0);
  void mark_all_dirty(Session& s);
  bool frame_size(int* w, int* h);
  void push_input(const InputEvent& ev);
  // Sessions of one protocol that must go: it was switched off or its password changed.
  void kick(const std::string& protocol);
  void kick_all();
  // Totals for one protocol: live viewers plus those that already left (since the daemon started).
  struct ProtoTotals {
    int clients = 0;
    uint64_t tx = 0, rx = 0, cpu_ns = 0, mem = 0;
  };
  ProtoTotals totals(const std::string& protocol);
  int count(const std::string& protocol);
  int count();
  // Failed logins: after five in a minute new logins are refused for the rest of the minute.
  bool auth_allowed();
  void auth_failed();

  Tuning vnc_tuning, rdp_tuning;

  // ---- Wayland thread ----
  // Copies the damaged rows of a captured frame (row-major, `stride` bytes per row) into the shared frame and
  // marks the touched tiles dirty in every session. Damage is {x,y,w,h} rectangles; empty means the whole frame.
  struct Rect {
    int x, y, w, h;
  };
  void publish(int w, int h, const uint8_t* src, int stride, bool swap_rb, bool y_invert, const std::vector<Rect>& damage);
  void clear_frame();
  std::vector<InputEvent> drain_input();
  int input_fd() const { return input_fd_; }
  int ctl_fd() const { return ctl_fd_; }  // written when a session is added or removed

 private:
  std::mutex mutex_;
  std::vector<Session*> sessions_;
  int w_ = 0, h_ = 0, cols_ = 0, rows_ = 0;
  std::vector<uint32_t> px_;
  std::deque<InputEvent> input_;
  int input_fd_ = -1, ctl_fd_ = -1;
  std::deque<std::chrono::steady_clock::time_point> failures_;
  ProtoTotals departed_[2];  // tx, rx, cpu_ns of viewers that have left, vnc then rdp
};

}  // namespace fleetremote
