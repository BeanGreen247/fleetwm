#include "rdp.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#include "conn.hpp"
#include "net_util.hpp"
#include "scale.hpp"
#include "shadow.hpp"
#include "throttle.hpp"

namespace fleetremote {

namespace {

using Buf = std::vector<uint8_t>;
constexpr uint16_t kServerUser = 1002;  // MCS user id handed to the client (1001 is the server)
constexpr uint16_t kGlobalChannel = 1003;
constexpr uint32_t kShareId = 0x000103EA;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint16_t rd16be(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

std::string utf16_to_utf8(const uint8_t* p, size_t bytes) {
  std::string out;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    uint32_t c = rd16(p + i);
    if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
      const uint32_t lo = rd16(p + i + 2);
      if (lo >= 0xDC00 && lo < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    if (c < 0x80) out += char(c);
    else if (c < 0x800) out += {char(0xC0 | (c >> 6)), char(0x80 | (c & 0x3F))};
    else if (c < 0x10000) out += {char(0xE0 | (c >> 12)), char(0x80 | ((c >> 6) & 0x3F)), char(0x80 | (c & 0x3F))};
    else out += {char(0xF0 | (c >> 18)), char(0x80 | ((c >> 12) & 0x3F)), char(0x80 | ((c >> 6) & 0x3F)), char(0x80 | (c & 0x3F))};
  }
  return out;
}

// Scancode (set 1) -> Linux evdev code.
uint32_t scancode_to_evdev(uint32_t code, bool extended) {
  if (extended) {
    switch (code) {
      case 0x1C: return 96;   // keypad enter
      case 0x1D: return 97;   // right ctrl
      case 0x35: return 98;   // keypad /
      case 0x37: return 99;   // print screen
      case 0x38: return 100;  // right alt
      case 0x46: return 119;  // pause
      case 0x47: return 102;  // home
      case 0x48: return 103;  // up
      case 0x49: return 104;  // page up
      case 0x4B: return 105;  // left
      case 0x4D: return 106;  // right
      case 0x4F: return 107;  // end
      case 0x50: return 108;  // down
      case 0x51: return 109;  // page down
      case 0x52: return 110;  // insert
      case 0x53: return 111;  // delete
      case 0x5B: return 125;  // left super
      case 0x5C: return 126;  // right super
      case 0x5D: return 127;  // menu
      default: return 0;
    }
  }
  return code > 0 && code < 0x80 ? code : 0;
}

class RdpClient {
 public:
  RdpClient(Hub& hub, int fd, ServeOptions options)
      : hub_(hub), conn_(fd), opt_(std::move(options)), session_(hub, "rdp"), throttle_(session_.tuning) {
    conn_.count_into(&session_.tx, &session_.rx);
  }
  ~RdpClient() {
    session_.cpu_ns = thread_cpu_ns();
    if (std::getenv("FLEETWM_REMOTE_STATS"))
      std::fprintf(stderr, "fleetwm-remote: rdp viewer left: %llu bytes in %llu updates\n", static_cast<unsigned long long>(throttle_.total_bytes()),
                   static_cast<unsigned long long>(throttle_.updates()));
  }

  void run() {
    const int fd = conn_.fd();
    timeval tv{15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int one = 1, sndbuf = 256 * 1024;  // a small send buffer makes a slow link show up in how long a send takes
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    peer_ = peer_address(fd);
    if (!hub_.auth_allowed()) {
      remote_log("rdp %s: refused, too many failed logins in the last minute", peer_.c_str());
      return;
    }
    if (!negotiate()) return;
    if (!connect_mcs()) {
      remote_log("rdp %s: the client stopped before the connection settings arrived", peer_.c_str());
      return;
    }
    tv = {0, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    loop();
  }

 private:
  // ---- transport ----
  bool tls_read(void* buf, size_t n) { return conn_.read_exact(buf, n); }
  bool tls_write(const Buf& b) { return conn_.write_all(b); }

  // One PDU from the TLS stream: a TPKT packet (the whole packet, header included) or a fast-path packet.
  bool read_pdu(Buf* pdu, bool* fast_path) {
    uint8_t b0;
    if (!tls_read(&b0, 1)) return false;
    pdu->assign(1, b0);
    if (b0 == 0x03) {
      uint8_t h[3];
      if (!tls_read(h, 3)) return false;
      const size_t len = rd16be(h + 1);
      if (len < 4) return false;
      pdu->insert(pdu->end(), h, h + 3);
      pdu->resize(len);
      *fast_path = false;
      return len == 4 || tls_read(pdu->data() + 4, len - 4);
    }
    if ((b0 & 3) != 0) return false;
    uint8_t l;
    if (!tls_read(&l, 1)) return false;
    size_t len = l;
    pdu->push_back(l);
    if (l & 0x80) {
      uint8_t l2;
      if (!tls_read(&l2, 1)) return false;
      pdu->push_back(l2);
      len = ((l & 0x7F) << 8) | l2;
    }
    if (len < pdu->size()) return false;
    const size_t have = pdu->size();
    pdu->resize(len);
    *fast_path = true;
    return len == have || tls_read(pdu->data() + have, len - have);
  }

  static Buf tpkt(const Buf& payload) {
    Buf out = {3, 0};
    put16be(out, static_cast<uint32_t>(payload.size() + 4));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
  }
  bool send_x224(const Buf& mcs) {
    Buf p = {0x02, 0xF0, 0x80};
    p.insert(p.end(), mcs.begin(), mcs.end());
    return tls_write(tpkt(p));
  }
  // MCS Send Data Indication from the server to `channel`.
  bool send_channel(uint16_t channel, const Buf& data) {
    Buf p = {0x68};
    put16be(p, 0);  // initiator: the server (user 1001)
    put16be(p, channel);
    p.push_back(0x70);
    if (data.size() > 0x7F) put16be(p, 0x8000 | static_cast<uint32_t>(data.size()));
    else p.push_back(uint8_t(data.size()));
    p.insert(p.end(), data.begin(), data.end());
    return send_x224(p);
  }

  // ---- connection sequence ----
  bool negotiate() {
    Buf req(4);
    if (!conn_.read_exact(req.data(), 4) || req[0] != 3) {
      remote_log("rdp %s: not an RDP client (the first bytes are not an RDP connection request)", peer_.c_str());
      return false;
    }
    const size_t len = rd16be(&req[2]);
    if (len < 11 || len > 1024) return false;
    req.resize(len);
    if (!conn_.read_exact(req.data() + 4, len - 4)) return false;
    uint32_t protocols = 0;
    if (len >= 19 && req[len - 8] == 0x01 && rd16(&req[len - 6]) == 8) protocols = rd32(&req[len - 4]);
    const bool want_tls = opt_.tls != nullptr;
    Buf cc = {0x0E, 0xD0, 0, 0, 0x12, 0x34, 0};
    remote_log("rdp %s: connection request, the client offers security 0x%x (1 = TLS, 2 = NLA/CredSSP; this server uses %s)", peer_.c_str(), protocols,
               want_tls ? "TLS" : "standard RDP security");
    if (want_tls && !(protocols & 1)) {  // TLS is switched on: refuse a client that will not use it
      remote_log("rdp %s: refused, the client does not offer TLS (allow TLS in the client, or in Guacamole set Security to tls or any)", peer_.c_str());
      cc.insert(cc.end(), {0x03, 0x00, 0x08, 0x00});
      put32le(cc, 1);  // SSL_REQUIRED_BY_SERVER
      conn_.write_all(tpkt(cc));
      return false;
    }
    if (!want_tls && protocols == 1) {  // a client that insists on TLS cannot talk to a server with TLS switched off
      cc.insert(cc.end(), {0x03, 0x00, 0x08, 0x00});
      put32le(cc, 2);  // SSL_NOT_ALLOWED_BY_SERVER
      conn_.write_all(tpkt(cc));
      return false;
    }
    cc.insert(cc.end(), {0x02, 0x00, 0x08, 0x00});
    put32le(cc, want_tls ? 1 : 0);  // selected protocol: TLS, or standard RDP security without encryption
    requested_protocols_ = protocols;
    if (!conn_.write_all(tpkt(cc))) return false;
    return !want_tls || conn_.start_tls(opt_.tls);
  }

  struct ClientData {
    int bpp = 16;
    uint16_t early_flags = 0;
    std::vector<std::string> channels;
  };

  bool parse_client_data(const Buf& pdu, ClientData* cd) {
    static const uint8_t kKey[4] = {'D', 'u', 'c', 'a'};
    auto it = std::search(pdu.begin(), pdu.end(), kKey, kKey + 4);
    if (it == pdu.end()) return false;
    size_t pos = static_cast<size_t>(it - pdu.begin()) + 4;
    if (pos + 2 > pdu.size()) return false;
    pos += (pdu[pos] & 0x80) ? 2 : 1;  // PER length of the user data
    while (pos + 4 <= pdu.size()) {
      const uint16_t type = rd16(&pdu[pos]), blen = rd16(&pdu[pos + 2]);
      if (blen < 4 || pos + blen > pdu.size()) break;
      const uint8_t* body = &pdu[pos + 4];
      const size_t n = blen - 4u;
      if (type == 0xC001 && n >= 140) {  // CS_CORE
        const uint16_t supported = rd16(body + 138);
        const uint16_t early = n >= 142 ? rd16(body + 140) : 0;
        cd->early_flags = early;
        cd->bpp = ((supported & 0x0008) && (early & 0x0002)) ? 32 : 16;
      } else if (type == 0xC003 && n >= 4) {  // CS_NET
        const uint32_t count = std::min<uint32_t>(rd32(body), 31);
        for (uint32_t i = 0; i < count && 4 + 12 * (i + 1) <= n; ++i) {
          const char* name = reinterpret_cast<const char*>(body + 4 + 12 * i);
          cd->channels.emplace_back(name, strnlen(name, 8));
        }
      }
      pos += blen;
    }
    return true;
  }

  static void ber_len(Buf& b, size_t n) {
    if (n < 0x80) b.push_back(uint8_t(n));
    else if (n <= 0xFF) b.insert(b.end(), {0x81, uint8_t(n)});
    else b.insert(b.end(), {0x82, uint8_t(n >> 8), uint8_t(n)});
  }

  bool connect_mcs() {
    Buf pdu;
    bool fast;
    if (!read_pdu(&pdu, &fast) || fast || pdu.size() < 8 || pdu[4] != 0x02) return false;
    ClientData cd;
    if (!parse_client_data(pdu, &cd)) return false;
    // 32 bit colour is raw and big: only when the quality is high and nothing limits the bandwidth. Otherwise 16 bit, run-length coded.
    const int depth = session_.tuning.depth.load();
    bpp_ = depth == 16 ? 16 : depth >= 24 ? 32 : (cd.bpp == 32 && session_.tuning.quality.load() >= 90 && session_.tuning.max_kbps.load() == 0) ? 32 : 16;
    channel_count_ = cd.channels.size();

    // Server data blocks.
    Buf blocks;
    put16le(blocks, 0x0C01);  // SC_CORE
    put16le(blocks, 16);
    put32le(blocks, 0x00080004);
    put32le(blocks, requested_protocols_);
    put32le(blocks, 0);
    put16le(blocks, 0x0C03);  // SC_NET
    const size_t n = channel_count_;
    put16le(blocks, static_cast<uint32_t>(8 + 2 * n + (n % 2 ? 2 : 0)));
    put16le(blocks, kGlobalChannel);
    put16le(blocks, static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) put16le(blocks, kGlobalChannel + 1 + static_cast<uint32_t>(i));
    if (n % 2) put16le(blocks, 0);
    put16le(blocks, 0x0C02);  // SC_SECURITY: no RDP-level encryption, TLS does it
    put16le(blocks, 12);
    put32le(blocks, 0);
    put32le(blocks, 0);

    Buf gcc = {0x14, 0x76, 0x0A, 0x01, 0x01, 0x00, 0x01, 0xC0, 0x00, 0x4D, 0x63, 0x44, 0x6E};
    put16be(gcc, 0x8000 | static_cast<uint32_t>(blocks.size()));
    gcc.insert(gcc.end(), blocks.begin(), blocks.end());
    Buf user_data = {0x00, 0x05, 0x00, 0x14, 0x7C, 0x00, 0x01};
    if (gcc.size() > 0x7F) put16be(user_data, 0x8000 | static_cast<uint32_t>(gcc.size()));
    else user_data.push_back(uint8_t(gcc.size()));
    user_data.insert(user_data.end(), gcc.begin(), gcc.end());

    Buf resp = {0x0A, 0x01, 0x00, 0x02, 0x01, 0x00, 0x30, 0x1A, 0x02, 0x01, 0x22, 0x02, 0x01, 0x02, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01,
                0x02, 0x01, 0x00, 0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0xFF, 0xF8, 0x02, 0x01, 0x02, 0x04};
    ber_len(resp, user_data.size());
    resp.insert(resp.end(), user_data.begin(), user_data.end());
    Buf mcs = {0x7F, 0x66};
    ber_len(mcs, resp.size());
    mcs.insert(mcs.end(), resp.begin(), resp.end());
    return send_x224(mcs);
  }

  // ---- PDU builders ----
  Buf share_control(uint16_t type, const Buf& body) const {
    Buf b;
    put16le(b, static_cast<uint32_t>(body.size() + 6));
    put16le(b, 0x10 | type);
    put16le(b, kServerUser);
    b.insert(b.end(), body.begin(), body.end());
    return b;
  }
  Buf data_pdu(uint8_t type2, const Buf& body) const {
    Buf b;
    put32le(b, kShareId);
    b.push_back(0);
    b.push_back(1);  // stream: low priority
    put16le(b, static_cast<uint32_t>(body.size() + 4));
    b.push_back(type2);
    b.push_back(0);
    put16le(b, 0);
    b.insert(b.end(), body.begin(), body.end());
    return share_control(7, b);
  }
  bool send_data(uint8_t type2, const Buf& body) { return send_channel(kGlobalChannel, data_pdu(type2, body)); }

  static void cap(Buf& caps, uint16_t type, const Buf& data) {
    put16le(caps, type);
    put16le(caps, static_cast<uint32_t>(data.size() + 4));
    caps.insert(caps.end(), data.begin(), data.end());
  }

  bool send_demand_active() {
    Buf caps;
    int sets = 0;
    auto add = [&](uint16_t type, const Buf& data) {
      cap(caps, type, data);
      ++sets;
    };
    Buf g;  // General
    put16le(g, 1);  // os major: Windows
    put16le(g, 3);  // os minor
    put16le(g, 0x0200);
    put16le(g, 0);
    put16le(g, 0);  // no compression
    put16le(g, 0x0400);  // NO_BITMAP_COMPRESSION_HDR
    put16le(g, 0);
    put16le(g, 0);
    put16le(g, 0);
    g.push_back(1);  // refresh rect supported
    g.push_back(1);  // suppress output supported
    add(1, g);
    Buf bm;  // Bitmap
    put16le(bm, static_cast<uint32_t>(bpp_));
    put16le(bm, 1);
    put16le(bm, 1);
    put16le(bm, 1);
    put16le(bm, static_cast<uint32_t>(width_));
    put16le(bm, static_cast<uint32_t>(height_));
    put16le(bm, 0);
    put16le(bm, 0);  // desktop resize not supported
    put16le(bm, 1);
    bm.push_back(0);
    bm.push_back(0);
    put16le(bm, 1);
    put16le(bm, 0);
    add(2, bm);
    Buf ord(84, 0);  // Order: no drawing orders, bitmaps only
    ord[16 + 4 + 0] = 1;  // desktopSaveXGranularity = 1
    ord[16 + 4 + 2] = 20;
    ord[16 + 4 + 6] = 1;  // maximumOrderLevel
    ord[16 + 4 + 10] = 0x0A;  // orderFlags: NEGOTIATEORDERSUPPORT | ZEROBOUNDSDELTASSUPPORT
    add(3, ord);
    Buf ptr;  // Pointer
    put16le(ptr, 1);
    put16le(ptr, 20);
    put16le(ptr, 20);
    add(8, ptr);
    Buf in;  // Input: scancodes, extended mouse, unicode, fast-path input
    put16le(in, 0x0001 | 0x0004 | 0x0010 | 0x0020);
    put16le(in, 0);
    in.insert(in.end(), 16 + 64, 0);
    add(13, in);
    Buf vc;  // Virtual channel
    put32le(vc, 0);
    put32le(vc, 1600);
    add(20, vc);
    Buf sh;  // Share
    put16le(sh, 0);
    put16le(sh, 0);
    add(9, sh);
    Buf font;
    put16le(font, 1);
    put16le(font, 0);
    add(14, font);

    Buf body;
    put32le(body, kShareId);
    put16le(body, 4);  // source descriptor length
    put16le(body, static_cast<uint32_t>(caps.size() + 4));
    body.insert(body.end(), {'R', 'D', 'P', 0});
    put16le(body, static_cast<uint32_t>(sets));
    put16le(body, 0);
    body.insert(body.end(), caps.begin(), caps.end());
    put32le(body, 0);  // session id
    return send_channel(kGlobalChannel, share_control(1, body));
  }

  bool send_license_ok() {
    Buf b;
    put16le(b, 0x0080);  // SEC_LICENSE_PKT
    put16le(b, 0);
    b.insert(b.end(), {0xFF, 0x83, 0x10, 0x00});  // ERROR_ALERT, version 3, size 16
    put32le(b, 7);                                // STATUS_VALID_CLIENT
    put32le(b, 2);                                // ST_NO_TRANSITION
    put16le(b, 4);                                // BB_ERROR_BLOB
    put16le(b, 0);
    return send_channel(kGlobalChannel, b);
  }

  // ---- main loop ----
  void loop() {
    bool dirty = false;
    while (!session_.stop) {
      int timeout = -1;
      if (active_ && !paused_ && dirty) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(throttle_.next_allowed() - Clock::now()).count();
        timeout = left > 0 ? static_cast<int>(left) + 1 : 0;
      }
      pollfd fds[2] = {{conn_.fd(), POLLIN, 0}, {session_.wake_fd, POLLIN, 0}};
      const bool buffered = conn_.buffered();
      if (!buffered && poll(fds, 2, timeout) < 0) return;
      if (!buffered && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) && !(fds[0].revents & POLLIN)) return;
      if (!buffered && (fds[1].revents & POLLIN)) {
        uint64_t drain;
        [[maybe_unused]] ssize_t r = read(session_.wake_fd, &drain, sizeof drain);
        dirty = true;
      }
      if (buffered || (fds[0].revents & POLLIN)) {
        do {
          if (!handle_one()) return;
        } while (conn_.buffered());
      }
      if (want_refresh_) {
        want_refresh_ = false;
        dirty = true;
      }
      if (active_ && !paused_ && dirty && Clock::now() >= throttle_.next_allowed()) {
        if (!send_updates()) return;
        dirty = false;
      }
      session_.cpu_ns = thread_cpu_ns();
    }
  }

  bool handle_one() {
    Buf pdu;
    bool fast;
    if (!read_pdu(&pdu, &fast)) return false;
    if (fast) return handle_fast_input(pdu);
    if (pdu.size() < 8 || pdu[4] != 0x02) return true;
    const uint8_t* m = &pdu[7];
    const size_t n = pdu.size() - 7;
    if (n < 1) return true;
    switch (m[0] >> 2) {
      case 1: return true;  // erect domain request
      case 8: return false; // disconnect provider ultimatum
      case 10: {            // attach user request
        Buf r = {0x2E, 0x00};
        put16be(r, kServerUser - 1001);
        return send_x224(r);
      }
      case 14: {  // channel join request
        if (n < 5) return false;
        Buf r = {0x3E, 0x00};
        r.insert(r.end(), m + 1, m + 3);  // initiator
        r.insert(r.end(), m + 3, m + 5);  // requested channel
        r.insert(r.end(), m + 3, m + 5);  // joined channel
        return send_x224(r);
      }
      case 25: {  // send data request
        if (n < 8) return true;
        const uint16_t channel = rd16be(m + 3);
        size_t off = 6;
        size_t len = m[off];
        if (len & 0x80) {
          if (n < 9) return true;
          len = ((len & 0x7F) << 8) | m[off + 1];
          off += 2;
        } else {
          off += 1;
        }
        if (off + len > n) return false;
        if (channel != kGlobalChannel) return true;  // static virtual channels are not served
        return handle_global(m + off, len);
      }
      default: return true;
    }
  }

  bool handle_global(const uint8_t* d, size_t n) {
    if (state_ == State::AwaitInfo) {
      if (n < 4 || !(rd16(d) & 0x0040)) return true;
      return handle_info(d + 4, n - 4);
    }
    if (n < 6) return true;
    const uint16_t type = rd16(d + 2) & 0x0F;
    if (type == 3) {  // confirm active
      state_ = State::Finalize;
      return true;
    }
    if (type != 7 || n < 18) return true;
    const uint8_t type2 = d[14];
    const uint8_t* body = d + 18;
    const size_t blen = n - 18;
    switch (type2) {
      case 0x1F: {  // synchronize
        Buf b;
        put16le(b, 1);
        put16le(b, kServerUser);
        return send_data(0x1F, b);
      }
      case 0x14: {  // control
        if (blen < 2) return true;
        const uint16_t action = rd16(body);
        Buf b;
        if (action == 4) {  // cooperate
          put16le(b, 4);
          put16le(b, 0);
          put32le(b, 0);
          return send_data(0x14, b);
        }
        if (action == 1) {  // request control
          put16le(b, 2);
          put16le(b, kServerUser);
          put32le(b, kShareId & 0xFFFF);
          return send_data(0x14, b);
        }
        return true;
      }
      case 0x27: {  // font list -> font map, then the screen
        Buf b;
        put16le(b, 0);
        put16le(b, 0);
        put16le(b, 3);
        put16le(b, 4);
        if (!send_data(0x28, b)) return false;
        state_ = State::Active;
        active_ = true;
        shadow_.forget();
        hub_.mark_all_dirty(session_);
        return true;
      }
      case 0x1C: return handle_slow_input(body, blen);
      case 0x21: want_refresh_ = true; shadow_.forget(); hub_.mark_all_dirty(session_); return true;  // refresh rect
      case 0x23:                                                                    // suppress output
        paused_ = blen >= 1 && body[0] == 0;
        if (!paused_) {
          shadow_.forget();
          hub_.mark_all_dirty(session_);
        }
        return true;
      case 0x24: return send_data(0x25, Buf{});  // shutdown request: refuse, the client then disconnects itself
      default: return true;
    }
  }

  bool handle_info(const uint8_t* d, size_t n) {
    if (n < 18) return false;
    const uint32_t flags = rd32(d + 4);
    const size_t cb_domain = rd16(d + 8), cb_user = rd16(d + 10), cb_pass = rd16(d + 12);
    size_t off = 18;
    auto field = [&](size_t cb, std::string* out) {
      if (off + cb + 2 > n) return false;
      if (flags & 0x10) *out = utf16_to_utf8(d + off, cb);
      else out->assign(reinterpret_cast<const char*>(d + off), cb);
      off += cb + 2;
      return true;
    };
    std::string domain, user, pass;
    if (!field(cb_domain, &domain) || !field(cb_user, &user) || !field(cb_pass, &pass)) return false;
    const size_t bs = user.find('\\');  // "DOMAIN\user" typed into the user box
    if (bs != std::string::npos) user = user.substr(bs + 1);
    const bool ok = !opt_.password.empty() && same_secret(user, opt_.user) & same_secret(pass, opt_.password);
    if (!ok) {
      remote_log("rdp %s: login rejected for user '%s' (this server's user name is '%s'; the password is not shown)", peer_.c_str(), user.c_str(), opt_.user.c_str());
      hub_.auth_failed();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      return false;
    }
    // The desktop must exist before the client is told its size.
    for (int i = 0; i < 100 && !hub_.frame_size(&width_, &height_); ++i) {
      if (session_.stop) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (width_ <= 0) return false;
    native_w_ = width_;
    native_h_ = height_;
    fit_size(width_, height_, session_.tuning.res_w.load(), session_.tuning.res_h.load(), &width_, &height_);  // the render resolution
    out_w_ = width_;
    shadow_.reset(width_, height_);
    crop_w_ = width_ & ~3;  // some clients want a multiple of four
    width_ = crop_w_;
    if (!send_license_ok() || !send_demand_active()) return false;
    remote_log("rdp %s: logged in, %dx%d, %d bit", peer_.c_str(), width_, height_, bpp_);
    state_ = State::AwaitConfirm;
    return true;
  }

  // ---- input ----
  void key_event(uint32_t code, bool extended, bool release) {
    const uint32_t evdev = scancode_to_evdev(code, extended);
    if (!evdev) return;
    InputEvent ev;
    ev.kind = InputEvent::KeyCode;
    ev.a = static_cast<int>(evdev);
    ev.down = !release;
    hub_.push_input(ev);
  }
  void unicode_event(uint32_t cp, bool release) {
    const xkb_keysym_t sym = xkb_utf32_to_keysym(cp);
    if (sym == XKB_KEY_NoSymbol) return;
    InputEvent ev;
    ev.kind = InputEvent::KeySym;
    ev.sym = sym;
    ev.down = !release;
    hub_.push_input(ev);
  }
  void mouse_event(uint16_t flags, int x, int y) {
    if (out_w_ > 0 && height_ > 0 && (native_w_ != out_w_ || native_h_ != height_)) {  // the viewer's coordinates are in the shrunk picture
      x = static_cast<int>(static_cast<int64_t>(x) * native_w_ / out_w_);
      y = static_cast<int>(static_cast<int64_t>(y) * native_h_ / height_);
    }
    InputEvent mv;
    mv.kind = InputEvent::Motion;
    mv.a = x;
    mv.b = y;
    hub_.push_input(mv);
    static const struct {
      uint16_t flag;
      int code;
    } kButtons[3] = {{0x1000, 0x110}, {0x2000, 0x111}, {0x4000, 0x112}};  // left, right, middle
    for (const auto& b : kButtons) {
      if (!(flags & b.flag)) continue;
      InputEvent be;
      be.kind = InputEvent::Button;
      be.a = b.code;
      be.down = flags & 0x8000;
      hub_.push_input(be);
    }
    if ((flags & 0x0200) && !(flags & 0x0400)) {  // vertical wheel
      InputEvent we;
      we.kind = InputEvent::Wheel;
      we.a = (flags & 0x0100) ? 1 : -1;
      hub_.push_input(we);
    }
  }

  bool handle_slow_input(const uint8_t* d, size_t n) {
    if (n < 4) return true;
    const size_t count = rd16(d);
    for (size_t i = 0; i < count && 4 + 12 * (i + 1) <= n; ++i) {
      const uint8_t* e = d + 4 + 12 * i;
      const uint16_t type = rd16(e + 4), a = rd16(e + 6), b = rd16(e + 8), c = rd16(e + 10);
      if (type == 0x0004) key_event(b & 0xFF, a & 0x0100, a & 0x8000);
      else if (type == 0x0005) unicode_event(b, a & 0x8000);
      else if (type == 0x8001) mouse_event(a, b, c);
    }
    return true;
  }

  bool handle_fast_input(const Buf& pdu) {
    size_t pos = 1;
    pos += (pdu[1] & 0x80) ? 2 : 1;
    size_t count = (pdu[0] >> 2) & 0x0F;
    if (count == 0) {
      if (pos >= pdu.size()) return true;
      count = pdu[pos++];
    }
    for (size_t i = 0; i < count && pos < pdu.size(); ++i) {
      const uint8_t hdr = pdu[pos++];
      const int code = hdr >> 5, flags = hdr & 0x1F;
      if (code == 0 && pos + 1 <= pdu.size()) {
        key_event(pdu[pos], flags & 2, flags & 1);
        pos += 1;
      } else if (code == 1 && pos + 6 <= pdu.size()) {
        mouse_event(rd16(&pdu[pos]), rd16(&pdu[pos + 2]), rd16(&pdu[pos + 4]));
        pos += 6;
      } else if (code == 2 && pos + 6 <= pdu.size()) {
        pos += 6;
      } else if (code == 4 && pos + 2 <= pdu.size()) {
        unicode_event(rd16(&pdu[pos]), flags & 1);
        pos += 2;
      } else if (code == 3) {
        // keyboard sync: nothing to do
      } else {
        break;
      }
    }
    return true;
  }

  // ---- screen ----
  // RDP's interleaved run-length coding for 16 bit pixels, using only the colour run and colour image orders (a bottom-up
  // stream; runs may cross rows). `px` is the strip top-down, w*h pixels.
  static void rle16(const uint16_t* px, int w, int h, Buf& out) {
    const int n = w * h;
    auto at = [&](int k) { return px[static_cast<size_t>(h - 1 - k / w) * w + k % w]; };
    auto header = [&](int code, int len) {
      if (len < 32) {
        out.push_back(uint8_t((code << 5) | len));
      } else {
        out.push_back(uint8_t(code << 5));
        out.push_back(uint8_t(len - 32));
      }
    };
    int i = 0;
    while (i < n) {
      const uint16_t v = at(i);
      int run = 1;
      while (i + run < n && run < 287 && at(i + run) == v) ++run;
      if (run >= 3) {
        header(3, run);
        put16le(out, v);
        i += run;
        continue;
      }
      int lit = 0;
      while (i + lit < n && lit < 287) {
        if (lit > 0 && i + lit + 2 < n && at(i + lit) == at(i + lit + 1) && at(i + lit) == at(i + lit + 2)) break;  // a run starts here
        ++lit;
      }
      header(4, lit);
      for (int k = 0; k < lit; ++k) put16le(out, at(i + k));
      i += lit;
    }
  }

  bool send_updates() {
    TileBatch& batch = batch_;
    int w, h;
    bool resized;
    if (!hub_.take_dirty(session_, &batch, &w, &h, &resized, session_.tuning.res_w.load(), session_.tuning.res_h.load())) return true;
    if ((w & ~3) != crop_w_ || h != height_) return false;  // the client cannot follow a new size; it reconnects
    throttle_.set_frame(w, h);
    const auto started = Clock::now();
    const int bits = throttle_.quant_bits();
    const int depth = session_.tuning.depth.load();
    for (size_t i = 0; i < batch.n; ++i) {
      quantize_to_depth(batch.v[i].px.data(), batch.v[i].px.size(), depth);
      quantize_pixels(batch.v[i].px.data(), batch.v[i].px.size(), bits);
    }
    const size_t kept = shadow_.drop_unchanged(batch);
    if (kept == 0) return true;

    const int bytes_pp = bpp_ / 8;
    const int strip_rows = bpp_ == 32 ? 32 : 64;  // at most 8 KB of raw pixels per rectangle
    Buf rects;
    int nrects = 0;
    size_t total = 0;
    Clock::duration writing{};
    auto flush = [&]() {
      if (!nrects) return true;
      Buf body;
      put16le(body, 1);  // UPDATETYPE_BITMAP
      put16le(body, static_cast<uint32_t>(nrects));
      body.insert(body.end(), rects.begin(), rects.end());
      rects.clear();
      nrects = 0;
      total += body.size() + 30;
      const auto t0 = Clock::now();
      const bool ok = send_data(0x02, body);
      writing += Clock::now() - t0;
      return ok;
    };
    std::vector<uint16_t> strip;
    Buf coded;
    for (size_t ti = 0; ti < kept; ++ti) {
      const Tile& t = batch.v[ti];
      const int tw = std::min(t.w, crop_w_ - t.x);
      if (tw <= 0) continue;
      for (int sy = 0; sy < t.h; sy += strip_rows) {
        const int sh = std::min(strip_rows, t.h - sy);
        const size_t raw_len = static_cast<size_t>(tw) * sh * bytes_pp;
        bool compressed = false;
        if (bpp_ == 16) {
          strip.resize(static_cast<size_t>(tw) * sh);
          for (int row = 0; row < sh; ++row) {
            const uint32_t* src = &t.px[static_cast<size_t>(sy + row) * t.w];
            for (int x = 0; x < tw; ++x) {
              const uint32_t v = src[x];
              strip[static_cast<size_t>(row) * tw + x] = uint16_t(((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x001F));
            }
          }
          coded.clear();
          rle16(strip.data(), tw, sh, coded);
          compressed = coded.size() < raw_len;
        }
        const size_t data_len = compressed ? coded.size() : raw_len;
        if (rects.size() + data_len + 18 > 12000 && !flush()) return false;
        put16le(rects, static_cast<uint32_t>(t.x));
        put16le(rects, static_cast<uint32_t>(t.y + sy));
        put16le(rects, static_cast<uint32_t>(t.x + tw - 1));
        put16le(rects, static_cast<uint32_t>(t.y + sy + sh - 1));
        put16le(rects, static_cast<uint32_t>(tw));
        put16le(rects, static_cast<uint32_t>(sh));
        put16le(rects, static_cast<uint32_t>(bpp_));
        put16le(rects, compressed ? 0x0401 : 0);  // BITMAP_COMPRESSION | NO_BITMAP_COMPRESSION_HDR
        put16le(rects, static_cast<uint32_t>(data_len));
        if (compressed) {
          rects.insert(rects.end(), coded.begin(), coded.end());
        } else {
          for (int row = sh - 1; row >= 0; --row) {  // bottom-up
            if (bpp_ == 32) {
              const uint32_t* src = &t.px[static_cast<size_t>(sy + row) * t.w];
              for (int x = 0; x < tw; ++x) put32le(rects, src[x]);
            } else {
              for (int x = 0; x < tw; ++x) put16le(rects, strip[static_cast<size_t>(row) * tw + x]);
            }
          }
        }
        ++nrects;
      }
    }
    if (!flush()) return false;
    for (size_t k = 0; k < kept; ++k) shadow_.commit(batch.v[k]);
    trim_batch(batch);
    {
      size_t m = shadow_.bytes() + 64 * 1024;
      for (const Tile& t : batch.v) m += t.px.capacity() * sizeof(uint32_t);
      session_.mem = m;
    }
    throttle_.sent(total, writing, started);
    return true;
  }

  enum class State { AwaitInfo, AwaitConfirm, Finalize, Active };

  Hub& hub_;
  Conn conn_;
  ServeOptions opt_;
  Session session_;
  Throttle throttle_;
  std::string peer_;
  Shadow shadow_;
  State state_ = State::AwaitInfo;
  uint32_t requested_protocols_ = 0;
  int bpp_ = 16;
  size_t channel_count_ = 0;
  int width_ = 0, height_ = 0, crop_w_ = 0;
  int native_w_ = 0, native_h_ = 0, out_w_ = 0;  // the screen; width_/height_ is the picture the viewer gets
  bool active_ = false, paused_ = false, want_refresh_ = false;
  TileBatch batch_;  // kept between updates so the pixel buffers are reused
};

}  // namespace

void serve_rdp(Hub& hub, int fd, ServeOptions options) {
  RdpClient client(hub, fd, std::move(options));
  client.run();
}

}  // namespace fleetremote
