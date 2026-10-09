// The Bluetooth glyph: it draws something in each state, connected has dots beside the rune, off has a red slash.
#include <gtest/gtest.h>

#include <cairo.h>

#include "bluetooth_glyph.hpp"

using fleetwm::kit::BluetoothState;

namespace {
struct Pic {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 40, 40);
  ~Pic() { cairo_surface_destroy(s); }
  void draw(BluetoothState st) {
    cairo_t* cr = cairo_create(s);
    fleetwm::kit::draw_bluetooth_glyph(cr, 20, 20, 24, st, 1, 1, 1);
    cairo_destroy(cr);
    cairo_surface_flush(s);
  }
  int count(bool (*pred)(const unsigned char*)) {
    int n = 0;
    const unsigned char* d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < 40; ++y)
      for (int x = 0; x < 40; ++x) n += pred(d + y * stride + x * 4) ? 1 : 0;
    return n;
  }
};
bool any_ink(const unsigned char* p) { return p[3] > 40; }
bool red(const unsigned char* p) { return p[3] > 200 && p[2] > 180 && p[1] < 100 && p[0] < 100; }  // BGRA
bool left_of_rune(const unsigned char* p) { return p[3] > 40; }
}  // namespace

TEST(BluetoothGlyph, DrawsInEveryState) {
  for (auto st : {BluetoothState::Off, BluetoothState::On, BluetoothState::Connected}) {
    Pic p;
    p.draw(st);
    EXPECT_GT(p.count(any_ink), 20);
  }
}

TEST(BluetoothGlyph, OnlyOffHasTheRedSlash) {
  Pic off, on, conn;
  off.draw(BluetoothState::Off);
  on.draw(BluetoothState::On);
  conn.draw(BluetoothState::Connected);
  EXPECT_GT(off.count(red), 10);
  EXPECT_EQ(on.count(red), 0);
  EXPECT_EQ(conn.count(red), 0);
}

TEST(BluetoothGlyph, ConnectedAddsDotsBesideTheRune) {
  Pic on, conn;
  on.draw(BluetoothState::On);
  conn.draw(BluetoothState::Connected);
  EXPECT_GT(conn.count(left_of_rune), on.count(left_of_rune) + 6);
}

TEST(BluetoothGlyph, StaysInsideItsBox) {
  Pic p;
  p.draw(BluetoothState::Connected);
  const unsigned char* d = cairo_image_surface_get_data(p.s);
  const int stride = cairo_image_surface_get_stride(p.s);
  for (int x = 0; x < 40; ++x) {  // the rows above and below the 24 px rune stay empty
    EXPECT_LT(d[2 * stride + x * 4 + 3], 40);
    EXPECT_LT(d[37 * stride + x * 4 + 3], 40);
  }
}
