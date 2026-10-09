#include "backdrop.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "image.hpp"
#include "wallpaper_config.hpp"

namespace fleetwm::kit {

namespace fs = std::filesystem;

void box_blur_rgba(uint8_t* p, int w, int h, int radius, int passes) {
  if (w < 1 || h < 1 || radius < 1) return;
  std::vector<uint8_t> tmp(static_cast<size_t>(w) * h * 4);
  const int span = 2 * radius + 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (int dir = 0; dir < 2; ++dir) {  // horizontal, then vertical
      const int lines = dir == 0 ? h : w, len = dir == 0 ? w : h;
      const size_t step = dir == 0 ? 4 : static_cast<size_t>(w) * 4;
      const size_t line_step = dir == 0 ? static_cast<size_t>(w) * 4 : 4;
      for (int l = 0; l < lines; ++l) {
        const uint8_t* in = p + l * line_step;
        uint8_t* out = tmp.data() + l * line_step;
        for (int c = 0; c < 4; ++c) {
          int sum = 0;
          for (int i = -radius; i <= radius; ++i) sum += in[std::clamp(i, 0, len - 1) * step + c];
          for (int i = 0; i < len; ++i) {
            out[i * step + c] = static_cast<uint8_t>(sum / span);
            sum += in[std::min(i + radius + 1, len - 1) * step + c] - in[std::max(i - radius, 0) * step + c];
          }
        }
      }
      std::copy(tmp.begin(), tmp.end(), p);
    }
  }
}

void downscale_rgba(const uint8_t* src, int sw, int sh, int max_w, uint8_t** dst, int* dw, int* dh) {
  const int w = std::max(1, std::min(sw, max_w));
  const int h = std::max(1, static_cast<int>(static_cast<long long>(sh) * w / sw));
  *dw = w;
  *dh = h;
  *dst = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * h * 4));
  for (int y = 0; y < h; ++y) {
    const int y0 = static_cast<int>(static_cast<long long>(y) * sh / h), y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * sh / h));
    for (int x = 0; x < w; ++x) {
      const int x0 = static_cast<int>(static_cast<long long>(x) * sw / w), x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * sw / w));
      long sum[4] = {0, 0, 0, 0};
      int n = 0;
      for (int yy = y0; yy < y1 && yy < sh; ++yy)
        for (int xx = x0; xx < x1 && xx < sw; ++xx, ++n)
          for (int c = 0; c < 4; ++c) sum[c] += src[(static_cast<size_t>(yy) * sw + xx) * 4 + c];
      for (int c = 0; c < 4; ++c) (*dst)[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(n ? sum[c] / n : 0);
    }
  }
}

namespace {

std::string cache_dir() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  return (xdg && *xdg ? std::string(xdg) : std::string(home ? home : "/tmp") + "/.cache") + "/fleetwm";
}

// What the cached picture was made from: the wallpaper's path, size and modification time.
std::string source_key(const WallpaperConfig& wc) {
  if (wc.use_solid_color || wc.path.empty()) return "solid:" + wc.solid_color;
  struct stat st {};
  if (stat(wc.path.c_str(), &st) != 0) return "";
  std::ostringstream k;
  k << wc.path << "|" << st.st_size << "|" << st.st_mtime;
  return k.str();
}

cairo_surface_t* solid_surface(const std::string& hex) {
  unsigned r = 30, g = 30, b = 46;
  if (hex.size() == 7 && hex[0] == '#') std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 4, 4);
  cairo_t* cr = cairo_create(s);
  cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  return s;
}

}  // namespace

cairo_surface_t* load_backdrop_uncached() {
  const WallpaperConfig wc = load_wallpaper_config();
  const std::string key = source_key(wc);
  if (key.empty()) return nullptr;
  if (key.rfind("solid:", 0) == 0) return solid_surface(wc.solid_color);

  const std::string dir = cache_dir(), png = dir + "/backdrop.png", keyfile = dir + "/backdrop.key";
  {
    std::ifstream in(keyfile);
    std::string stored;
    std::getline(in, stored);
    if (stored == key) {
      cairo_surface_t* s = cairo_image_surface_create_from_png(png.c_str());
      if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return s;
      cairo_surface_destroy(s);
    }
  }
  // Not cached yet (or the wallpaper changed): decode, shrink, blur, remember.
  Image img = load_image(wc.path);
  if (!img.ok()) return nullptr;
  uint8_t* small = nullptr;
  int w = 0, h = 0;
  downscale_rgba(img.rgba.data(), img.width, img.height, 192, &small, &w, &h);
  box_blur_rgba(small, w, h, 5, 3);
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
  cairo_surface_flush(s);
  uint8_t* dst = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint8_t* px = small + (static_cast<size_t>(y) * w + x) * 4;
      reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * stride)[x] = (0xFFu << 24) | (px[0] << 16) | (px[1] << 8) | px[2];
    }
  cairo_surface_mark_dirty(s);
  std::free(small);
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (cairo_surface_write_to_png(s, png.c_str()) == CAIRO_STATUS_SUCCESS) std::ofstream(keyfile) << key << "\n";
  return s;
}

