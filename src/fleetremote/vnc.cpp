#include "vnc.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/des.h>
#include <poll.h>
#include <sys/random.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "conn.hpp"
#include "net_util.hpp"
#include "scale.hpp"
#include "shadow.hpp"
#include "throttle.hpp"

namespace fleetremote {

namespace {

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

// Standard VNC DES: each key byte has its bits reversed.
std::array<uint8_t, 16> vnc_response(const std::string& password, const uint8_t challenge[16]) {
  DES_cblock key = {};
  for (size_t i = 0; i < 8 && i < password.size(); ++i) {
    uint8_t b = static_cast<uint8_t>(password[i]), r = 0;
    for (int bit = 0; bit < 8; ++bit) r |= ((b >> bit) & 1) << (7 - bit);
    key[i] = r;
  }
  DES_key_schedule ks;
  DES_set_key_unchecked(&key, &ks);
  std::array<uint8_t, 16> out{};
  for (int blk = 0; blk < 2; ++blk) {
    DES_cblock in, res;
    std::memcpy(in, challenge + 8 * blk, 8);
    DES_ecb_encrypt(&in, &res, &ks, DES_ENCRYPT);
    std::memcpy(out.data() + 8 * blk, res, 8);
  }
  return out;
}

// One ZRLE tile (<= 64x64): solid, packed palette (2..16 colours) or raw CPIXELs. The palette is built on the
// unconverted pixels (a new colour is looked up only when it differs from the one before) and only its entries are
// converted to the client's format; a tile of plain colour costs one pass of comparisons.
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

class VncClient {
 public:
  VncClient(Hub& hub, int fd, ServeOptions options)
      : hub_(hub), conn_(fd), opt_(std::move(options)), session_(hub, "vnc"), throttle_(session_.tuning) {
    deflateInit(&zs_, 3);
    zs_ok_ = true;
    conn_.count_into(&session_.tx, &session_.rx);
  }
  ~VncClient() {
    session_.cpu_ns = thread_cpu_ns();
    if (zs_ok_) deflateEnd(&zs_);
    if (std::getenv("FLEETWM_REMOTE_STATS"))
      std::fprintf(stderr, "fleetwm-remote: vnc viewer left: %llu bytes in %llu updates\n", static_cast<unsigned long long>(throttle_.total_bytes()),
                   static_cast<unsigned long long>(throttle_.updates()));
  }

