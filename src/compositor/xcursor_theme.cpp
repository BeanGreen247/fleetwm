#include "xcursor_theme.hpp"

#include <cairo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "cursor_draw.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace fleetwm {

namespace {
void put32(std::vector<unsigned char>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xff));
}

constexpr uint32_t kImageType = 0xfffd0002;
}  // namespace

std::vector<unsigned char> xcursor_file(const std::vector<XcursorFrame>& frames) {
  std::vector<unsigned char> out;
  put32(out, 0x72756358);  // "Xcur"
  put32(out, 16);          // header size
  put32(out, 0x10000);     // version
  put32(out, static_cast<uint32_t>(frames.size()));
  uint32_t pos = 16 + 12 * static_cast<uint32_t>(frames.size());
  for (const XcursorFrame& f : frames) {
    put32(out, kImageType);
    put32(out, f.nominal_size);
    put32(out, pos);
    pos += 36 + 4 * f.width * f.height;
  }
  for (const XcursorFrame& f : frames) {
    put32(out, 36);  // chunk header size
    put32(out, kImageType);
    put32(out, f.nominal_size);
    put32(out, 1);   // chunk version
    put32(out, f.width);
    put32(out, f.height);
    put32(out, f.xhot);
    put32(out, f.yhot);
    put32(out, f.delay_ms);
    for (uint32_t px : f.argb) put32(out, px);
  }
  return out;
}

namespace {
// One shape at one scale, every frame of it (the busy ring turns in twelve).
void shape_frames(kit::CursorShape shape, bool glass, int scale, std::vector<XcursorFrame>* out) {
  const int size = kit::kCursorGrid * scale;
  const kit::CursorHotspot hot = kit::cursor_hotspot(shape);
  const int frames = kit::cursor_shape_spins(shape) ? kit::kBusyFrames : 1;
  for (int phase = 0; phase < frames; ++phase) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surf);
    kit::draw_cursor(cr, shape, glass, scale, phase);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    XcursorFrame f;
    f.nominal_size = static_cast<uint32_t>(size);
    f.width = f.height = static_cast<uint32_t>(size);
    f.xhot = static_cast<uint32_t>(hot.x * scale);
    f.yhot = static_cast<uint32_t>(hot.y * scale);
    f.delay_ms = frames > 1 ? 83 : 0;
    const auto* px = reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(surf));
    const int stride = cairo_image_surface_get_stride(surf) / 4;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) f.argb.push_back(px[y * stride + x]);
    cairo_surface_destroy(surf);
    out->push_back(std::move(f));
  }
}
}  // namespace

std::string install_xcursor_theme(bool glass, const std::string& inherits) {
  const std::string name = glass ? "fleetwm-aero" : "fleetwm-flat";
  const char* xdg = std::getenv("XDG_DATA_HOME");
  const char* home = std::getenv("HOME");
  const fs::path base = xdg && *xdg ? fs::path(xdg) : (home ? fs::path(home) / ".local/share" : fs::path());
  if (base.empty()) return "";
  const fs::path dir = base / "icons" / name;
  const std::string stamp = version_string() + "/" + inherits + "/v1";
  std::error_code ec;
  {
    std::ifstream in(dir / "STAMP");
    std::string have;
    if (in && std::getline(in, have) && have == stamp && fs::exists(dir / "cursors" / "left_ptr", ec)) return name;
  }
  fs::create_directories(dir / "cursors", ec);
  if (ec) return "";
  // each shape is drawn once at 32 and 64 pixels and written under every name that maps to it
  std::map<int, std::vector<unsigned char>> files;
  int count = 0;
  const kit::CursorAliasName* table = kit::cursor_alias_table(&count);
  for (int i = 0; i < count; ++i) {
    const int key = static_cast<int>(table[i].shape);
    if (!files.count(key)) {
      std::vector<XcursorFrame> frames;
      shape_frames(table[i].shape, glass, 1, &frames);
      shape_frames(table[i].shape, glass, 2, &frames);
      files[key] = xcursor_file(frames);
    }
    std::ofstream out(dir / "cursors" / table[i].name, std::ios::binary | std::ios::trunc);
    if (!out) return "";
    out.write(reinterpret_cast<const char*>(files[key].data()), static_cast<std::streamsize>(files[key].size()));
  }
  std::ofstream index(dir / "index.theme");
  index << "[Icon Theme]\nName=" << name << "\nComment=The Fleetwm pointer, written by the compositor\nInherits=" << (inherits.empty() ? "default" : inherits) << "\n";
  std::ofstream(dir / "STAMP") << stamp << "\n";
  return name;
}

}  // namespace fleetwm
