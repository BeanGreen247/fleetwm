#include "hub.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "scale.hpp"

namespace fleetremote {

namespace {

void poke(int fd) {
  const uint64_t one = 1;
  [[maybe_unused]] ssize_t r = write(fd, &one, sizeof one);
}

}  // namespace

Session::Session(Hub& h, std::string proto)
    : hub(h), protocol(std::move(proto)), tuning(protocol == "vnc" ? h.vnc_tuning : h.rdp_tuning) {
  wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  hub.add(this);
}

Session::~Session() {
  hub.remove(this);
  if (wake_fd >= 0) close(wake_fd);
}

Hub::Hub() {
  input_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  ctl_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

Hub::~Hub() {
  close(input_fd_);
  close(ctl_fd_);
}

void Hub::add(Session* s) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    s->dirty_.assign(static_cast<size_t>(cols_) * rows_, 1);
    s->resized_ = false;
    sessions_.push_back(s);
  }
  poke(ctl_fd_);
}

void Hub::remove(Session* s) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ProtoTotals& gone = departed_[s->protocol == "vnc" ? 0 : 1];
    gone.tx += s->tx.load();
    gone.rx += s->rx.load();
    gone.cpu_ns += s->cpu_ns.load();
    sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), s), sessions_.end());
  }
  poke(ctl_fd_);
}

bool Hub::frame_size(int* w, int* h) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (w_ <= 0) return false;
  *w = w_;
  *h = h_;
  return true;
}

bool Hub::take_dirty(Session& s, TileBatch* out, int* w, int* h, bool* resized, int tw, int th) {
  uint64_t drain;
  [[maybe_unused]] ssize_t r = read(s.wake_fd, &drain, sizeof drain);
  std::lock_guard<std::mutex> lock(mutex_);
  if (w_ <= 0) return false;
  int ow, oh;
  fit_size(w_, h_, tw, th, &ow, &oh);
  *w = ow;
  *h = oh;
  *resized = s.resized_;
  s.resized_ = false;
  if (ow != s.out_w_ || oh != s.out_h_) {  // a new target: the session gets the whole picture again
    if (s.out_w_ != 0) *resized = true;
    s.out_w_ = ow;
    s.out_h_ = oh;
    s.dirty_.assign(static_cast<size_t>(cols_) * rows_, 1);
  }
  size_t n = 0;  // reuse the tiles (and their pixel buffers) the caller already has
  out->n = 0;
  if (s.dirty_.size() != static_cast<size_t>(cols_) * rows_) s.dirty_.assign(static_cast<size_t>(cols_) * rows_, 1);
  const bool scaled = ow != w_ || oh != h_;
  if (!scaled) {
    for (int ty = 0; ty < rows_; ++ty) {
      for (int tx = 0; tx < cols_; ++tx) {
        uint8_t& flag = s.dirty_[static_cast<size_t>(ty) * cols_ + tx];
        if (!flag) continue;
        flag = 0;
        if (n == out->v.size()) out->v.emplace_back();
        Tile& t = out->v[n++];
        t.x = tx * kTile;
        t.y = ty * kTile;
        t.w = std::min(kTile, w_ - t.x);
        t.h = std::min(kTile, h_ - t.y);
        t.px.resize(static_cast<size_t>(t.w) * t.h);
        for (int row = 0; row < t.h; ++row)
          std::memcpy(&t.px[static_cast<size_t>(row) * t.w], &px_[static_cast<size_t>(t.y + row) * w_ + t.x], sizeof(uint32_t) * t.w);
      }
    }
    out->n = n;
    return true;
  }
  // The shrunk picture: every screen tile that changed marks the output tiles it reaches (one pixel more on each side
  // for the filter), then those output tiles are rebuilt from the current screen.
  const int ocols = (ow + kTile - 1) / kTile, orows = (oh + kTile - 1) / kTile;
  std::vector<uint8_t> odirty(static_cast<size_t>(ocols) * orows, 0);
  for (int ty = 0; ty < rows_; ++ty) {
    for (int tx = 0; tx < cols_; ++tx) {
      uint8_t& flag = s.dirty_[static_cast<size_t>(ty) * cols_ + tx];
      if (!flag) continue;
      flag = 0;
      const int x0 = std::max(0, static_cast<int>(static_cast<int64_t>(tx * kTile) * ow / w_) - 1);
      const int x1 = std::min(ow - 1, static_cast<int>(static_cast<int64_t>(std::min(w_, (tx + 1) * kTile)) * ow / w_) + 1);
      const int y0 = std::max(0, static_cast<int>(static_cast<int64_t>(ty * kTile) * oh / h_) - 1);
      const int y1 = std::min(oh - 1, static_cast<int>(static_cast<int64_t>(std::min(h_, (ty + 1) * kTile)) * oh / h_) + 1);
      for (int oy = y0 / kTile; oy <= y1 / kTile; ++oy)
        for (int ox = x0 / kTile; ox <= x1 / kTile; ++ox) odirty[static_cast<size_t>(oy) * ocols + ox] = 1;
    }
  }
  for (int oy = 0; oy < orows; ++oy) {
    for (int ox = 0; ox < ocols; ++ox) {
      if (!odirty[static_cast<size_t>(oy) * ocols + ox]) continue;
      if (n == out->v.size()) out->v.emplace_back();
      Tile& t = out->v[n++];
      t.x = ox * kTile;
      t.y = oy * kTile;
      t.w = std::min(kTile, ow - t.x);
      t.h = std::min(kTile, oh - t.y);
      t.px.resize(static_cast<size_t>(t.w) * t.h);
      box_scale(px_.data(), w_, h_, ow, oh, t.x, t.y, t.w, t.h, t.px.data());
    }
  }
  out->n = n;
  return true;
}

