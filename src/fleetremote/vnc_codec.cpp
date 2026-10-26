#include "vnc_codec.hpp"

#include <jpeglib.h>

#include <algorithm>
#include <cstring>

#include "net_util.hpp"

namespace fleetremote {

void zrle_tile(const PixelFormat& pf, const Tile& t, int x0, int y0, int w, int h, std::vector<uint8_t>& out) {
  uint32_t pal[17];
  uint8_t idx[kTile * kTile];
  int npal = 0, last = 0;
  bool many = false;
  for (int y = 0; y < h && !many; ++y) {
    const uint32_t* row = &t.px[static_cast<size_t>(y0 + y) * t.w + x0];
    uint8_t* irow = idx + y * w;
    for (int x = 0; x < w; ++x) {
      const uint32_t v = row[x];
      if (npal && v == pal[last]) {
        irow[x] = static_cast<uint8_t>(last);
        continue;
      }
      int i = 0;
      while (i < npal && pal[i] != v) ++i;
      if (i == npal) {
        if (npal == 16) {
          many = true;
          break;
        }
        pal[npal++] = v;
      }
      last = i;
      irow[x] = static_cast<uint8_t>(i);
    }
  }
  if (many) {
    out.push_back(0);
    for (int y = 0; y < h; ++y) {
      const uint32_t* row = &t.px[static_cast<size_t>(y0 + y) * t.w + x0];
      for (int x = 0; x < w; ++x) pf.put_c(out, pf.convert(row[x]));
    }
    return;
  }
  if (npal == 1) {
    out.push_back(1);
    pf.put_c(out, pf.convert(pal[0]));
    return;
  }
  out.push_back(uint8_t(npal));
  for (int i = 0; i < npal; ++i) pf.put_c(out, pf.convert(pal[i]));
  const int bits = npal == 2 ? 1 : npal <= 4 ? 2 : 4;
  for (int y = 0; y < h; ++y) {
    const uint8_t* irow = idx + y * w;
    uint32_t acc = 0;
    int nbits = 0;
    for (int x = 0; x < w; ++x) {
      acc = (acc << bits) | irow[x];
      nbits += bits;
      if (nbits == 8) {
        out.push_back(uint8_t(acc));
        acc = 0;
        nbits = 0;
      }
    }
    if (nbits) out.push_back(uint8_t(acc << (8 - nbits)));
  }
}

int jpeg_quality_for(int q) { return std::clamp(static_cast<int>(10 + q * 0.85 + 0.5), 10, 95); }

int jpeg_quality_of_level(int level) {
  static const int kTable[10] = {15, 29, 41, 42, 62, 77, 79, 86, 92, 100};
  return kTable[std::clamp(level, 0, 9)];
}

namespace {

// Compact length of Tight: 1 to 3 bytes, 7 bits each, low bits first.
void put_compact(std::vector<uint8_t>& out, size_t n) {
  out.push_back(static_cast<uint8_t>(n & 0x7F) | (n > 0x7F ? 0x80 : 0));
  if (n > 0x7F) {
    out.push_back(static_cast<uint8_t>((n >> 7) & 0x7F) | (n > 0x3FFF ? 0x80 : 0));
    if (n > 0x3FFF) out.push_back(static_cast<uint8_t>(n >> 14));
  }
}

// A TPIXEL is 3 bytes (R, G, B) for 32 bits per pixel, depth 24 and 8 bits per colour; otherwise a whole pixel.
bool tpixel3(const PixelFormat& pf) { return pf.bpp == 32 && pf.depth == 24 && pf.max[0] == 255 && pf.max[1] == 255 && pf.max[2] == 255; }

void put_tpixel(const PixelFormat& pf, std::vector<uint8_t>& out, uint32_t xrgb) {
  if (tpixel3(pf)) {
    out.push_back(static_cast<uint8_t>(xrgb >> 16));
    out.push_back(static_cast<uint8_t>(xrgb >> 8));
    out.push_back(static_cast<uint8_t>(xrgb));
  } else {
    pf.put(out, pf.convert(xrgb));
  }
}

// Open-addressing palette over 0x00RRGGBB colours, at most 256 entries.
struct Palette {
  uint32_t key[512];
  uint16_t idx[512];
  bool used[512];
  uint32_t colour[256];
  int n = 0;
  uint16_t slot[256];  // hash slot of each entry, in insertion order, so the newest can be taken back
  Palette() { std::memset(used, 0, sizeof used); }
  // Takes back every entry added after the palette had `keep` entries.
  void rollback(int keep) {
    while (n > keep) used[slot[--n]] = false;
  }
  // Index of `v`, adding it when new; -1 when the palette is full.
  int lookup(uint32_t v) {
    uint32_t h = (v * 2654435761u) >> 23;  // 9 bits
    while (used[h]) {
      if (key[h] == v) return idx[h];
      h = (h + 1) & 511;
    }
    if (n == 256) return -1;
    used[h] = true;
    key[h] = v;
    idx[h] = static_cast<uint16_t>(n);
    colour[n] = v;
    slot[n] = static_cast<uint16_t>(h);
    return n++;
  }
};

// Distinct colours of a tile, counted up to 257 (more than 256 cannot use a palette).
int count_colours(const Tile& t, uint32_t* first) {
  *first = t.px.empty() ? 0 : t.px[0];
  Palette p;
  uint32_t last = *first;
  p.lookup(last);
  for (uint32_t v : t.px) {
    if (v == last) continue;  // runs of one colour are the common case
    if (p.lookup(v) < 0) return 257;
    last = v;
  }
  return p.n;
}

}  // namespace

TightEncoder::TightEncoder() = default;

TightEncoder::~TightEncoder() {
  for (int i = 0; i < 4; ++i)
    if (inited_[i]) deflateEnd(&zs_[i]);
  if (trial_inited_) deflateEnd(&trial_);
}

size_t TightEncoder::memory() const {
  size_t m = scratch_.capacity() + grad_.capacity() + zbuf_.capacity() + jpeg_.capacity() + rgb_.capacity();
  if (trial_inited_) m += 256 * 1024;
  for (int i = 0; i < 4; ++i)
    if (inited_[i]) m += 256 * 1024;
  return m;
}

void TightEncoder::deflate_into(int stream, const uint8_t* data, size_t len, int level, std::vector<uint8_t>& out) {
  z_stream& z = zs_[stream];
  if (!inited_[stream]) {
    deflateInit(&z, level);
    inited_[stream] = true;
    level_[stream] = level;
  } else if (level != level_[stream]) {
    deflateParams(&z, level, Z_DEFAULT_STRATEGY);
    level_[stream] = level;
  }
  zbuf_.resize(deflateBound(&z, len) + 64);
  z.next_in = const_cast<uint8_t*>(data);
  z.avail_in = static_cast<uInt>(len);
  z.next_out = zbuf_.data();
  z.avail_out = static_cast<uInt>(zbuf_.size());
  deflate(&z, Z_SYNC_FLUSH);
  const size_t zlen = zbuf_.size() - z.avail_out;
  put_compact(out, zlen);
  out.insert(out.end(), zbuf_.begin(), zbuf_.begin() + static_cast<std::ptrdiff_t>(zlen));
}

size_t TightEncoder::estimate_deflate(const uint8_t* data, size_t len) {
  if (!trial_inited_) {
    deflateInit(&trial_, 1);
    trial_inited_ = true;
  } else {
    deflateReset(&trial_);
  }
  zbuf_.resize(deflateBound(&trial_, len) + 64);
  trial_.next_in = const_cast<uint8_t*>(data);
  trial_.avail_in = static_cast<uInt>(len);
  trial_.next_out = zbuf_.data();
  trial_.avail_out = static_cast<uInt>(zbuf_.size());
  deflate(&trial_, Z_SYNC_FLUSH);
  return zbuf_.size() - trial_.avail_out;
}

void TightEncoder::make_jpeg(const Tile* tiles, size_t n, const Options& opt) {
  int w = 0;
  for (size_t i = 0; i < n; ++i) w += tiles[i].w;
  const int h = tiles[0].h;
  rgb_.resize(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    uint8_t* d = &rgb_[static_cast<size_t>(y) * w * 3];
    for (size_t i = 0; i < n; ++i) {
      const uint32_t* s = &tiles[i].px[static_cast<size_t>(y) * tiles[i].w];
      for (int x = 0; x < tiles[i].w; ++x) {
        const uint32_t v = s[x];
        *d++ = static_cast<uint8_t>(v >> 16);
        *d++ = static_cast<uint8_t>(v >> 8);
        *d++ = static_cast<uint8_t>(v);
      }
    }
  }
  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  unsigned char* buf = nullptr;
  unsigned long buflen = 0;
  jpeg_mem_dest(&cinfo, &buf, &buflen);
  cinfo.image_width = static_cast<JDIMENSION>(w);
  cinfo.image_height = static_cast<JDIMENSION>(h);
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, opt.jpeg_quality, TRUE);
  cinfo.dct_method = JDCT_IFAST;
  cinfo.optimize_coding = opt.jpeg_optimize ? TRUE : FALSE;
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = &rgb_[static_cast<size_t>(cinfo.next_scanline) * w * 3];
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_.assign(buf, buf + buflen);
  free(buf);
  jpeg_destroy_compress(&cinfo);
}

