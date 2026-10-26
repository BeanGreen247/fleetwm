#include <gtest/gtest.h>
#include <jpeglib.h>
#include <zlib.h>

#include <cmath>
#include <cstring>
#include <random>

#include "vnc_codec.hpp"

using namespace fleetremote;

namespace {

// A reference Tight decoder for 32 bit true colour (3 byte TPIXELs): just enough to check the encoder's output.
struct TightDecoder {
  z_stream zs[4]{};
  bool init[4]{};
  ~TightDecoder() {
    for (int i = 0; i < 4; ++i)
      if (init[i]) inflateEnd(&zs[i]);
  }
  static size_t compact(const uint8_t*& p) {
    size_t n = *p & 0x7F;
    if (*p++ & 0x80) {
      n |= static_cast<size_t>(*p & 0x7F) << 7;
      if (*p++ & 0x80) n |= static_cast<size_t>(*p++) << 14;
    }
    return n;
  }
  std::vector<uint8_t> inflate_stream(int s, const uint8_t* data, size_t len, size_t want) {
    if (!init[s]) {
      inflateInit(&zs[s]);
      init[s] = true;
    }
    std::vector<uint8_t> out(want);
    zs[s].next_in = const_cast<uint8_t*>(data);
    zs[s].avail_in = static_cast<uInt>(len);
    zs[s].next_out = out.data();
    zs[s].avail_out = static_cast<uInt>(want);
    const int r = inflate(&zs[s], Z_SYNC_FLUSH);
    EXPECT_TRUE(r == Z_OK || r == Z_STREAM_END || r == Z_BUF_ERROR);
    EXPECT_EQ(zs[s].avail_out, 0u);
    return out;
  }
  // Decodes `rects` rectangles into a W*H 0x00RRGGBB picture.
  void decode(const std::vector<uint8_t>& buf, int rects, std::vector<uint32_t>& pic, int W) {
    const uint8_t* p = buf.data();
    for (int r = 0; r < rects; ++r) {
      const int x = (p[0] << 8) | p[1], y = (p[2] << 8) | p[3], w = (p[4] << 8) | p[5], h = (p[6] << 8) | p[7];
      const uint32_t enc = (uint32_t(p[8]) << 24) | (p[9] << 16) | (p[10] << 8) | p[11];
      p += 12;
      ASSERT_EQ(enc, 7u);
      const uint8_t ctl = *p++;
      auto put = [&](int px, int py, uint32_t v) { pic[static_cast<size_t>(y + py) * W + x + px] = v; };
      if (ctl == 0x80) {
        const uint32_t v = (uint32_t(p[0]) << 16) | (p[1] << 8) | p[2];
        p += 3;
        for (int j = 0; j < h; ++j)
          for (int i = 0; i < w; ++i) put(i, j, v);
      } else if (ctl == 0x90) {
        const size_t len = compact(p);
        jpeg_decompress_struct c;
        jpeg_error_mgr e;
        c.err = jpeg_std_error(&e);
        jpeg_create_decompress(&c);
        jpeg_mem_src(&c, p, len);
        jpeg_read_header(&c, TRUE);
        c.out_color_space = JCS_RGB;
        jpeg_start_decompress(&c);
        ASSERT_EQ(static_cast<int>(c.output_width), w);
        std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
        for (int j = 0; j < h; ++j) {
          JSAMPROW rp = row.data();
          jpeg_read_scanlines(&c, &rp, 1);
          for (int i = 0; i < w; ++i) put(i, j, (uint32_t(row[i * 3]) << 16) | (row[i * 3 + 1] << 8) | row[i * 3 + 2]);
        }
        jpeg_finish_decompress(&c);
        jpeg_destroy_decompress(&c);
        p += len;
      } else {
        ASSERT_EQ(ctl & 0x80, 0);
        const int stream = (ctl >> 4) & 3;
        const int filter = (ctl & 0x40) ? *p++ : 0;
        std::vector<uint32_t> pal;
        if (filter == 1) {
          const int np = *p++ + 1;
          for (int i = 0; i < np; ++i, p += 3) pal.push_back((uint32_t(p[0]) << 16) | (p[1] << 8) | p[2]);
        }
        const size_t bytes = filter == 1 ? (pal.size() == 2 ? static_cast<size_t>((w + 7) / 8) * h : static_cast<size_t>(w) * h)
                                         : static_cast<size_t>(w) * h * 3;
        std::vector<uint8_t> data;
        if (bytes < 12) {
          data.assign(p, p + bytes);
          p += bytes;
        } else {
          const size_t len = compact(p);
          data = inflate_stream(stream, p, len, bytes);
          p += len;
        }
        if (filter == 1) {
          for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
              const int idx = pal.size() == 2 ? (data[static_cast<size_t>(j) * ((w + 7) / 8) + i / 8] >> (7 - i % 8)) & 1 : data[static_cast<size_t>(j) * w + i];
              put(i, j, pal[static_cast<size_t>(idx)]);
            }
        } else {
          std::vector<uint8_t> px(data.size());
          const size_t stride = static_cast<size_t>(w) * 3;
          for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
              for (int c = 0; c < 3; ++c) {
                const size_t o = j * stride + static_cast<size_t>(i) * 3 + c;
                if (filter == 2) {
                  const int left = i ? px[o - 3] : 0, up = j ? px[o - stride] : 0, ul = (i && j) ? px[o - stride - 3] : 0;
                  px[o] = static_cast<uint8_t>(data[o] + std::clamp(left + up - ul, 0, 255));
                } else {
                  px[o] = data[o];
                }
              }
          for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
              const size_t o = j * stride + static_cast<size_t>(i) * 3;
              put(i, j, (uint32_t(px[o]) << 16) | (px[o + 1] << 8) | px[o + 2]);
            }
        }
      }
    }
    ASSERT_EQ(p, buf.data() + buf.size());
  }
};

