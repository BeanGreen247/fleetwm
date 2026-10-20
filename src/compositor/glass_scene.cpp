#include "glass_scene.hpp"

#include <cairo.h>
#include <drm_fourcc.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>

extern "C" {
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include "backdrop.hpp"
#include "pixel_buffer.hpp"
#include "server.hpp"
#include "titlebar.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

bool gs_intersects(const wlr_box& a, const wlr_box& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

wlr_box gs_intersection(const wlr_box& a, const wlr_box& b) {
  const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.width, b.x + b.width), y1 = std::min(a.y + a.height, b.y + b.height);
  return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// The window's whole outer rectangle (titlebar, frame and content) in layout coordinates, and its content part.
struct GsRects {
  wlr_box outer, content;
};

bool gs_rects(const View* v, GsRects* r) {
  if (!v->container_tree || !v->container_tree->node.enabled || v->minimized || !v->wants_titlebar()) return false;
  const int bt = v->frame_px(), th = v->titlebar_height(), w = v->content_w, h = v->frame_content_h();
  if (w <= 0 || h <= 0) return false;
  int cx = 0, cy = 0;
  wlr_scene_node_coords(&v->container_tree->node, &cx, &cy);
  r->outer = {cx, cy, w + 2 * bt, v->top_border() + th + h + bt};
  r->content = {cx + bt, cy + v->top_border() + th, w, h};
  return true;
}

void gs_box_blur(unsigned char* p, int w, int h, int stride, int r, int passes) {
  if (w < 1 || h < 1) return;
  std::vector<unsigned char> tmp(static_cast<size_t>(stride) * h);
  const int div = 2 * r + 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < h; ++y) {
      const unsigned char* s = p + static_cast<size_t>(y) * stride;
      unsigned char* d = &tmp[static_cast<size_t>(y) * stride];
      for (int c = 0; c < 4; ++c) {
        int sum = 0;
        for (int x = -r; x <= r; ++x) sum += s[std::clamp(x, 0, w - 1) * 4 + c];
        for (int x = 0; x < w; ++x) {
          d[x * 4 + c] = static_cast<unsigned char>(sum / div);
          sum += s[std::min(x + r + 1, w - 1) * 4 + c] - s[std::max(x - r, 0) * 4 + c];
        }
      }
    }
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c) {
        int sum = 0;
        for (int y = -r; y <= r; ++y) sum += tmp[static_cast<size_t>(std::clamp(y, 0, h - 1)) * stride + x * 4 + c];
        for (int y = 0; y < h; ++y) {
          p[static_cast<size_t>(y) * stride + x * 4 + c] = static_cast<unsigned char>(sum / div);
          sum += tmp[static_cast<size_t>(std::min(y + r + 1, h - 1)) * stride + x * 4 + c] -
                 tmp[static_cast<size_t>(std::max(y - r, 0)) * stride + x * 4 + c];
        }
      }
  }
}

