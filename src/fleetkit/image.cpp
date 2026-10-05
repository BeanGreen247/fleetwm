#include "image.hpp"

#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace fleetwm::kit {

namespace {

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

Image decode_png(const std::vector<uint8_t>& data) {
  Image img;
  png_image p;
  std::memset(&p, 0, sizeof p);
  p.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&p, data.data(), data.size())) return img;
  p.format = PNG_FORMAT_RGBA;
  std::vector<uint8_t> out(PNG_IMAGE_SIZE(p));
  if (!png_image_finish_read(&p, nullptr, out.data(), 0, nullptr)) {
    png_image_free(&p);
    return img;
  }
  img.width = static_cast<int>(p.width);
  img.height = static_cast<int>(p.height);
  img.rgba = std::move(out);
  return img;
}

struct JpegErr {
  jpeg_error_mgr pub;
  jmp_buf jump;
};

Image decode_jpeg(const std::vector<uint8_t>& data) {
  Image img;
  jpeg_decompress_struct cinfo;
  JpegErr err;
  cinfo.err = jpeg_std_error(&err.pub);
  err.pub.error_exit = [](j_common_ptr c) { longjmp(reinterpret_cast<JpegErr*>(c->err)->jump, 1); };
  err.pub.output_message = [](j_common_ptr) {};
  if (setjmp(err.jump)) {
    jpeg_destroy_decompress(&cinfo);
    return Image{};
  }
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, data.data(), data.size());
  jpeg_read_header(&cinfo, TRUE);
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);
  const int w = static_cast<int>(cinfo.output_width);
  const int h = static_cast<int>(cinfo.output_height);
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
  std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW rp = row.data();
    const int y = static_cast<int>(cinfo.output_scanline);
    jpeg_read_scanlines(&cinfo, &rp, 1);
    uint8_t* o = &rgba[static_cast<size_t>(y) * w * 4];
    for (int x = 0; x < w; ++x) {
      o[4 * x] = row[3 * x];
      o[4 * x + 1] = row[3 * x + 1];
      o[4 * x + 2] = row[3 * x + 2];
      o[4 * x + 3] = 255;
    }
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  img.width = w;
  img.height = h;
  img.rgba = std::move(rgba);
  return img;
}

Image decode_webp(const std::vector<uint8_t>& data) {
  Image img;
  int w = 0, h = 0;
  uint8_t* px = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
  if (!px) return img;
  img.width = w;
  img.height = h;
  img.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
  WebPFree(px);
  return img;
}

// Halves the image (2x2 box filter) until neither dimension is more than
// twice the target, so the final bilinear pass never skips source pixels
// (avoids aliasing on big photos downscaled to a screen).
Image halve(const Image& s) {
  Image d;
  d.width = std::max(1, s.width / 2);
  d.height = std::max(1, s.height / 2);
  d.rgba.resize(static_cast<size_t>(d.width) * d.height * 4);
  for (int y = 0; y < d.height; ++y) {
    const int y0 = std::min(2 * y, s.height - 1), y1 = std::min(2 * y + 1, s.height - 1);
    for (int x = 0; x < d.width; ++x) {
      const int x0 = std::min(2 * x, s.width - 1), x1 = std::min(2 * x + 1, s.width - 1);
      const uint8_t* a = &s.rgba[(static_cast<size_t>(y0) * s.width + x0) * 4];
      const uint8_t* b = &s.rgba[(static_cast<size_t>(y0) * s.width + x1) * 4];
      const uint8_t* c = &s.rgba[(static_cast<size_t>(y1) * s.width + x0) * 4];
      const uint8_t* e = &s.rgba[(static_cast<size_t>(y1) * s.width + x1) * 4];
      uint8_t* o = &d.rgba[(static_cast<size_t>(y) * d.width + x) * 4];
      for (int k = 0; k < 4; ++k) o[k] = static_cast<uint8_t>((a[k] + b[k] + c[k] + e[k] + 2) / 4);
    }
  }
  return d;
}

}  // namespace

Image load_image(const std::string& path) {
  const auto data = read_file(path);
  if (data.size() < 12) return {};
  if (!png_sig_cmp(data.data(), 0, 8)) return decode_png(data);
  if (data[0] == 0xFF && data[1] == 0xD8) return decode_jpeg(data);
  if (!std::memcmp(data.data(), "RIFF", 4) && !std::memcmp(data.data() + 8, "WEBP", 4))
    return decode_webp(data);
  return {};
}

void render_cover(const Image& src_in, int dst_w, int dst_h, uint8_t* dst) {
  const Image* src = &src_in;
  Image owned;
  // Scale needed to cover the target, then pre-shrink by 2x steps.
  double scale = std::max(static_cast<double>(dst_w) / src->width,
                          static_cast<double>(dst_h) / src->height);
  while (scale <= 0.5 && src->width > 1 && src->height > 1) {
    Image next = halve(*src);
    owned = std::move(next);
    src = &owned;
    scale *= 2.0;
  }
  const double sw = src->width * scale, sh = src->height * scale;
  const double ox = (sw - dst_w) / 2.0, oy = (sh - dst_h) / 2.0;
  const double inv = 1.0 / scale;
  for (int y = 0; y < dst_h; ++y) {
    const double fy = std::clamp((y + 0.5 + oy) * inv - 0.5, 0.0, src->height - 1.0);
    const int y0 = static_cast<int>(fy);
    const int y1 = std::min(y0 + 1, src->height - 1);
    const double wy = fy - y0;
    for (int x = 0; x < dst_w; ++x) {
      const double fx = std::clamp((x + 0.5 + ox) * inv - 0.5, 0.0, src->width - 1.0);
      const int x0 = static_cast<int>(fx);
      const int x1 = std::min(x0 + 1, src->width - 1);
      const double wx = fx - x0;
      const uint8_t* p00 = &src->rgba[(static_cast<size_t>(y0) * src->width + x0) * 4];
      const uint8_t* p10 = &src->rgba[(static_cast<size_t>(y0) * src->width + x1) * 4];
      const uint8_t* p01 = &src->rgba[(static_cast<size_t>(y1) * src->width + x0) * 4];
      const uint8_t* p11 = &src->rgba[(static_cast<size_t>(y1) * src->width + x1) * 4];
      uint8_t* o = dst + (static_cast<size_t>(y) * dst_w + x) * 4;
      double ch[3];
      for (int k = 0; k < 3; ++k) {
        const double top = p00[k] + (p10[k] - p00[k]) * wx;
        const double bot = p01[k] + (p11[k] - p01[k]) * wx;
        ch[k] = top + (bot - top) * wy;
      }
      o[0] = static_cast<uint8_t>(ch[2] + 0.5);  // B
      o[1] = static_cast<uint8_t>(ch[1] + 0.5);  // G
      o[2] = static_cast<uint8_t>(ch[0] + 0.5);  // R
      o[3] = 255;
    }
  }
}

}  // namespace fleetwm::kit
