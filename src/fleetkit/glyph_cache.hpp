#pragma once

// Finished pictures of the small vector icons (battery, network, volume, power mode, plug). Drawing one with
// cairo's path and stroke calls costs 15-40 us on the Celeron N4020 and the bar repaints several per frame; the
// picture only depends on the icon, its state, its colour and its size, so it is drawn once and copied (about 1 us).
// Small, bounded (at most kMaxEntries pictures), least recently used. See tests/test_glyph_cache.cpp.

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fleetwm::kit {

struct GlyphKey {
  int kind = 0;      // which icon
  int state = 0;     // everything else that changes its look (level, mode, connected...)
  uint32_t rgb = 0;  // icon colour, 8 bits per channel
  int w = 0, h = 0;  // picture size
  bool operator==(const GlyphKey& o) const { return kind == o.kind && state == o.state && rgb == o.rgb && w == o.w && h == o.h; }
};

inline uint32_t glyph_rgb(double r, double g, double b) {
  auto c = [](double v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
  return c(r) << 16 | c(g) << 8 | c(b);
}

struct GlyphCacheStats {
  int entries = 0;
  int hits = 0, misses = 0;
};

namespace glyph_cache_detail {
inline constexpr size_t kMaxEntries = 96;
struct Entry {
  GlyphKey key;
  cairo_surface_t* image = nullptr;
  unsigned long used = 0;
};
inline std::vector<Entry>& entries() {
  static std::vector<Entry> v;
  return v;
}
inline unsigned long& tick() {
  static unsigned long t = 0;
  return t;
}
inline GlyphCacheStats& stats() {
  static GlyphCacheStats s;
  return s;
}
}  // namespace glyph_cache_detail

inline GlyphCacheStats glyph_cache_stats() {
  GlyphCacheStats s = glyph_cache_detail::stats();
  s.entries = static_cast<int>(glyph_cache_detail::entries().size());
  return s;
}

inline void glyph_cache_clear() {
  for (auto& e : glyph_cache_detail::entries()) cairo_surface_destroy(e.image);
  glyph_cache_detail::entries().clear();
  glyph_cache_detail::stats() = {};
}

// Paints the icon described by `key` with its picture's top-left corner at (x, y) rounded to whole pixels.
// `draw(cr, cx, cy)` is called only when the picture is not cached yet; it draws the icon centred on (cx, cy)
// in a transparent surface of key.w x key.h.
template <class Draw>
inline void draw_cached_glyph(cairo_t* cr, double x, double y, const GlyphKey& key, Draw&& draw) {
  using namespace glyph_cache_detail;
  Entry* found = nullptr;
  for (Entry& e : entries())
    if (e.key == key) {
      found = &e;
      break;
    }
  if (found) {
    ++stats().hits;
  } else {
    ++stats().misses;
    Entry made;
    made.key = key;
    made.image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, key.w, key.h);
    cairo_t* ic = cairo_create(made.image);
    draw(ic, key.w / 2.0, key.h / 2.0);
    cairo_destroy(ic);
    if (entries().size() >= kMaxEntries) {
      auto lru = std::min_element(entries().begin(), entries().end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
      cairo_surface_destroy(lru->image);
      entries().erase(lru);
    }
    entries().push_back(made);
    found = &entries().back();
  }
  found->used = ++tick();
  cairo_set_source_surface(cr, found->image, std::round(x), std::round(y));
  cairo_paint(cr);
}

}  // namespace fleetwm::kit