void paint_backdrop(cairo_t* cr, cairo_surface_t* bd, int out_w, int out_h, double sx, double sy, double x, double y,
                    double w, double h) {
  if (!bd || out_w < 1 || out_h < 1) return;
  const double bw = cairo_image_surface_get_width(bd), bh = cairo_image_surface_get_height(bd);
  const double scale = std::max(out_w / bw, out_h / bh);  // cover-fit, as the wallpaper itself
  const double ox = (out_w - bw * scale) / 2, oy = (out_h - bh * scale) / 2;
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  // (x, y) is where the rectangle is drawn on `cr`; (sx, sy) is where it sits on the output.
  cairo_translate(cr, x - sx + ox, y - sy + oy);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, bd, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
  cairo_paint(cr);
  cairo_restore(cr);
}

cairo_surface_t* load_backdrop(bool recheck) {
  // One decoded copy per process, re-checked against the wallpaper at most every two seconds: a
  // caller that asks on every Alt+Tab step no longer reads and decodes the PNG each time.
  static cairo_surface_t* shared = nullptr;
  static std::string shared_key;
  static std::chrono::steady_clock::time_point checked;
  const auto now = std::chrono::steady_clock::now();
  if (shared && !recheck && now - checked < std::chrono::seconds(2)) return cairo_surface_reference(shared);
  const std::string key = source_key(load_wallpaper_config());
  checked = now;
  if (shared && key == shared_key) return cairo_surface_reference(shared);
  cairo_surface_t* fresh = load_backdrop_uncached();
  if (shared) cairo_surface_destroy(shared);
  shared = fresh;
  shared_key = key;
  return shared ? cairo_surface_reference(shared) : nullptr;
}

namespace {

// Glass rectangles are painted once and kept: a clip, a stretched blit, two gradients and two
// strokes become one blit of a ready picture. Small least-recently-used set, capped in bytes.
struct GlassTile {
  cairo_surface_t* image = nullptr;
  cairo_surface_t* backdrop = nullptr;  // referenced, so its address cannot be reused while cached
  int out_w = 0, out_h = 0;
  double sx = 0, sy = 0, w = 0, h = 0, scale = 1;
  GlassStyle st;
  uint64_t used = 0;
  size_t bytes = 0;
};
std::vector<GlassTile> g_tiles;
uint64_t g_tile_clock = 0;
constexpr size_t kTileBudget = 3u << 20;

bool whole(double v) { return std::fabs(v - std::round(v)) < 1e-6; }

bool same_style(const GlassStyle& a, const GlassStyle& b) {
  return a.tint.r == b.tint.r && a.tint.g == b.tint.g && a.tint.b == b.tint.b && a.tint_alpha == b.tint_alpha &&
         a.radius == b.radius && a.rim == b.rim;
}

void paint_glass_direct(cairo_t* cr, cairo_surface_t* bd, int out_w, int out_h, double sx, double sy, double x, double y,
                        double w, double h, const GlassStyle& st);

}  // namespace

