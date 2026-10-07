#pragma once

// The mouse pointer, drawn with cairo in the Windows 7 style: the Aero set (white arrow with a dark outline and a
// soft shadow, the teal glass busy ring, the white hand, double-headed resize arrows, the red "not allowed" sign)
// when glass effects are on, and a flat version of the same shapes (solid fill, solid outline, no shadow, no
// gradients) when they are off. The compositor renders each (shape, style, scale) once into a small buffer and
// keeps it (src/compositor/cursor.cpp); this file has no wlroots in it so the tests can draw too.

#include <cairo.h>

namespace fleetwm::kit {

enum class CursorShape {
  Arrow,      // normal select
  Help,       // arrow with a question mark
  Progress,   // arrow with the busy ring (working in the background)
  Wait,       // the busy ring
  Text,       // I-beam
  Hand,       // link select
  Cross,      // precision select
  NotAllowed,
  Move,       // four-way arrow
  ResizeEW,
  ResizeNS,
  ResizeNESW,
  ResizeNWSE,
  UpArrow,    // alternate select
  Count
};

// Every cursor is drawn on a 32 x 32 grid (logical pixels); a picture for scale s is 32 s pixels square.
inline constexpr int kCursorGrid = 32;

// Maps an xcursor or CSS cursor name ("left_ptr", "ew-resize", "text", "pointer"...) to a shape. Returns false for
// a name it does not draw, so the caller can fall back to the installed cursor theme.
bool cursor_shape_for_name(const char* name, CursorShape* out);

// Where the pointer's tip is, in the 32 x 32 grid.
struct CursorHotspot {
  int x, y;
};
CursorHotspot cursor_hotspot(CursorShape shape);

// Draws the cursor into `cr`, an ARGB surface of (32 * scale) pixels square that starts transparent.
void draw_cursor(cairo_t* cr, CursorShape shape, bool glass, int scale);

}  // namespace fleetwm::kit

// ---- the cache of finished pictures ---------------------------------------------------------------------------
//
// Every (shape, style, scale) is drawn once and kept: there are 14 shapes, 2 styles and one or two scales, and a
// picture is 4 KB at scale 1, so the whole set is about 230 KB at the most. T is whatever the caller keeps (the
// compositor keeps a wlr_buffer*; the tests keep an int).

#include <vector>

namespace fleetwm::kit {

struct CursorKey {
  CursorShape shape = CursorShape::Arrow;
  bool glass = false;
  int scale = 1;
  bool operator==(const CursorKey&) const = default;
};

template <class T>
class CursorCache {
 public:
  // The kept picture for `key`, made by make() the first time (make() returns a T; a default T means it failed and
  // nothing is stored, so the next call tries again).
  template <class Make>
  T get(const CursorKey& key, Make&& make) {
    for (const Entry& e : entries_)
      if (e.key == key) {
        ++hits_;
        return e.value;
      }
    ++misses_;
    T value = make();
    if (value) entries_.push_back({key, value});
    return value;
  }
  // Forgets everything; `drop` is called on each kept picture first (to release a buffer, for instance).
  template <class Drop>
  void clear(Drop&& drop) {
    for (Entry& e : entries_) drop(e.value);
    entries_.clear();
  }
  int entries() const { return static_cast<int>(entries_.size()); }
  int hits() const { return hits_; }
  int misses() const { return misses_; }

 private:
  struct Entry {
    CursorKey key;
    T value;
  };
  std::vector<Entry> entries_;
  int hits_ = 0, misses_ = 0;
};

}  // namespace fleetwm::kit
