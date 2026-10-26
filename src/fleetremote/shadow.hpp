#pragma once

// A viewer's copy of what it has been sent. With it a tile that came out identical (the compositor's damage is
// coarse: a blinking cursor or a clock marks the tiles around it) is not sent again, and a scroll shows up as the new
// picture equal to the old one moved by some lines, which VNC can say in 8 bytes (CopyRect) instead of resending the
// window.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "hub.hpp"

namespace fleetremote {

struct ScrollMove {
  int x = 0, y = 0, w = 0, h = 0;  // the rectangle that moves, where it ends up
  int dy = 0;                      // it comes from y - dy
};

class Shadow {
 public:
  void reset(int w, int h) {
    w_ = w;
    h_ = h;
    cols_ = (w + kTile - 1) / kTile;
    rows_ = (h + kTile - 1) / kTile;
    px_.assign(static_cast<size_t>(w) * h, 0);
    known_.assign(static_cast<size_t>(cols_) * rows_, 0);
  }
  void forget() { std::fill(known_.begin(), known_.end(), 0); }  // the viewer lost its picture
  size_t bytes() const { return px_.capacity() * sizeof(uint32_t) + known_.capacity(); }
  int width() const { return w_; }
  int height() const { return h_; }

  // Moves the tiles that differ from the viewer's copy to the front of the batch and returns how many.
  size_t drop_unchanged(TileBatch& b) const {
    size_t kept = 0;
    for (size_t i = 0; i < b.n; ++i) {
      if (!same(b.v[i])) {
        if (kept != i) std::swap(b.v[kept], b.v[i]);
        ++kept;
      }
    }
    return kept;
  }

  // The viewer now has this tile.
  void commit(const Tile& t) {
    for (int row = 0; row < t.h; ++row)
      std::memcpy(&px_[static_cast<size_t>(t.y + row) * w_ + t.x], &t.px[static_cast<size_t>(row) * t.w], sizeof(uint32_t) * t.w);
    known_[static_cast<size_t>(t.y / kTile) * cols_ + t.x / kTile] = 1;
  }

