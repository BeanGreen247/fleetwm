#include "window_icon.hpp"

#include <cairo.h>

#include "config.h"

#if FLEETWM_XWAYLAND
#include <xcb/xcb.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace fleetwm {

#if FLEETWM_XWAYLAND
namespace {
xcb_connection_t* connection(const char* display_name) {
  static std::map<std::string, xcb_connection_t*> conns;
  if (!display_name) return nullptr;
  auto it = conns.find(display_name);
  if (it != conns.end()) {
    if (it->second && xcb_connection_has_error(it->second)) {
      xcb_disconnect(it->second);
      conns.erase(it);
    } else {
      return it->second;
    }
  }
  xcb_connection_t* c = xcb_connect(display_name, nullptr);
  if (!c || xcb_connection_has_error(c)) {
    if (c) xcb_disconnect(c);
    return nullptr;
  }
  conns[display_name] = c;
  return c;
}

}  // namespace
#endif

namespace {
std::string cache_dir() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  std::string base = xdg && *xdg ? xdg : (home ? std::string(home) + "/.cache" : "");
  return base.empty() ? "" : base + "/fleetwm/window-icons";
}
}  // namespace

const char* window_icon_dir() {
  static const std::string dir = cache_dir();
  return dir.c_str();
}

bool export_x11_window_icon(const char* display_name, uint32_t x11_window, uint32_t view_id) {
#if !FLEETWM_XWAYLAND
  (void)display_name; (void)x11_window; (void)view_id;
  return false;
#else
  xcb_connection_t* c = connection(display_name);
  const std::string dir = cache_dir();
  if (!c || dir.empty()) return false;
  xcb_intern_atom_reply_t* a = xcb_intern_atom_reply(c, xcb_intern_atom(c, 1, 12, "_NET_WM_ICON"), nullptr);
  if (!a || a->atom == XCB_ATOM_NONE) {
    std::free(a);
    return false;
  }
  const xcb_atom_t atom = a->atom;
  std::free(a);
  auto read_icon = [&](xcb_window_t w) {
    return xcb_get_property_reply(c, xcb_get_property(c, 0, w, atom, XCB_ATOM_CARDINAL, 0, 1u << 20), nullptr);
  };
  xcb_get_property_reply_t* r = read_icon(x11_window);
  if (r && xcb_get_property_value_length(r) == 0) {
    // Wine keeps the icon on one of the program's other top-level windows (an owner or a hidden frame), so look at the others of the same process.
    auto pid_of = [&](xcb_window_t w) -> uint32_t {
      xcb_intern_atom_reply_t* pa = xcb_intern_atom_reply(c, xcb_intern_atom(c, 1, 11, "_NET_WM_PID"), nullptr);
      if (!pa) return 0;
      xcb_get_property_reply_t* pr = xcb_get_property_reply(c, xcb_get_property(c, 0, w, pa->atom, XCB_ATOM_CARDINAL, 0, 1), nullptr);
      std::free(pa);
      uint32_t pid = 0;
      if (pr && xcb_get_property_value_length(pr) >= 4) pid = *static_cast<const uint32_t*>(xcb_get_property_value(pr));
      std::free(pr);
      return pid;
    };
    const uint32_t pid = pid_of(x11_window);
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_query_tree_reply_t* tree = pid && screen ? xcb_query_tree_reply(c, xcb_query_tree(c, screen->root), nullptr) : nullptr;
    if (tree) {
      const xcb_window_t* kids = xcb_query_tree_children(tree);
      for (int i = 0; i < xcb_query_tree_children_length(tree); ++i) {
        if (kids[i] == x11_window || pid_of(kids[i]) != pid) continue;
        xcb_get_property_reply_t* other = read_icon(kids[i]);
        if (other && xcb_get_property_value_length(other) > 0) {
          std::free(r);
          r = other;
          break;
        }
        std::free(other);
      }
      std::free(tree);
    }
  }
  if (!r) return false;
  const uint32_t* data = static_cast<const uint32_t*>(xcb_get_property_value(r));
  const size_t count = static_cast<size_t>(xcb_get_property_value_length(r)) / 4;
  // the list is width, height, width*height ARGB words, repeated: take the one nearest 48 px, preferring bigger
  size_t best = 0, best_w = 0, best_h = 0;
  for (size_t i = 0; i + 2 <= count;) {
    const size_t w = data[i], h = data[i + 1];
    if (w == 0 || h == 0 || w > 1024 || h > 1024 || i + 2 + w * h > count) break;
    const auto score = [](size_t width) { return width >= 48 ? width - 48 : (48 - width) * 4; };
    if (best_w == 0 || score(w) < score(best_w)) {
      best = i + 2;
      best_w = w;
      best_h = h;
    }
    i += 2 + w * h;
  }
  bool ok = false;
  if (best_w) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(best_w), static_cast<int>(best_h));
    unsigned char* px = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (size_t y = 0; y < best_h; ++y) {
      uint32_t* row = reinterpret_cast<uint32_t*>(px + y * stride);
      for (size_t x = 0; x < best_w; ++x) {
        const uint32_t v = data[best + y * best_w + x];  // not premultiplied; cairo wants it premultiplied
        const uint32_t al = v >> 24;
        const uint32_t rr = ((v >> 16) & 0xff) * al / 255, gg = ((v >> 8) & 0xff) * al / 255, bb = (v & 0xff) * al / 255;
        row[x] = (al << 24) | (rr << 16) | (gg << 8) | bb;
      }
    }
    cairo_surface_mark_dirty(s);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string tmp = dir + "/." + std::to_string(view_id) + ".png";
    const std::string path = dir + "/" + std::to_string(view_id) + ".png";
    if (cairo_surface_write_to_png(s, tmp.c_str()) == CAIRO_STATUS_SUCCESS) {
      fs::rename(tmp, path, ec);
      ok = !ec;
    }
    cairo_surface_destroy(s);
  }
  std::free(r);
  return ok;
#endif
}

void remove_window_icon(uint32_t view_id) {
  const std::string dir = cache_dir();
  if (dir.empty()) return;
  std::error_code ec;
  fs::remove(dir + "/" + std::to_string(view_id) + ".png", ec);
  fs::remove(dir + "/" + std::to_string(view_id) + ".name", ec);
}

bool write_window_icon_argb(uint32_t view_id, int width, int height, int stride, const unsigned char* argb) {
  const std::string dir = cache_dir();
  if (dir.empty() || width < 8 || height < 8 || width > 1024 || height > 1024 || !argb) return false;
  cairo_surface_t* s = cairo_image_surface_create_for_data(const_cast<unsigned char*>(argb), CAIRO_FORMAT_ARGB32, width, height, stride);
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string tmp = dir + "/." + std::to_string(view_id) + ".png";
  const std::string path = dir + "/" + std::to_string(view_id) + ".png";
  bool ok = false;
  if (cairo_surface_write_to_png(s, tmp.c_str()) == CAIRO_STATUS_SUCCESS) {
    fs::rename(tmp, path, ec);
    ok = !ec;
  }
  cairo_surface_destroy(s);
  return ok;
}

void write_window_icon_name(uint32_t view_id, const char* name) {
  const std::string dir = cache_dir();
  if (dir.empty() || !name || !*name) return;
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::FILE* f = std::fopen((dir + "/" + std::to_string(view_id) + ".name").c_str(), "w");
  if (!f) return;
  std::fputs(name, f);
  std::fclose(f);
}

}  // namespace fleetwm