// Reads the part `src` (surface pixels) of a client's texture and paints it, averaged down by kGlassScale, onto `cr` at layout position (dst_x, dst_y).
bool gs_paint_content(cairo_t* cr, const View* v, const wlr_box& src, int dst_x, int dst_y) {
  wlr_surface* surface = v->surface();
  if (!surface || src.width < 1 || src.height < 1) return false;
  wlr_texture* tex = wlr_surface_get_texture(surface);
  if (!tex || surface->current.scale != 1 || static_cast<int64_t>(src.width) * src.height > 1500000) return false;
  if (src.x < 0 || src.y < 0 || src.x + src.width > static_cast<int>(tex->width) || src.y + src.height > static_cast<int>(tex->height)) return false;
  std::vector<uint32_t> px(static_cast<size_t>(src.width) * src.height);
  const wlr_texture_read_pixels_options opt{px.data(), DRM_FORMAT_ARGB8888, static_cast<uint32_t>(src.width) * 4, 0, 0, src};
  if (!wlr_texture_read_pixels(tex, &opt)) return false;
  const int sw = std::max(1, src.width / kGlassScale), sh = std::max(1, src.height / kGlassScale);
  cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
  unsigned char* d = cairo_image_surface_get_data(small);
  const int dstride = cairo_image_surface_get_stride(small);
  for (int y = 0; y < sh; ++y)
    for (int x = 0; x < sw; ++x) {
      uint32_t a = 0, r = 0, g = 0, b = 0;
      for (int by = 0; by < kGlassScale; ++by)
        for (int bx = 0; bx < kGlassScale; ++bx) {
          const int sx = std::min(src.width - 1, x * kGlassScale + bx), sy = std::min(src.height - 1, y * kGlassScale + by);
          const uint32_t p = px[static_cast<size_t>(sy) * src.width + sx];
          a += p >> 24;
          r += (p >> 16) & 255;
          g += (p >> 8) & 255;
          b += p & 255;
        }
      const uint32_t n = kGlassScale * kGlassScale;
      reinterpret_cast<uint32_t*>(d + static_cast<size_t>(y) * dstride)[x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
    }
  cairo_surface_mark_dirty(small);
  cairo_save(cr);
  cairo_scale(cr, kGlassScale, kGlassScale);  // the cairo context works in layout pixels; the small picture is drawn back up to size
  cairo_set_source_surface(cr, small, dst_x / static_cast<double>(kGlassScale), dst_y / static_cast<double>(kGlassScale));
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_paint(cr);
  cairo_restore(cr);
  cairo_surface_destroy(small);
  return true;
}

uint64_t gs_mix(uint64_t h, uint64_t v) { return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 0x100000001b3ull; }

}  // namespace