// One row of `n` tiles filled by fn(x, y) -> 0x00RRGGBB.
template <class F>
std::vector<Tile> make_row(int n, F fn) {
  std::vector<Tile> v;
  for (int t = 0; t < n; ++t) {
    Tile tile;
    tile.x = t * kTile;
    tile.y = 0;
    tile.w = tile.h = kTile;
    tile.px.resize(kTile * kTile);
    for (int y = 0; y < kTile; ++y)
      for (int x = 0; x < kTile; ++x) tile.px[static_cast<size_t>(y) * kTile + x] = fn(tile.x + x, y);
    v.push_back(std::move(tile));
  }
  return v;
}

double psnr(const std::vector<Tile>& tiles, const std::vector<uint32_t>& pic, int W) {
  double se = 0;
  size_t n = 0;
  for (const Tile& t : tiles)
    for (int y = 0; y < t.h; ++y)
      for (int x = 0; x < t.w; ++x) {
        const uint32_t a = t.px[static_cast<size_t>(y) * t.w + x], b = pic[static_cast<size_t>(t.y + y) * W + t.x + x];
        for (int s : {16, 8, 0}) {
          const double d = static_cast<int>((a >> s) & 255) - static_cast<int>((b >> s) & 255);
          se += d * d;
          ++n;
        }
      }
  return se == 0 ? 99 : 10 * std::log10(255.0 * 255.0 / (se / static_cast<double>(n)));
}

}  // namespace

TEST(VncCodec, FlatColourIsOneFill) {
  PixelFormat pf;
  pf.build();
  auto tiles = make_row(4, [](int, int) { return 0x336699u; });
  TightEncoder enc;
  std::vector<uint8_t> out;
  EXPECT_EQ(enc.encode_run(pf, tiles.data(), tiles.size(), {}, out), 1);
  EXPECT_EQ(out.size(), 12u + 1 + 3);  // header, control byte, one TPIXEL
  std::vector<uint32_t> pic(4 * kTile * kTile);
  TightDecoder dec;
  dec.decode(out, 1, pic, 4 * kTile);
  for (const Tile& t : tiles) EXPECT_EQ(psnr({t}, pic, 4 * kTile), 99);
}

TEST(VncCodec, TextLikeTilesAreLosslessAndSmall) {
  PixelFormat pf;
  pf.build();
  std::mt19937 rng(7);
  auto tiles = make_row(6, [&](int x, int y) { return ((x * 7 + y * 3) % 11 < 4) ? 0x101010u : 0xF0F0F0u; });
  TightEncoder enc;
  std::vector<uint8_t> out;
  const int rects = enc.encode_run(pf, tiles.data(), tiles.size(), {}, out);
  std::vector<uint32_t> pic(6 * kTile * kTile);
  TightDecoder dec;
  dec.decode(out, rects, pic, 6 * kTile);
  EXPECT_EQ(psnr(tiles, pic, 6 * kTile), 99);
  EXPECT_LT(out.size(), 6u * kTile * kTile * 3 / 20);
}

