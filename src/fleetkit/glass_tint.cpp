#include "glass_tint.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "backdrop.hpp"

namespace fleetwm::kit {

namespace {

constexpr int kHueBins = 24;

struct Hsv {
  double h, s, v;  // h in [0,360)
};

Hsv to_hsv(double r, double g, double b) {
  const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  double h = 0;
  if (d > 1e-9) {
    if (mx == r) h = 60 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
    if (h < 0) h += 360;
  }
  return {h, mx <= 0 ? 0 : d / mx, mx};
}

Color from_hsv(double h, double s, double v) {
  const double c = v * s, hp = std::fmod(h, 360.0) / 60.0, x = c * (1 - std::fabs(std::fmod(hp, 2.0) - 1)), m = v - c;
  double r = 0, g = 0, b = 0;
  if (hp < 1) r = c, g = x;
  else if (hp < 2) r = x, g = c;
  else if (hp < 3) g = c, b = x;
  else if (hp < 4) g = x, b = c;
  else if (hp < 5) r = x, b = c;
  else r = c, b = x;
  return {r + m, g + m, b + m, 1.0};
}

Color mixc(const Color& a, const Color& b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
}

}  // namespace

Color aero_blue() { return parse_color("#4a86c8"); }

Color tint_from_pixels(const uint8_t* px, int w, int h) {
  if (!px || w < 1 || h < 1) return aero_blue();
  // A saturation- and brightness-weighted histogram of hues: a big grey sky does not outvote a small orange sunset, and
  // near-black or near-white pixels (whose hue is noise) count for little.
  double weight[kHueBins] = {}, sat[kHueBins] = {}, val[kHueBins] = {}, hx[kHueBins] = {}, hy[kHueBins] = {};
  double total = 0, vsum = 0;
  const long n = static_cast<long>(w) * h;
  for (long i = 0; i < n; ++i) {
    const Hsv c = to_hsv(px[i * 4] / 255.0, px[i * 4 + 1] / 255.0, px[i * 4 + 2] / 255.0);
    vsum += c.v;
    const double wt = c.s * c.s * c.v * (c.v > 0.92 && c.s < 0.15 ? 0.2 : 1.0);
    if (wt < 1e-4) continue;
    const int bin = std::min(kHueBins - 1, static_cast<int>(c.h / (360.0 / kHueBins)));
    weight[bin] += wt;
    sat[bin] += c.s * wt;
    val[bin] += c.v * wt;
    hx[bin] += std::cos(c.h * M_PI / 180) * wt;
    hy[bin] += std::sin(c.h * M_PI / 180) * wt;
    total += wt;
  }
  if (total / static_cast<double>(n) < 0.01) {  // grey picture: a neutral tint at about its brightness
    const double v = std::clamp(vsum / static_cast<double>(n), 0.35, 0.65);
    return from_hsv(215, 0.18, v);
  }
  // Pick the bin whose neighbourhood (itself plus the two beside it) is heaviest, so a hue split across a bin edge still wins.
  int best = 0;
  double best_w = -1;
  for (int b = 0; b < kHueBins; ++b) {
    const double sum = weight[b] + 0.5 * (weight[(b + 1) % kHueBins] + weight[(b + kHueBins - 1) % kHueBins]);
    if (sum > best_w) best_w = sum, best = b;
  }
  double wsum = 0, ss = 0, vv = 0, x = 0, y = 0;
  for (int d = -1; d <= 1; ++d) {
    const int b = (best + d + kHueBins) % kHueBins;
    wsum += weight[b];
    ss += sat[b];
    vv += val[b];
    x += hx[b];
    y += hy[b];
  }
  double hue = std::atan2(y, x) * 180 / M_PI;
  if (hue < 0) hue += 360;
  // Glass wants a colour with some body: saturated enough to read as a tint, and not so bright that light text on it fails.
  return from_hsv(hue, std::clamp(ss / wsum, 0.35, 0.80), std::clamp(vv / wsum, 0.42, 0.72));
}

Color wallpaper_tint() {
  static cairo_surface_t* cached_for = nullptr;  // identity of the shared backdrop the answer belongs to
  static Color cached = aero_blue();
  cairo_surface_t* bd = load_backdrop();
  if (!bd) return aero_blue();
  if (bd != cached_for) {
    cairo_surface_flush(bd);
    const int w = cairo_image_surface_get_width(bd), h = cairo_image_surface_get_height(bd);
    const int stride = cairo_image_surface_get_stride(bd);
    const unsigned char* data = cairo_image_surface_get_data(bd);
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {  // cairo ARGB32/RGB24 is B,G,R,A in memory on little-endian machines
        const unsigned char* s = data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
        uint8_t* d = &rgba[(static_cast<size_t>(y) * w + x) * 4];
        d[0] = s[2];
        d[1] = s[1];
        d[2] = s[0];
        d[3] = 255;
      }
    cached = tint_from_pixels(rgba.data(), w, h);
    if (cached_for) cairo_surface_destroy(cached_for);
    cached_for = cairo_surface_reference(bd);  // held, so the address cannot be reused by a different picture
  }
  cairo_surface_destroy(bd);
  return cached;
}

void mix_glass_colors(Palette& pal, const Color& base, int intensity) {
  const double k = std::clamp(intensity, 0, 100) / 100.0;
  pal.glass_surface = mixc(pal.bg_primary, base, 0.25 + 0.60 * k);
  pal.glass_title = mixc(pal.bg_secondary, base, 0.30 + 0.65 * k);
  pal.glass_title_idle = mixc(pal.bg_secondary, base, 0.12 + 0.30 * k);
}

void apply_glass_tint(Palette& pal, const GlassTintConfig& cfg) {
  if (cfg.mode == GlassTintMode::Theme) {  // the theme's own colours, as before the tint engine
    pal.glass_surface = pal.bg_primary;
    pal.glass_title = mixc(pal.bg_secondary, pal.accent, 0.12);
    pal.glass_title_idle = pal.bg_secondary;
    return;
  }
  Color base = aero_blue();
  if (cfg.mode == GlassTintMode::Custom) base = parse_color(cfg.hex, base);
  else if (cfg.mode == GlassTintMode::Wallpaper) base = wallpaper_tint();
  mix_glass_colors(pal, base, cfg.intensity);
}

}  // namespace fleetwm::kit
