#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "vnc_codec.hpp"
#include "net_util.hpp"
using namespace fleetremote;
using Clock = std::chrono::steady_clock;
static std::vector<Tile> tiles_of(const std::vector<uint32_t>& img, int W, int H, int rx, int ry, int rw, int rh) {
  std::vector<Tile> v;
  for (int ty = ry / 64 * 64; ty < ry + rh; ty += 64)
    for (int tx = rx / 64 * 64; tx < rx + rw; tx += 64) {
      Tile t; t.x = tx; t.y = ty; t.w = std::min(64, W - tx); t.h = std::min(64, H - ty);
      t.px.resize(size_t(t.w) * t.h);
      for (int y = 0; y < t.h; ++y) for (int x = 0; x < t.w; ++x) t.px[size_t(y) * t.w + x] = img[size_t(ty + y) * W + tx + x];
      v.push_back(std::move(t));
    }
  return v;
}
int main(int argc, char** argv) {
  std::string name = argv[1];
  int W, H; { std::ifstream d(name + ".dim"); d >> W >> H; }
  std::vector<uint32_t> img(size_t(W) * H);
  { std::ifstream f(name + ".raw", std::ios::binary); f.read(reinterpret_cast<char*>(img.data()), img.size() * 4); }
  struct Region { const char* label; int x, y, w, h; } regions[] = {{"full frame", 0, 0, W, H}, {"window 640x400", 200, 150, 640, 400}};
  PixelFormat pf; pf.build();
  for (auto& r : regions) {
    auto tiles = tiles_of(img, W, H, r.x, r.y, r.w, r.h);
    // runs = rows of tiles
    auto run_all = [&](auto fn) {
      size_t total = 0; auto t0 = Clock::now();
      const int reps = 5;
      for (int k = 0; k < reps; ++k) {
        total = 0;
        for (size_t i = 0; i < tiles.size();) { size_t j = i + 1; while (j < tiles.size() && tiles[j].y == tiles[i].y) ++j; total += fn(&tiles[i], j - i); i = j; }
      }
      double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / reps;
      return std::pair<size_t, double>(total, ms);
    };
    std::printf("%-10s %-15s", name.c_str(), r.label);
    {  // ZRLE like vnc.cpp: tiles of a run, one deflate stream, level 3
      z_stream zs{}; deflateInit(&zs, 3);
      auto res = run_all([&](const Tile* t, size_t n) {
        std::vector<uint8_t> payload, zb;
        for (size_t k = 0; k < n; ++k) zrle_tile(pf, t[k], 0, 0, t[k].w, t[k].h, payload);
        zb.resize(deflateBound(&zs, payload.size()) + 64);
        zs.next_in = payload.data(); zs.avail_in = payload.size(); zs.next_out = zb.data(); zs.avail_out = zb.size();
        deflate(&zs, Z_SYNC_FLUSH); return zb.size() - zs.avail_out + 16;
      });
      std::printf(" | ZRLE %7.1f KB %6.1f ms", res.first / 1024.0, res.second);
      deflateEnd(&zs);
    }
    for (int q : {95, 77, 47}) {
      TightEncoder te; TightEncoder::Options o; o.jpeg_quality = q; o.quality = q == 95 ? 100 : q == 77 ? 90 : 45; o.jpeg_optimize = !getenv("NOOPT");
      auto res = run_all([&](const Tile* t, size_t n) { std::vector<uint8_t> out; te.encode_run(pf, t, n, o, out); return out.size(); });
      std::printf(" | Tight jq%d %7.1f KB %6.1f ms", q, res.first / 1024.0, res.second);
    }
    std::printf("\n");
  }
}