TEST(VncCodec, ThirtyColourTilesStayLossless) {
  PixelFormat pf;
  pf.build();
  auto tiles = make_row(3, [](int x, int y) { return static_cast<uint32_t>(((x + y) % 30) * 8) * 0x010101u; });
  TightEncoder enc;
  std::vector<uint8_t> out;
  const int rects = enc.encode_run(pf, tiles.data(), tiles.size(), {}, out);
  std::vector<uint32_t> pic(3 * kTile * kTile);
  TightDecoder dec;
  dec.decode(out, rects, pic, 3 * kTile);
  EXPECT_EQ(psnr(tiles, pic, 3 * kTile), 99);
}

TEST(VncCodec, PhotographsGoThroughJpegAtLowQuality) {
  PixelFormat pf;
  pf.build();
  std::mt19937 rng(3);
  std::normal_distribution<double> noise(0, 2);
  auto fn = [&](int x, int y) {
    auto c = [&](double v) { return static_cast<uint32_t>(std::clamp(v + noise(rng), 0.0, 255.0)); };
    return (c(128 + 100 * std::sin(x / 31.0 + y / 47.0)) << 16) | (c(128 + 100 * std::sin(x / 19.0 - y / 53.0)) << 8) | c(128 + 100 * std::cos(x / 61.0 + y / 23.0));
  };
  auto tiles = make_row(8, fn);
  TightEncoder enc;
  TightEncoder::Options o;
  o.quality = 40;
  o.jpeg_quality = jpeg_quality_for(40);
  std::vector<uint8_t> out;
  const int rects = enc.encode_run(pf, tiles.data(), tiles.size(), o, out);
  EXPECT_EQ(rects, 1);
  EXPECT_EQ(out[12], 0x90);  // JPEG
  std::vector<uint32_t> pic(8 * kTile * kTile);
  TightDecoder dec;
  dec.decode(out, rects, pic, 8 * kTile);
  EXPECT_GT(psnr(tiles, pic, 8 * kTile), 28.0);
  EXPECT_LT(out.size(), 8u * kTile * kTile * 3 / 12);
}

TEST(VncCodec, WithoutJpegPhotographsAreLossless) {
  PixelFormat pf;
  pf.build();
  std::mt19937 rng(5);
  auto tiles = make_row(5, [&](int x, int y) { return static_cast<uint32_t>(rng() & 0xFFFFFF); });
  TightEncoder enc;
  TightEncoder::Options o;
  o.allow_jpeg = false;
  std::vector<uint8_t> out;
  const int rects = enc.encode_run(pf, tiles.data(), tiles.size(), o, out);
  std::vector<uint32_t> pic(5 * kTile * kTile);
  TightDecoder dec;
  dec.decode(out, rects, pic, 5 * kTile);
  EXPECT_EQ(psnr(tiles, pic, 5 * kTile), 99);
}

TEST(VncCodec, ZlibStreamsStayInStepAcrossRuns) {
  PixelFormat pf;
  pf.build();
  TightEncoder enc;
  TightDecoder dec;
  for (int round = 0; round < 4; ++round) {
    auto tiles = make_row(3, [&](int x, int y) { return ((x + y + round * 5) % 9 < 3) ? 0x202020u : 0xE0E0E0u; });
    std::vector<uint8_t> out;
    const int rects = enc.encode_run(pf, tiles.data(), tiles.size(), {}, out);
    std::vector<uint32_t> pic(3 * kTile * kTile);
    dec.decode(out, rects, pic, 3 * kTile);
    EXPECT_EQ(psnr(tiles, pic, 3 * kTile), 99) << "round " << round;
  }
}

TEST(VncCodec, JpegQualityMapping) {
  EXPECT_EQ(jpeg_quality_for(100), 95);
  EXPECT_LT(jpeg_quality_for(40), jpeg_quality_for(90));
  EXPECT_EQ(jpeg_quality_of_level(9), 100);
  EXPECT_EQ(jpeg_quality_of_level(0), 15);
}
