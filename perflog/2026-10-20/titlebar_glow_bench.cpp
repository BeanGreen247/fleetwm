// Titlebar render cost with the Aero caption glow (kit::draw_text_glow) against the old light text: warm background cache, same title (a hover
// redraw) and a new title every time (the glow picture is built). Build: see docs/OPTIMIZATIONS.md, Round 2026-10-20.
#include <cairo.h>
#include <chrono>
#include <cstdio>
#include <string>
#include "fleetkit.hpp"
#include "titlebar_draw.hpp"
#include "window_geometry.hpp"
using namespace fleetwm; namespace kit = fleetwm::kit;
static double bench(bool aero, bool same_title, int n) {
  ThemeConfig t; t.glass = true; t.glass_tint.mode = aero ? GlassTintMode::Aero : GlassTintMode::Theme;
  kit::Palette pal = kit::load_palette(t);
  geom::TitlebarMetrics m; m.height = 30; m.button_w = 28; m.button_h = 20; m.strip = true;
  const int W = 800;
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, 30);
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) {
    cairo_t* cr = cairo_create(s);
    kit::TitlebarPaint p; p.glass = true; p.focused = true; p.round_top = true;
    p.title = same_title ? "Hamilton C shell x64" : "Window title number " + std::to_string(i);
    p.layout = geom::layout_titlebar(W, m);
    kit::draw_titlebar(cr, W, 30, p, pal);
    cairo_destroy(cr);
  }
  auto d = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
  cairo_surface_destroy(s);
  return d / n;
}
int main() {
  bench(true, true, 200); bench(false, true, 200);
  for (int r = 0; r < 3; ++r)
    std::printf("theme-tint same title %.1f us | aero same title %.1f us | theme-tint new titles %.1f us | aero new titles %.1f us\n",
      bench(false, true, 2000), bench(true, true, 2000), bench(false, false, 2000), bench(true, false, 2000));
}
