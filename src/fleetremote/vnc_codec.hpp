#pragma once

// The picture encoders of the VNC server, apart from the connection so tests and benchmarks can run them: the pixel
// format a viewer asked for, ZRLE tiles, and Tight rectangles (lossless palette/fill for text and flat colour, JPEG
// for photographs, gradients and video). Pixels are 0x00RRGGBB as captured.

#include <zlib.h>

#include <array>
#include <cstdint>
#include <vector>

#include "hub.hpp"

namespace fleetremote {

struct PixelFormat {
  int bpp = 32, depth = 24;
  bool big_endian = false, true_colour = true;
  int max[3] = {255, 255, 255};  // r g b
  int shift[3] = {16, 8, 0};
  std::array<std::array<uint32_t, 256>, 3> lut{};

  void build() {
    for (int c = 0; c < 3; ++c)
      for (int v = 0; v < 256; ++v) lut[c][v] = (static_cast<uint32_t>(v * max[c] + 127) / 255) << shift[c];
  }
  uint32_t convert(uint32_t xrgb) const { return lut[0][(xrgb >> 16) & 0xFF] | lut[1][(xrgb >> 8) & 0xFF] | lut[2][xrgb & 0xFF]; }
  // ZRLE sends 3 bytes per pixel when the format is 32 bits with all colour bits in the low (or high) three bytes.
  bool cpixel3() const {
    if (bpp != 32 || depth > 24) return false;
    uint32_t used = 0;
    for (int c = 0; c < 3; ++c) used |= static_cast<uint32_t>(max[c]) << shift[c];
    return (used & 0xFF000000u) == 0 || (used & 0x000000FFu) == 0;
  }
  bool cpixel_high() const {  // the three bytes are the high ones
    uint32_t used = 0;
    for (int c = 0; c < 3; ++c) used |= static_cast<uint32_t>(max[c]) << shift[c];
    return (used & 0xFF000000u) != 0;
  }
  void put(std::vector<uint8_t>& out, uint32_t v) const {
    const int n = bpp / 8;
    for (int i = 0; i < n; ++i) out.push_back(uint8_t(big_endian ? v >> (8 * (n - 1 - i)) : v >> (8 * i)));
  }
  void put_c(std::vector<uint8_t>& out, uint32_t v) const {
    if (!cpixel3()) return put(out, v);
    uint8_t wire[4];
    for (int i = 0; i < 4; ++i) wire[i] = uint8_t(big_endian ? v >> (8 * (3 - i)) : v >> (8 * i));
    const bool drop_first = big_endian != cpixel_high();  // the unused byte is the zero one
    out.insert(out.end(), wire + (drop_first ? 1 : 0), wire + (drop_first ? 4 : 3));
  }
};


// One ZRLE tile (<= 64x64): solid, packed palette (2..16 colours) or raw CPIXELs. The palette is built on the
// unconverted pixels (a new colour is looked up only when it differs from the one before) and only its entries are
// converted to the client's format; a tile of plain colour costs one pass of comparisons.
void zrle_tile(const PixelFormat& pf, const Tile& t, int x0, int y0, int w, int h, std::vector<uint8_t>& out);

// JPEG quality (1..100) for a quality slider value: 100 -> 95, 40 -> 47, 10 -> 19.
int jpeg_quality_for(int slider_quality);

// The JPEG quality a viewer's quality-level pseudo-encoding (-32 + level, level 0..9) stands for.
int jpeg_quality_of_level(int level);

// Tight encoding (RFB encoding 7). Four persistent zlib streams, one per kind of data, as the viewer keeps them.
class TightEncoder {
 public:
  struct Options {
    bool allow_jpeg = true;  // the viewer sent a JPEG quality level and the pixel format has 16 or 32 bits
    int jpeg_quality = 70;
    int quality = 90;          // the quality slider: how much smaller the lossy JPEG must be before it replaces lossless
    int zlib_level = 3;
    bool jpeg_optimize = true;  // optimized Huffman tables: a smaller picture for a few ms more
  };
  TightEncoder();
  ~TightEncoder();
  TightEncoder(const TightEncoder&) = delete;
  TightEncoder& operator=(const TightEncoder&) = delete;

  // `n` tiles side by side in one row (same y, each starting where the last ended). Appends whole rectangles
  // (header, encoding 7 and data) to `out` and returns how many.
  int encode_run(const PixelFormat& pf, const Tile* tiles, size_t n, const Options& opt, std::vector<uint8_t>& out);

  // Bytes held by the buffers and zlib streams, for the viewer's memory figure.
  size_t memory() const;

 private:
  void deflate_into(int stream, const uint8_t* data, size_t len, int level, std::vector<uint8_t>& out);
  // Encodes the tiles as JPEG into jpeg_.
  void make_jpeg(const Tile* tiles, size_t n, const Options& opt);
  // How many bytes `data` takes through zlib, measured on a scratch stream so the persistent ones stay in step with the viewer.
  size_t estimate_deflate(const uint8_t* data, size_t len);
  z_stream zs_[4]{}, trial_{};
  bool trial_inited_ = false;
  int jpeg_hdr_q_ = -1;     // the JPEG quality jpeg_hdr_ was measured for
  size_t jpeg_hdr_ = 0;     // bytes a JPEG of a flat 64x64 tile takes: its tables and headers, which a long rectangle shares
  bool inited_[4]{};
  int level_[4]{3, 3, 3, 3};
  std::vector<uint8_t> scratch_, grad_, zbuf_, jpeg_, rgb_;
  std::vector<int> ncol_;       // per tile of the run being encoded; kept so a busy desktop does not allocate every update
  std::vector<uint32_t> first_;
  std::vector<size_t> order_;
};

}  // namespace fleetremote