  void run() {
    const int fd = conn_.fd();
    timeval tv{15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int one = 1, sndbuf = 256 * 1024;  // a small send buffer makes a slow link show up in how long a send takes
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    if (!handshake()) return;
    tv = {0, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);  // an idle viewer is fine once logged in
    loop();
  }

 private:
  bool fail_with(const char* msg, int v) {
    std::vector<uint8_t> res;
    put32be(res, 1);
    if (v >= 8) {
      put32be(res, static_cast<uint32_t>(std::strlen(msg)));
      res.insert(res.end(), msg, msg + std::strlen(msg));
    }
    conn_.write_all(res);
    return false;
  }

  // VeNCrypt 0.2, sub-type X509Vnc: TLS with our certificate, then the ordinary VNC password exchange inside it.
  bool vencrypt() {
    const uint8_t ver[2] = {0, 2};
    if (!conn_.write_all(ver, 2)) return false;
    uint8_t client[2];
    if (!conn_.read_exact(client, 2)) return false;
    if (client[0] != 0 || client[1] < 2) {
      const uint8_t no = 255;
      conn_.write_all(&no, 1);
      return false;
    }
    const uint8_t ok = 0;
    if (!conn_.write_all(&ok, 1)) return false;
    std::vector<uint8_t> list = {1};
    put32be(list, 261);  // X509Vnc
    if (!conn_.write_all(list)) return false;
    uint8_t chosen[4];
    if (!conn_.read_exact(chosen, 4)) return false;
    const uint32_t sub = (uint32_t(chosen[0]) << 24) | (chosen[1] << 16) | (chosen[2] << 8) | chosen[3];
    const uint8_t accept = sub == 261 ? 1 : 0;
    if (!conn_.write_all(&accept, 1) || !accept) return false;
    return conn_.start_tls(opt_.tls);
  }

  bool handshake() {
    if (!hub_.auth_allowed()) return false;
    if (!conn_.write_all("RFB 003.008\n", 12)) return false;
    char ver[12];
    if (!conn_.read_exact(ver, 12) || std::memcmp(ver, "RFB 003.", 8) != 0) return false;
    const int minor = (ver[8] - '0') * 100 + (ver[9] - '0') * 10 + (ver[10] - '0');
    const int v = minor >= 8 ? 8 : minor == 7 ? 7 : 3;
    const bool want_tls = opt_.tls != nullptr;
    if (v < 7 && want_tls) return false;  // RFB 3.3 cannot negotiate VeNCrypt
    if (v >= 7) {
      const uint8_t types[2] = {1, uint8_t(want_tls ? 19 : 2)};
      if (!conn_.write_all(types, 2)) return false;
      uint8_t chosen;
      if (!conn_.read_exact(&chosen, 1) || chosen != types[1]) return false;
    } else {
      std::vector<uint8_t> t;
      put32be(t, 2);
      if (!conn_.write_all(t)) return false;
    }
    if (want_tls && !vencrypt()) return false;

    uint8_t challenge[16], answer[16];
    if (getrandom(challenge, 16, 0) != 16) return false;
    if (!conn_.write_all(challenge, 16) || !conn_.read_exact(answer, 16)) return false;
    const auto expect = vnc_response(opt_.password, challenge);
    unsigned diff = 0;
    for (int i = 0; i < 16; ++i) diff |= expect[i] ^ answer[i];
    if (diff != 0 || opt_.password.empty()) {
      hub_.auth_failed();
      fail_with("Wrong password", v);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      return false;
    }
    std::vector<uint8_t> res;
    put32be(res, 0);
    if (!conn_.write_all(res)) return false;
    uint8_t shared;
    if (!conn_.read_exact(&shared, 1)) return false;

    for (int i = 0; i < 100 && !hub_.frame_size(&w_, &h_); ++i) {  // the capture starts when the first client joins
      if (session_.stop) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (w_ <= 0) return false;
    fit_size(w_, h_, session_.tuning.res_w.load(), session_.tuning.res_h.load(), &w_, &h_);  // the render resolution
    pf_.build();
    shadow_.reset(w_, h_);
    std::vector<uint8_t> init;
    put16be(init, w_);
    put16be(init, h_);
    append_pixel_format(init);
    const char name[] = "fleetwm";
    put32be(init, sizeof name - 1);
    init.insert(init.end(), name, name + sizeof name - 1);
    return conn_.write_all(init);
  }

  void append_pixel_format(std::vector<uint8_t>& o) const {
    o.push_back(uint8_t(pf_.bpp));
    o.push_back(uint8_t(pf_.depth));
    o.push_back(pf_.big_endian);
    o.push_back(pf_.true_colour);
    put16be(o, pf_.max[0]);
    put16be(o, pf_.max[1]);
    put16be(o, pf_.max[2]);
    o.push_back(uint8_t(pf_.shift[0]));
    o.push_back(uint8_t(pf_.shift[1]));
    o.push_back(uint8_t(pf_.shift[2]));
    o.insert(o.end(), 3, 0);
  }

  void loop() {
    const int fd = conn_.fd();
    bool want = false, maybe_dirty = true;
    while (!session_.stop) {
      int timeout = -1;
      if (want && maybe_dirty) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(throttle_.next_allowed() - Clock::now()).count();
        timeout = left > 0 ? static_cast<int>(left) + 1 : 0;
      }
      pollfd fds[2] = {{fd, POLLIN, 0}, {session_.wake_fd, POLLIN, 0}};
      const bool buffered = conn_.buffered();
      if (!buffered && poll(fds, 2, timeout) < 0) return;
      if (fds[1].revents & POLLIN) {
        uint64_t drain;
        [[maybe_unused]] ssize_t r = read(session_.wake_fd, &drain, sizeof drain);
        maybe_dirty = true;
      }
      if (!buffered && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) && !(fds[0].revents & POLLIN)) return;
      if (buffered || (fds[0].revents & POLLIN)) {
        do {
          if (!read_message(&want)) return;
        } while (conn_.buffered());
      }
      if (want && maybe_dirty && Clock::now() >= throttle_.next_allowed()) {
        bool sent = false;
        if (!send_update(&sent)) return;
        if (sent) want = false;
        maybe_dirty = false;
      }
      session_.cpu_ns = thread_cpu_ns();
    }
  }