int TightEncoder::encode_run(const PixelFormat& pf, const Tile* tiles, size_t n, const Options& opt, std::vector<uint8_t>& out) {
  constexpr size_t kMaxTiles = 2048 / kTile;  // a Tight rectangle is at most 2048 wide
  // JPEG replaces the lossless version only when it is this much smaller; a high quality setting asks for more.
  const double need_ratio = 1.1 + 0.9 * opt.quality / 100.0;
  ncol_.resize(n);
  first_.resize(n);
  std::vector<int>& ncol = ncol_;
  std::vector<uint32_t>& first = first_;
  for (size_t i = 0; i < n; ++i) {
    ncol[i] = count_colours(tiles[i], &first[i]);
  }

  int rects = 0;
  size_t i = 0;
  while (i < n) {
    const bool photo = ncol[i] > 256;
    size_t j = i + 1;
    Palette pal;
    bool solid = !photo && ncol[i] == 1;
    if (!photo) {
      for (uint32_t v : tiles[i].px) pal.lookup(v);
      while (j < n && j - i < kMaxTiles && ncol[j] <= 256) {
        const int keep = pal.n;  // would the union stay within 256 colours?
        bool ok = true;
        for (uint32_t v : tiles[j].px)
          if (pal.lookup(v) < 0) {
            ok = false;
            break;
          }
        if (!ok) {
          pal.rollback(keep);
          break;
        }
        if (solid && !(ncol[j] == 1 && first[j] == first[i])) solid = false;
        ++j;
      }
    } else {
      while (j < n && j - i < kMaxTiles && ncol[j] > 256) {
        ++j;
      }
    }
    int w = 0;
    for (size_t k = i; k < j; ++k) w += tiles[k].w;
    const int h = tiles[i].h;
    const bool can_jpeg = opt.allow_jpeg;
    const bool grad_ok = tpixel3(pf);
    put16be(out, static_cast<uint32_t>(tiles[i].x));
    put16be(out, static_cast<uint32_t>(tiles[i].y));
    put16be(out, static_cast<uint32_t>(w));
    put16be(out, static_cast<uint32_t>(h));
    put32be(out, 7);
    ++rects;

    if (solid) {
      out.push_back(0x80);
      put_tpixel(pf, out, first[i]);
      i = j;
      continue;
    }

    const int np = photo ? 0 : pal.n;
    // Fills scratch_ with tiles [a, b) as a lossless rectangle would carry them: palette indices (1 bit each for two
    // colours, else 8), or for a photograph the TPIXELs through the gradient filter.
    auto build_lossless = [&](size_t a, size_t b) {
      scratch_.clear();
      int rw = 0;
      for (size_t k = a; k < b; ++k) rw += tiles[k].w;
      if (!photo) {
        for (int y = 0; y < h; ++y) {
          uint32_t acc = 0;
          int nbits = 0;
          uint32_t prev = 0;
          int prev_idx = -1;
          for (size_t k = a; k < b; ++k) {
            const uint32_t* row = &tiles[k].px[static_cast<size_t>(y) * tiles[k].w];
            for (int x = 0; x < tiles[k].w; ++x) {
              const int idx = (prev_idx >= 0 && row[x] == prev) ? prev_idx : pal.lookup(row[x]);  // runs of one colour skip the hash
              prev = row[x];
              prev_idx = idx;
              if (np == 2) {
                acc = (acc << 1) | static_cast<uint32_t>(idx);
                if (++nbits == 8) {
                  scratch_.push_back(static_cast<uint8_t>(acc));
                  acc = 0;
                  nbits = 0;
                }
              } else {
                scratch_.push_back(static_cast<uint8_t>(idx));
              }
            }
          }
          if (np == 2 && nbits) scratch_.push_back(static_cast<uint8_t>(acc << (8 - nbits)));
        }
        return;
      }
      std::vector<uint8_t>& px = grad_;
      px.clear();
      for (int y = 0; y < h; ++y)
        for (size_t k = a; k < b; ++k)
          for (int x = 0; x < tiles[k].w; ++x) put_tpixel(pf, px, tiles[k].px[static_cast<size_t>(y) * tiles[k].w + x]);
      if (!grad_ok) {
        scratch_ = px;
        return;
      }
      scratch_.resize(px.size());
      const size_t stride = static_cast<size_t>(rw) * 3;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < rw; ++x)
          for (int c = 0; c < 3; ++c) {
            const size_t o = y * stride + static_cast<size_t>(x) * 3 + c;
            const int left = x ? px[o - 3] : 0, up = y ? px[o - stride] : 0, ul = (x && y) ? px[o - stride - 3] : 0;
            scratch_[o] = static_cast<uint8_t>(px[o] - std::clamp(left + up - ul, 0, 255));
          }
    };

    // Text and flat UI (a few dozen colours) stay lossless without asking JPEG. Anything richer is judged on one sample
    // tile, the richest of the rectangle: lossless and JPEG are both tried on it and JPEG wins when clearly smaller. At a
    // low quality setting a photograph goes straight to JPEG.
    bool use_jpeg = false;
    if (can_jpeg && !(!photo && np <= 48)) {
      if (photo && opt.quality < 75) {
        use_jpeg = true;
      } else {
        if (jpeg_hdr_q_ != opt.jpeg_quality) {
          Tile flat_tile;
          flat_tile.w = flat_tile.h = kTile;
          flat_tile.px.assign(static_cast<size_t>(kTile) * kTile, 0x808080);
          Options hdr_opt = opt;
          hdr_opt.jpeg_optimize = false;
          make_jpeg(&flat_tile, 1, hdr_opt);
          jpeg_hdr_ = jpeg_.size();
          jpeg_hdr_q_ = opt.jpeg_quality;
        }
        Options trial_opt = opt;
        trial_opt.jpeg_optimize = false;  // the verdict does not need the smallest tables; only a chosen rectangle gets them
        // Two samples must agree: the richest tile (a photograph in a window of text still counts) and the median one
        // (a text window with one gradient tile must not turn into a blur).
        std::vector<size_t>& order = order_;
        order.clear();
        for (size_t k = i; k < j; ++k) order.push_back(k);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ncol[a] < ncol[b]; });
        use_jpeg = true;
        for (size_t s : {order.back(), order[order.size() / 2]}) {
          build_lossless(s, s + 1);
          const size_t lossless = scratch_.size() < 12 ? scratch_.size() : estimate_deflate(scratch_.data(), scratch_.size());
          make_jpeg(&tiles[s], 1, trial_opt);
          const size_t lossy = jpeg_.size() > jpeg_hdr_ ? jpeg_.size() - jpeg_hdr_ : 1;
          if (static_cast<double>(lossless) <= need_ratio * static_cast<double>(lossy)) {
            use_jpeg = false;
            break;
          }
          if (order.size() == 1) break;
        }
      }
    }
    if (use_jpeg) make_jpeg(&tiles[i], j - i, opt);
    else build_lossless(i, j);
    if (use_jpeg) {
      out.push_back(0x90);
      put_compact(out, jpeg_.size());
      out.insert(out.end(), jpeg_.begin(), jpeg_.end());
    } else if (!photo) {
      const int stream = np == 2 ? 1 : 2;
      out.push_back(static_cast<uint8_t>(0x40 | (stream << 4)));
      out.push_back(1);  // PaletteFilter
      out.push_back(static_cast<uint8_t>(np - 1));
      for (int c = 0; c < np; ++c) put_tpixel(pf, out, pal.colour[c]);
      if (scratch_.size() < 12) out.insert(out.end(), scratch_.begin(), scratch_.end());
      else deflate_into(stream, scratch_.data(), scratch_.size(), opt.zlib_level, out);
    } else if (grad_ok) {
      out.push_back(0x40 | (3 << 4));
      out.push_back(2);  // GradientFilter
      if (scratch_.size() < 12) out.insert(out.end(), scratch_.begin(), scratch_.end());
      else deflate_into(3, scratch_.data(), scratch_.size(), opt.zlib_level, out);
    } else {
      out.push_back(0x00);  // CopyFilter, stream 0
      if (scratch_.size() < 12) out.insert(out.end(), scratch_.begin(), scratch_.end());
      else deflate_into(0, scratch_.data(), scratch_.size(), opt.zlib_level, out);
    }
    i = j;
  }
  return rects;
}

}  // namespace fleetremote