std::vector<View*> glass_views_below(Server* server, const View* w) {
  // `views` is front = topmost within a layer; the always-on-top layer is above the pinned one, which is above the plain windows.
  std::vector<std::pair<int, View*>> ranked;
  int order = 0;
  for (auto it = server->views.rbegin(); it != server->views.rend(); ++it, ++order) {
    View* v = it->get();
    const int rank = v->always_on_top ? 2 : (v->pinned ? 1 : 0);
    ranked.push_back({rank * 100000 + order, v});
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<View*> below;
  for (const auto& [key, v] : ranked) {
    if (v == w) break;
    below.push_back(v);
  }
  return below;
}

uint64_t glass_backdrop_key(Server* server, const View* w, const wlr_box rects[4]) {
  wlr_box grown[4];
  for (int i = 0; i < 4; ++i)
    grown[i] = {rects[i].x - kGlassMargin, rects[i].y - kGlassMargin, rects[i].width + 2 * kGlassMargin, rects[i].height + 2 * kGlassMargin};
  uint64_t key = 0;
  for (View* v : glass_views_below(server, w)) {
    GsRects r;
    if (!gs_rects(v, &r)) continue;
    bool touches = false;
    for (int i = 0; i < 4; ++i)
      if (gs_intersects(grown[i], r.outer)) touches = true;
    if (!touches) continue;
    if (key == 0) {  // the first window under the glass: the picture now depends on where the glass is, too
      key = 1469598103934665603ull;
      for (int i = 0; i < 4; ++i) {
        key = gs_mix(key, static_cast<uint64_t>(grown[i].x) * 31 + static_cast<uint64_t>(grown[i].y));
        key = gs_mix(key, static_cast<uint64_t>(grown[i].width) * 131 + static_cast<uint64_t>(grown[i].height));
      }
    }
    key = gs_mix(key, reinterpret_cast<uintptr_t>(v));
    key = gs_mix(key, static_cast<uint64_t>(r.outer.x) * 7919 + static_cast<uint64_t>(r.outer.y));
    key = gs_mix(key, static_cast<uint64_t>(r.outer.width) * 104729 + static_cast<uint64_t>(r.outer.height));
    key = gs_mix(key, v->content_serial + (v->focused ? 1u << 31 : 0u));
  }
  return key;  // 0: only the wallpaper is under the glass, and the plain blurred wallpaper shows it exactly
}

void glass_picture_release(GlassPicture* p) {
  if (p->buffer) wlr_buffer_drop(p->buffer);
  *p = GlassPicture{};
}

bool glass_build_pictures(Server* server, const View* w, const wlr_box rects[4], const wlr_box& area, GlassPicture out[4]) {
  cairo_surface_t* wall = kit::load_backdrop();  // the blurred wallpaper (a reference)
  const std::vector<View*> below = glass_views_below(server, w);
  bool any = false;
  for (int i = 0; i < 4; ++i) {
    glass_picture_release(&out[i]);
    const wlr_box R = rects[i];
    if (R.width < 1 || R.height < 1) continue;
    const wlr_box E = {R.x - kGlassMargin, R.y - kGlassMargin, R.width + 2 * kGlassMargin, R.height + 2 * kGlassMargin};
    const int sw = (E.width + kGlassScale - 1) / kGlassScale, sh = (E.height + kGlassScale - 1) / kGlassScale;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    cairo_t* cr = cairo_create(surf);
    cairo_scale(cr, 1.0 / kGlassScale, 1.0 / kGlassScale);
    cairo_translate(cr, -E.x, -E.y);  // layout coordinates from here on
    // 1. the wallpaper
    if (wall) kit::paint_backdrop(cr, wall, area.width, area.height, E.x - area.x, E.y - area.y, E.x, E.y, E.width, E.height);
    else {
      cairo_set_source_rgb(cr, 0.1, 0.3, 0.55);
      cairo_paint(cr);
    }
    // 2. the windows below, bottom to top: their frame colour, then their content
    for (View* v : below) {
      GsRects r;
      if (!gs_rects(v, &r) || !gs_intersects(E, r.outer)) continue;
      double rgba[4];
      titlebar_frame_color(v->focused, server->theme_config().glass, rgba);
      const wlr_box frame = gs_intersection(E, r.outer);
      cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
      cairo_rectangle(cr, frame.x, frame.y, frame.width, frame.height);
      cairo_fill(cr);
      const wlr_box visible = gs_intersection(E, r.content);
      if (visible.width < 1 || visible.height < 1) continue;
      // the surface's own origin is the content's top-left plus the geometry offset of xdg windows
      int gx = 0, gy = 0;
      if (v->kind == View::Kind::XdgToplevel && v->xdg_toplevel) {
        wlr_box geo{};
        wlr_xdg_surface_get_geometry(v->xdg_toplevel->base, &geo);
        gx = geo.x;
        gy = geo.y;
      }
      const wlr_box src = {visible.x - r.content.x + gx, visible.y - r.content.y + gy, visible.width, visible.height};
      gs_paint_content(cr, v, src, visible.x, visible.y);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    gs_box_blur(cairo_image_surface_get_data(surf), sw, sh, cairo_image_surface_get_stride(surf), 3, 3);
    void* pixels = nullptr;
    size_t stride = 0;
    wlr_buffer* buf = create_pixel_buffer(sw, sh, &pixels, &stride);
    if (buf) {
      const unsigned char* s = cairo_image_surface_get_data(surf);
      const int sstride = cairo_image_surface_get_stride(surf);
      for (int y = 0; y < sh; ++y) std::memcpy(static_cast<unsigned char*>(pixels) + y * stride, s + static_cast<size_t>(y) * sstride, static_cast<size_t>(sw) * 4);
      out[i].buffer = buf;
      out[i].origin = E;
      out[i].w = sw;
      out[i].h = sh;
      any = true;
    }
    cairo_surface_destroy(surf);
  }
  if (wall) cairo_surface_destroy(wall);
  return any;
}

}  // namespace fleetwm