  bool read_message(bool* want) {
    uint8_t type;
    if (!conn_.read_exact(&type, 1)) return false;
    switch (type) {
      case 0: {  // SetPixelFormat
        uint8_t b[19];
        if (!conn_.read_exact(b, 19)) return false;
        PixelFormat n;
        n.bpp = b[3];
        n.depth = b[4];
        n.big_endian = b[5];
        n.true_colour = b[6];
        n.max[0] = (b[7] << 8) | b[8];
        n.max[1] = (b[9] << 8) | b[10];
        n.max[2] = (b[11] << 8) | b[12];
        n.shift[0] = b[13];
        n.shift[1] = b[14];
        n.shift[2] = b[15];
        if (!n.true_colour || (n.bpp != 8 && n.bpp != 16 && n.bpp != 32) || n.max[0] < 1 || n.max[1] < 1 || n.max[2] < 1) return false;
        for (int c = 0; c < 3; ++c)
          if (n.shift[c] > 31 || n.max[c] > 255) return false;
        n.build();
        pf_ = n;
        shadow_.forget();
        hub_.mark_all_dirty(session_);
        return true;
      }
      case 2: {  // SetEncodings
        uint8_t b[3];
        if (!conn_.read_exact(b, 3)) return false;
        const int n = (b[1] << 8) | b[2];
        zrle_ = desktop_size_ = copyrect_ = false;
        for (int i = 0; i < n; ++i) {
          uint8_t e[4];
          if (!conn_.read_exact(e, 4)) return false;
          const int32_t enc = static_cast<int32_t>((uint32_t(e[0]) << 24) | (e[1] << 16) | (e[2] << 8) | e[3]);
          if (enc == 16) zrle_ = true;
          if (enc == 1) copyrect_ = true;
          if (enc == -223) desktop_size_ = true;
        }
        return true;
      }
      case 3: {  // FramebufferUpdateRequest
        uint8_t b[9];
        if (!conn_.read_exact(b, 9)) return false;
        if (!b[0]) {  // not incremental: the client lost its picture
          shadow_.forget();
          hub_.mark_all_dirty(session_);
        }
        *want = true;
        return true;
      }
      case 4: {  // KeyEvent
        uint8_t b[7];
        if (!conn_.read_exact(b, 7)) return false;
        InputEvent ev;
        ev.kind = InputEvent::KeySym;
        ev.down = b[0];
        ev.sym = (uint32_t(b[3]) << 24) | (b[4] << 16) | (b[5] << 8) | b[6];
        hub_.push_input(ev);
        return true;
      }
      case 5: {  // PointerEvent
        uint8_t b[5];
        if (!conn_.read_exact(b, 5)) return false;
        const int mask = b[0];
        int x = (b[1] << 8) | b[2], y = (b[3] << 8) | b[4];
        int nw, nh;  // the viewer's coordinates are in the shrunk picture, the compositor's in the screen's
        if (hub_.frame_size(&nw, &nh) && w_ > 0 && h_ > 0 && (nw != w_ || nh != h_)) {
          x = static_cast<int>(static_cast<int64_t>(x) * nw / w_);
          y = static_cast<int>(static_cast<int64_t>(y) * nh / h_);
        }
        InputEvent ev;
        ev.kind = InputEvent::Motion;
        ev.a = x;
        ev.b = y;
        hub_.push_input(ev);
        static const int kButtons[3] = {0x110, 0x112, 0x111};  // left, middle, right
        for (int i = 0; i < 3; ++i) {
          const bool now = mask & (1 << i), before = buttons_ & (1 << i);
          if (now != before) {
            InputEvent be;
            be.kind = InputEvent::Button;
            be.a = kButtons[i];
            be.down = now;
            hub_.push_input(be);
          }
        }
        for (int i = 3; i <= 4; ++i) {
          if ((mask & (1 << i)) && !(buttons_ & (1 << i))) {
            InputEvent we;
            we.kind = InputEvent::Wheel;
            we.a = i == 3 ? -1 : 1;
            hub_.push_input(we);
          }
        }
        buttons_ = mask;
        return true;
      }
      case 6: {  // ClientCutText: read and ignore
        uint8_t b[7];
        if (!conn_.read_exact(b, 7)) return false;
        uint32_t len = (uint32_t(b[3]) << 24) | (b[4] << 16) | (b[5] << 8) | b[6];
        if (len > (1u << 20)) return false;
        std::vector<uint8_t> skip(len);
        return conn_.read_exact(skip.data(), len);
      }
      default:
        return false;
    }
  }

