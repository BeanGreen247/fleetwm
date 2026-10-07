#include "cursor.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <cairo.h>

extern "C" {
#include <wlr/types/wlr_buffer.h>
}

#include "pixel_buffer.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> icon_dirs() {
  std::vector<fs::path> dirs;
  if (const char* home = std::getenv("HOME")) {
    dirs.push_back(fs::path(home) / ".local/share/icons");
    dirs.push_back(fs::path(home) / ".icons");
  }
  dirs.push_back("/usr/local/share/icons");
  dirs.push_back("/usr/share/icons");
  dirs.push_back("/usr/share/pixmaps");
  return dirs;
}

}  // namespace

bool cursor_theme_has_left_ptr(const std::string& theme) {
  if (theme.empty()) return false;
  std::error_code ec;
  for (const fs::path& dir : icon_dirs())
    if (fs::exists(dir / theme / "cursors" / "left_ptr", ec)) return true;
  return false;
}

std::string pick_cursor_theme() {
  if (const char* env = std::getenv("XCURSOR_THEME"); env && *env) return env;
  for (const char* candidate : {"DMZ-White", "DMZ-Black", "Adwaita", "Breeze_Snow", "Breeze", "Bibata-Modern-Classic",
                                "Yaru", "Vanilla-DMZ", "whiteglass", "redglass", "default"})
    if (cursor_theme_has_left_ptr(candidate)) return candidate;
  return "";
}

wlr_buffer* create_fallback_cursor(int* hotspot_x, int* hotspot_y) {
  // '#' = black outline, '.' = white fill, ' ' = transparent. Drawn at 2x so it is
  // easy to see on a high-resolution screen.
  static const char* const kArrow[] = {
      "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ", "#....#      ",
      "#.....#     ", "#......#    ", "#.......#   ", "#........#  ", "#.....#####", "#..#..#     ",
      "#.# #..#    ", "##  #..#    ", "#    #..#   ", "     #..#   ", "      ##    ",
  };
  constexpr int kRows = static_cast<int>(sizeof(kArrow) / sizeof(kArrow[0])), kCols = 12, kScale = 2;
  void* mem = nullptr;
  size_t stride = 0;
  wlr_buffer* buffer = create_pixel_buffer(kCols * kScale, kRows * kScale, &mem, &stride);
  if (!buffer) return nullptr;
  for (int y = 0; y < kRows * kScale; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(mem) + y * stride);
    for (int x = 0; x < kCols * kScale; ++x) {
      const char c = kArrow[y / kScale][x / kScale];
      row[x] = c == '#' ? 0xFF000000u : c == '.' ? 0xFFFFFFFFu : 0x00000000u;
    }
  }
  *hotspot_x = 0;
  *hotspot_y = 0;
  return buffer;
}

kit::CursorCache<wlr_buffer*>& cursor_picture_cache() {
  static kit::CursorCache<wlr_buffer*> cache;
  return cache;
}

wlr_buffer* cursor_picture(kit::CursorShape shape, bool glass, int scale) {
  scale = scale < 1 ? 1 : scale > 4 ? 4 : scale;
  return cursor_picture_cache().get({shape, glass, scale}, [&]() -> wlr_buffer* {
    const int size = kit::kCursorGrid * scale;
    void* mem = nullptr;
    size_t stride = 0;
    wlr_buffer* buffer = create_pixel_buffer(size, size, &mem, &stride);
    if (!buffer) return nullptr;
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(mem), CAIRO_FORMAT_ARGB32, size, size,
                                                                static_cast<int>(stride));
    cairo_t* cr = cairo_create(surf);
    kit::draw_cursor(cr, shape, glass, scale);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return buffer;  // the cache owns this reference
  });
}

void cursor_pictures_clear() {
  cursor_picture_cache().clear([](wlr_buffer* b) { wlr_buffer_drop(b); });
}

}  // namespace fleetwm