void Hub::mark_all_dirty(Session& s) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::fill(s.dirty_.begin(), s.dirty_.end(), 1);
  poke(s.wake_fd);
}

void Hub::push_input(const InputEvent& ev) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (input_.size() > 4096) return;  // a stuck compositor must not make this grow without bound
    input_.push_back(ev);
  }
  poke(input_fd_);
}

std::vector<InputEvent> Hub::drain_input() {
  uint64_t drain;
  [[maybe_unused]] ssize_t r = read(input_fd_, &drain, sizeof drain);
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<InputEvent> out(input_.begin(), input_.end());
  input_.clear();
  return out;
}

void Hub::kick(const std::string& protocol) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Session* s : sessions_)
    if (s->protocol == protocol) {
      s->stop = true;
      poke(s->wake_fd);
    }
}

void Hub::kick_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Session* s : sessions_) {
    s->stop = true;
    poke(s->wake_fd);
  }
}

Hub::ProtoTotals Hub::totals(const std::string& protocol) {
  std::lock_guard<std::mutex> lock(mutex_);
  ProtoTotals t = departed_[protocol == "vnc" ? 0 : 1];
  for (Session* s : sessions_) {
    if (s->protocol != protocol) continue;
    ++t.clients;
    t.tx += s->tx.load();
    t.rx += s->rx.load();
    t.cpu_ns += s->cpu_ns.load();
    t.mem += s->mem.load();
  }
  return t;
}

int Hub::count(const std::string& protocol) {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(std::count_if(sessions_.begin(), sessions_.end(), [&](Session* s) { return s->protocol == protocol; }));
}

int Hub::count() {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(sessions_.size());
}

bool Hub::auth_allowed() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  while (!failures_.empty() && now - failures_.front() > std::chrono::minutes(1)) failures_.pop_front();
  return failures_.size() < 5;
}

void Hub::auth_failed() {
  std::lock_guard<std::mutex> lock(mutex_);
  failures_.push_back(std::chrono::steady_clock::now());
}

void Hub::clear_frame() {
  std::lock_guard<std::mutex> lock(mutex_);
  w_ = h_ = cols_ = rows_ = 0;
  px_.clear();
  px_.shrink_to_fit();
}

void Hub::publish(int w, int h, const uint8_t* src, int stride, bool swap_rb, bool y_invert, const std::vector<Rect>& damage) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool size_changed = w != w_ || h != h_;
  std::vector<Rect> full;
  const std::vector<Rect>* rects = &damage;
  if (size_changed) {
    w_ = w;
    h_ = h;
    cols_ = (w + kTile - 1) / kTile;
    rows_ = (h + kTile - 1) / kTile;
    px_.assign(static_cast<size_t>(w) * h, 0);
    full.push_back({0, 0, w, h});
    rects = &full;
  } else if (damage.empty()) {
    full.push_back({0, 0, w, h});
    rects = &full;
  }
  for (const Rect& r0 : *rects) {
    const int x0 = std::clamp(r0.x, 0, w), y0 = std::clamp(r0.y, 0, h);
    const int x1 = std::clamp(r0.x + r0.w, 0, w), y1 = std::clamp(r0.y + r0.h, 0, h);
    if (x1 <= x0 || y1 <= y0) continue;
    for (int y = y0; y < y1; ++y) {
      const uint8_t* row = src + static_cast<size_t>(y_invert ? h - 1 - y : y) * stride;
      uint32_t* dst = &px_[static_cast<size_t>(y) * w];
      if (swap_rb) {
        for (int x = x0; x < x1; ++x) {
          uint32_t v;
          std::memcpy(&v, row + 4 * x, 4);
          dst[x] = ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
        }
      } else {
        for (int x = x0; x < x1; ++x) {
          uint32_t v;
          std::memcpy(&v, row + 4 * x, 4);
          dst[x] = v & 0x00FFFFFF;
        }
      }
    }
    const int tx0 = x0 / kTile, tx1 = (x1 - 1) / kTile, ty0 = y0 / kTile, ty1 = (y1 - 1) / kTile;
    for (Session* s : sessions_) {
      if (s->dirty_.size() != static_cast<size_t>(cols_) * rows_) s->dirty_.assign(static_cast<size_t>(cols_) * rows_, 1);
      for (int ty = ty0; ty <= ty1; ++ty)
        std::memset(&s->dirty_[static_cast<size_t>(ty) * cols_ + tx0], 1, static_cast<size_t>(tx1 - tx0 + 1));
    }
  }
  for (Session* s : sessions_) {
    if (size_changed) {
      s->resized_ = true;
      s->dirty_.assign(static_cast<size_t>(cols_) * rows_, 1);
    }
    poke(s->wake_fd);
  }
}

}  // namespace fleetremote