  // Looks for a vertical scroll in the first `n` tiles of `b`: a block of the new picture that is the old picture
  // moved up or down. Returns false when there is none worth a message.
  bool detect_scroll(const TileBatch& b, size_t n, ScrollMove* out) const {
    if (n < 6 || w_ <= 0) return false;
    // The columns most tiles of the change sit in (a scrolling window), and the rows they span.
    std::vector<int> per_col(static_cast<size_t>(cols_), 0);
    for (size_t i = 0; i < n; ++i) ++per_col[static_cast<size_t>(b.v[i].x / kTile)];
    const int best = *std::max_element(per_col.begin(), per_col.end());
    int c0 = -1, c1 = -1;
    for (int c = 0; c < cols_; ++c) {
      if (per_col[static_cast<size_t>(c)] * 2 >= best) {
        if (c0 < 0) c0 = c;
        c1 = c;
      }
    }
    int r0 = rows_, r1 = -1;
    for (size_t i = 0; i < n; ++i) {
      const int c = b.v[i].x / kTile;
      if (c < c0 || c > c1) continue;
      r0 = std::min(r0, b.v[i].y / kTile);
      r1 = std::max(r1, b.v[i].y / kTile);
    }
    if (r1 < r0) return false;
    const int x0 = c0 * kTile, x1 = std::min(w_, (c1 + 1) * kTile);
    const int y0 = r0 * kTile, y1 = std::min(h_, (r1 + 1) * kTile);
    const int bw = x1 - x0, bh = y1 - y0;
    if (bh < 2 * kTile) return false;
    for (int r = r0; r <= r1; ++r)
      for (int c = c0; c <= c1; ++c)
        if (!known_[static_cast<size_t>(r) * cols_ + c]) return false;

    // The new picture of the block: the viewer's copy with the changed tiles laid over it.
    std::vector<uint32_t> nb(static_cast<size_t>(bw) * bh);
    for (int y = 0; y < bh; ++y) std::memcpy(&nb[static_cast<size_t>(y) * bw], &px_[static_cast<size_t>(y0 + y) * w_ + x0], sizeof(uint32_t) * bw);
    for (size_t i = 0; i < n; ++i) {
      const Tile& t = b.v[i];
      if (t.x < x0 || t.x >= x1 || t.y < y0 || t.y >= y1) continue;
      for (int row = 0; row < t.h; ++row)
        std::memcpy(&nb[static_cast<size_t>(t.y - y0 + row) * bw + (t.x - x0)], &t.px[static_cast<size_t>(row) * t.w], sizeof(uint32_t) * t.w);
    }

    const int reach = std::min(bh, 512);  // how far the block may have moved
    const int ylo = std::max(0, y0 - reach), yhi = std::min(h_, y1 + reach);
    auto hash_row = [&](const uint32_t* p) {
      uint64_t h = 1469598103934665603ull;
      for (int i = 0; i < bw; ++i) h = (h ^ p[i]) * 1099511628211ull;
      return h;
    };
    std::vector<uint64_t> nh(static_cast<size_t>(bh));
    std::unordered_map<uint64_t, int> new_count;
    for (int y = 0; y < bh; ++y) {
      nh[static_cast<size_t>(y)] = hash_row(&nb[static_cast<size_t>(y) * bw]);
      ++new_count[nh[static_cast<size_t>(y)]];
    }
    std::unordered_map<uint64_t, std::vector<int>> old_rows;
    for (int y = ylo; y < yhi; ++y) {
      auto& v = old_rows[hash_row(&px_[static_cast<size_t>(y) * w_ + x0])];
      if (v.size() < 4) v.push_back(y);
    }
    std::unordered_map<int, int> votes;
    int informative = 0;
    for (int y = 0; y < bh; ++y) {
      if (new_count[nh[static_cast<size_t>(y)]] > 2) continue;  // blank or repeated lines say nothing about a move
      ++informative;
      auto it = old_rows.find(nh[static_cast<size_t>(y)]);
      if (it == old_rows.end() || it->second.size() > 3) continue;
      for (int oy : it->second)
        if (oy != y0 + y) ++votes[y0 + y - oy];
    }
    int dy = 0, top = 0;
    for (const auto& [d, v] : votes)
      if (v > top) {
        top = v;
        dy = d;
      }
    if (top < std::max(4, informative / 4)) return false;

    // The longest run of rows that really are the old rows moved by dy.
    int best_a = 0, best_len = 0, run_a = -1;
    for (int y = 0; y <= bh; ++y) {
      bool match = false;
      if (y < bh) {
        const int sy = y0 + y - dy;
        match = sy >= 0 && sy < h_ && std::memcmp(&nb[static_cast<size_t>(y) * bw], &px_[static_cast<size_t>(sy) * w_ + x0], sizeof(uint32_t) * bw) == 0;
      }
      if (match) {
        if (run_a < 0) run_a = y;
      } else if (run_a >= 0) {
        if (y - run_a > best_len) {
          best_len = y - run_a;
          best_a = run_a;
        }
        run_a = -1;
      }
    }
    if (best_len < 32) return false;
    int changed = 0;  // rows the move saves from being sent
    for (int y = best_a; y < best_a + best_len; ++y)
      if (std::memcmp(&nb[static_cast<size_t>(y) * bw], &px_[static_cast<size_t>(y0 + y) * w_ + x0], sizeof(uint32_t) * bw) != 0) ++changed;
    if (changed < 16) return false;
    // The source rows must be on the viewer's screen as the copy has them.
    const int sy0 = y0 + best_a - dy, sy1 = sy0 + best_len;
    for (int r = sy0 / kTile; r <= (sy1 - 1) / kTile; ++r)
      for (int c = c0; c <= c1; ++c)
        if (!known_[static_cast<size_t>(r) * cols_ + c]) return false;
    out->x = x0;
    out->y = y0 + best_a;
    out->w = bw;
    out->h = best_len;
    out->dy = dy;
    return true;
  }

  // The viewer has done the move: do it on the copy.
  void apply(const ScrollMove& m) {
    auto copy_row = [&](int y) {
      std::memmove(&px_[static_cast<size_t>(y) * w_ + m.x], &px_[static_cast<size_t>(y - m.dy) * w_ + m.x], sizeof(uint32_t) * m.w);
    };
    if (m.dy > 0)
      for (int y = m.y + m.h - 1; y >= m.y; --y) copy_row(y);  // content moved down: fill from the bottom
    else
      for (int y = m.y; y < m.y + m.h; ++y) copy_row(y);
  }

 private:
  bool same(const Tile& t) const {
    if (!known_[static_cast<size_t>(t.y / kTile) * cols_ + t.x / kTile]) return false;
    for (int row = 0; row < t.h; ++row)
      if (std::memcmp(&px_[static_cast<size_t>(t.y + row) * w_ + t.x], &t.px[static_cast<size_t>(row) * t.w], sizeof(uint32_t) * t.w) != 0) return false;
    return true;
  }

  int w_ = 0, h_ = 0, cols_ = 0, rows_ = 0;
  std::vector<uint32_t> px_;
  std::vector<uint8_t> known_;
};

}  // namespace fleetremote