  bool send_update(bool* sent) {
    TileBatch& batch = batch_;
    bool resized = false;
    int w, h;
    if (!hub_.take_dirty(session_, &batch, &w, &h, &resized, session_.tuning.res_w.load(), session_.tuning.res_h.load())) return true;
    std::vector<uint8_t> out;
    if (resized && (w != w_ || h != h_)) {
      if (!desktop_size_) return false;  // this viewer cannot follow a new screen size
      w_ = w;
      h_ = h;
      shadow_.reset(w, h);
      out = {0, 0, 0, 1};
      put16be(out, 0);
      put16be(out, 0);
      put16be(out, w);
      put16be(out, h);
      put32be(out, static_cast<uint32_t>(-223));
      *sent = true;
      hub_.mark_all_dirty(session_);  // the tiles taken above were discarded; send the new picture next round
      return conn_.write_all(out);
    }
    const auto started = Clock::now();
    const int bits = throttle_.quant_bits();
    const int depth = session_.tuning.depth.load();
    for (size_t i = 0; i < batch.n; ++i) {
      quantize_to_depth(batch.v[i].px.data(), batch.v[i].px.size(), depth);
      quantize_pixels(batch.v[i].px.data(), batch.v[i].px.size(), bits);
    }
    size_t kept = shadow_.drop_unchanged(batch);
    ScrollMove mv;
    bool moved = false;
    if (copyrect_ && kept >= 6 && shadow_.detect_scroll(batch, kept, &mv)) {
      shadow_.apply(mv);  // the picture on the viewer after CopyRect; what still differs is sent as usual
      kept = shadow_.drop_unchanged(batch);
      moved = true;
    }
    if (kept == 0 && !moved) return true;

    // CopyRect first (it reads the viewer's old picture), then one rectangle per run of neighbouring tiles in a row.
    std::vector<std::pair<size_t, size_t>> runs;
    std::sort(batch.v.begin(), batch.v.begin() + static_cast<std::ptrdiff_t>(kept),
              [](const Tile& a, const Tile& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    for (size_t i = 0; i < kept;) {
      size_t j = i + 1;
      while (j < kept && batch.v[j].y == batch.v[i].y && batch.v[j].x == batch.v[j - 1].x + kTile) ++j;
      runs.push_back({i, j});
      i = j;
    }
    out = {0, 0};
    put16be(out, static_cast<uint32_t>(runs.size() + (moved ? 1 : 0)));
    if (moved) {
      put16be(out, mv.x);
      put16be(out, mv.y);
      put16be(out, mv.w);
      put16be(out, mv.h);
      put32be(out, 1);  // CopyRect
      put16be(out, mv.x);
      put16be(out, mv.y - mv.dy);
    }
    if (zrle_) {
      const int level = session_.tuning.max_kbps.load() > 0 ? 6 : 3;  // a limited link is worth more deflate effort
      if (level != zlevel_) {
        deflateParams(&zs_, level, Z_DEFAULT_STRATEGY);
        zlevel_ = level;
      }
    }
    std::vector<uint8_t>& payload = payload_;
    for (auto [a, b] : runs) {
      const int rx = batch.v[a].x, ry = batch.v[a].y, rw = batch.v[b - 1].x + batch.v[b - 1].w - rx, rh = batch.v[a].h;
      put16be(out, rx);
      put16be(out, ry);
      put16be(out, rw);
      put16be(out, rh);
      payload.clear();
      if (zrle_) {
        put32be(out, 16);
        for (size_t k = a; k < b; ++k) zrle_tile(pf_, batch.v[k], 0, 0, batch.v[k].w, batch.v[k].h, payload);
        zbuf_.resize(deflateBound(&zs_, payload.size()) + 64);
        zs_.next_in = payload.data();
        zs_.avail_in = static_cast<uInt>(payload.size());
        zs_.next_out = zbuf_.data();
        zs_.avail_out = static_cast<uInt>(zbuf_.size());
        deflate(&zs_, Z_SYNC_FLUSH);
        const size_t zlen = zbuf_.size() - zs_.avail_out;
        put32be(out, static_cast<uint32_t>(zlen));
        out.insert(out.end(), zbuf_.begin(), zbuf_.begin() + static_cast<std::ptrdiff_t>(zlen));
      } else {
        put32be(out, 0);
        for (int row = 0; row < rh; ++row)
          for (size_t k = a; k < b; ++k)
            for (int x = 0; x < batch.v[k].w; ++x) pf_.put(out, pf_.convert(batch.v[k].px[static_cast<size_t>(row) * batch.v[k].w + x]));
      }
    }
    for (size_t k = 0; k < kept; ++k) shadow_.commit(batch.v[k]);
    trim_batch(batch);
    if (payload_.capacity() > 256 * 1024) std::vector<uint8_t>().swap(payload_);  // a full picture grew these; a scroll needs far less
    if (zbuf_.capacity() > 256 * 1024) std::vector<uint8_t>().swap(zbuf_);
    update_memory_estimate();
    *sent = true;
    const auto before_write = Clock::now();
    if (!conn_.write_all(out)) return false;
    throttle_.sent(out.size(), Clock::now() - before_write, started);
    return true;
  }

  // What this viewer's buffers hold (the screen copy, the tiles, the encoder's buffers and the deflate state).
  void update_memory_estimate() {
    size_t m = shadow_.bytes() + payload_.capacity() + zbuf_.capacity() + 300 * 1024;
    for (const Tile& t : batch_.v) m += t.px.capacity() * sizeof(uint32_t);
    session_.mem = m;
  }

  Hub& hub_;
  Conn conn_;
  ServeOptions opt_;
  Session session_;
  Throttle throttle_;
  PixelFormat pf_;
  Shadow shadow_;
  TileBatch batch_;
  std::vector<uint8_t> payload_, zbuf_;
  int w_ = 0, h_ = 0;
  bool zrle_ = false, desktop_size_ = false, copyrect_ = false;
  int buttons_ = 0, zlevel_ = 3;
  z_stream zs_{};
  bool zs_ok_ = false;
};

}  // namespace

void serve_vnc(Hub& hub, int fd, ServeOptions options) {
  VncClient client(hub, fd, std::move(options));
  client.run();
}

}  // namespace fleetremote
