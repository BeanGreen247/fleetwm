// Per-call cost of every custom icon. build: g++ -O2 -std=c++20 -I. glyphbench.cpp $(pkg-config --cflags --libs cairo)
#include <chrono>
#include <cstdio>
#include "gauge_glyph.hpp"
#include "network_glyphs.hpp"
#include "speaker_glyph.hpp"
using namespace fleetwm;
static double now_ns() { return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
template <class F> static double bench(cairo_t* cr, F f) {
  for (int i = 0; i < 300; ++i) f(cr);  // warm up
  double best = 1e18;
  for (int round = 0; round < 7; ++round) {
    const double t0 = now_ns();
    for (int i = 0; i < 2000; ++i) f(cr);
    best = std::min(best, (now_ns() - t0) / 2000);
  }
  return best;
}
int main() {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
  cairo_t* cr = cairo_create(s);
  const net::GlyphColor fg{0.9, 0.9, 0.95, 1};
  std::printf("%-34s %10s\n", "glyph (16 px unless noted)", "ns/call");
  auto row = [&](const char* n, double v) { std::printf("%-34s %10.0f\n", n, v); };
  row("gauge (tachometer)", bench(cr, [](cairo_t* c) { kit::draw_gauge_glyph(c, 24, 24, 16, 1.0, .9, .9, .9); }));
  row("speaker, 3 waves", bench(cr, [](cairo_t* c) { kit::draw_speaker_glyph(c, 24, 24, 16, 3, .9, .9, .9); }));
  row("speaker, crossed", bench(cr, [](cairo_t* c) { kit::draw_speaker_glyph(c, 24, 24, 16, 0, .9, .9, .9, true); }));
  row("wifi, 5 bars", bench(cr, [&](cairo_t* c) { net::draw_wifi_glyph(c, 24, 24, 16, 5, fg); }));
  row("mobile, 5 bars", bench(cr, [&](cairo_t* c) { net::draw_mobile_glyph(c, 24, 24, 16, 5, fg); }));
  row("wired, connected", bench(cr, [&](cairo_t* c) { net::draw_ethernet_glyph(c, 24, 24, 16, true, fg); }));
  row("wifi, 34 px (Settings)", bench(cr, [&](cairo_t* c) { net::draw_wifi_glyph(c, 24, 24, 34, 5, fg); }));
}