void paint_glass(cairo_t* cr, cairo_surface_t* bd, int out_w, int out_h, double sx, double sy, double x, double y, double w,
                 double h, const GlassStyle& st) {
  if (w < 2 || h < 2) return;
  double scale = 1, scale_y = 1;
  cairo_surface_get_device_scale(cairo_get_target(cr), &scale, &scale_y);
  if (!whole(x) || !whole(y) || !whole(w) || !whole(h) || !whole(sx) || !whole(sy) || scale != scale_y ||
      w * h * scale * scale * 4 > kTileBudget / 2) {
    paint_glass_direct(cr, bd, out_w, out_h, sx, sy, x, y, w, h, st);
    return;
  }
  GlassTile* hit = nullptr;
  for (GlassTile& t : g_tiles)
    if (t.backdrop == bd && t.out_w == out_w && t.out_h == out_h && t.sx == sx && t.sy == sy && t.w == w && t.h == h &&
        t.scale == scale && same_style(t.st, st)) {
      hit = &t;
      break;
    }
  if (!hit) {
    GlassTile t;
    t.bytes = static_cast<size_t>(w * scale) * static_cast<size_t>(h * scale) * 4;
    t.image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(w * scale), static_cast<int>(h * scale));
    cairo_surface_set_device_scale(t.image, scale, scale);
    cairo_t* tcr = cairo_create(t.image);
    paint_glass_direct(tcr, bd, out_w, out_h, sx, sy, 0, 0, w, h, st);
    cairo_destroy(tcr);
    t.backdrop = bd ? cairo_surface_reference(bd) : nullptr;
    t.out_w = out_w;
    t.out_h = out_h;
    t.sx = sx;
    t.sy = sy;
    t.w = w;
    t.h = h;
    t.scale = scale;
    t.st = st;
    size_t total = t.bytes;
    for (const GlassTile& o : g_tiles) total += o.bytes;
    while (total > kTileBudget && !g_tiles.empty()) {  // drop the least recently used
      auto lru = std::min_element(g_tiles.begin(), g_tiles.end(), [](const GlassTile& a, const GlassTile& b) { return a.used < b.used; });
      total -= lru->bytes;
      cairo_surface_destroy(lru->image);
      if (lru->backdrop) cairo_surface_destroy(lru->backdrop);
      g_tiles.erase(lru);
    }
    g_tiles.push_back(t);
    hit = &g_tiles.back();
  }
  hit->used = ++g_tile_clock;
  cairo_set_source_surface(cr, hit->image, x, y);
  cairo_paint(cr);
}

namespace {

void paint_glass_direct(cairo_t* cr, cairo_surface_t* bd, int out_w, int out_h, double sx, double sy, double x, double y,
                        double w, double h, const GlassStyle& st) {
  if (w < 2 || h < 2) return;
  cairo_save(cr);
  rounded_rect(cr, x, y, w, h, st.radius);
  cairo_clip(cr);
  if (bd) paint_backdrop(cr, bd, out_w, out_h, sx, sy, x, y, w, h);
  // Tint: over a backdrop it is see-through; with none (no wallpaper) it is nearly solid.
  cairo_set_source_rgba(cr, st.tint.r, st.tint.g, st.tint.b, bd ? st.tint_alpha : 0.92);
  cairo_paint(cr);
  // Sheen: a light wash fading out over the top half, and one soft diagonal band.
  cairo_pattern_t* top = cairo_pattern_create_linear(0, y, 0, y + h * 0.6);
  cairo_pattern_add_color_stop_rgba(top, 0, 1, 1, 1, 0.20);
  cairo_pattern_add_color_stop_rgba(top, 1, 1, 1, 1, 0.0);
  cairo_set_source(cr, top);
  cairo_rectangle(cr, x, y, w, h * 0.6);
  cairo_fill(cr);
  cairo_pattern_destroy(top);
  const double slant = h * 0.8;
  cairo_pattern_t* band = cairo_pattern_create_linear(x + w * 0.18, 0, x + w * 0.18 + std::max(24.0, w * 0.12), 0);
  cairo_pattern_add_color_stop_rgba(band, 0.0, 1, 1, 1, 0.0);
  cairo_pattern_add_color_stop_rgba(band, 0.5, 1, 1, 1, 0.07);
  cairo_pattern_add_color_stop_rgba(band, 1.0, 1, 1, 1, 0.0);
  cairo_set_source(cr, band);
  cairo_move_to(cr, x + w * 0.18 + slant * 0.35, y);
  cairo_line_to(cr, x + w * 0.18 + std::max(24.0, w * 0.12) + slant * 0.35, y);
  cairo_line_to(cr, x + w * 0.18 + std::max(24.0, w * 0.12) - slant * 0.35, y + h);
  cairo_line_to(cr, x + w * 0.18 - slant * 0.35, y + h);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_pattern_destroy(band);
  cairo_restore(cr);
  if (st.rim) {
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, st.radius);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.30);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    if (w > 6 && h > 6) {
      rounded_rect(cr, x + 1.5, y + 1.5, w - 3, h - 3, std::max(0.0, st.radius - 1));
      cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
      cairo_stroke(cr);
    }
  }
}

}  // namespace

}  // namespace fleetwm::kit
