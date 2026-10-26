// Round-trip harness: serve_vnc on a TCP port fed synthetic frames, decoded by libvncclient (the decoder Apache Guacamole uses).
extern "C" {
#include <rfb/rfbclient.h>
}
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>
#include "hub.hpp"
#include "vnc.hpp"
using namespace fleetremote;
static char* getpw(rfbClient*) { return strdup("testpw12"); }
int main(int argc, char** argv) {
  std::string name = argv[1]; const char* enc = argv[2]; int quality = atoi(argv[3]); int level = atoi(argv[4]);
  int W, H; { std::ifstream d(name + ".dim"); d >> W >> H; }
  std::vector<uint32_t> img(size_t(W) * H);
  { std::ifstream f(name + ".raw", std::ios::binary); f.read(reinterpret_cast<char*>(img.data()), img.size() * 4); }
  Hub hub;
  hub.vnc_tuning.quality = quality; hub.vnc_tuning.fps = 240;
  hub.publish(W, H, reinterpret_cast<const uint8_t*>(img.data()), W * 4, false, false, {});
  int ls = socket(AF_INET, SOCK_STREAM, 0); int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0; bind(ls, (sockaddr*)&a, sizeof a); listen(ls, 1);
  socklen_t al = sizeof a; getsockname(ls, (sockaddr*)&a, &al); int port = ntohs(a.sin_port);
  std::thread srv([&] { int fd = accept(ls, nullptr, nullptr); ServeOptions o; o.password = "testpw12"; serve_vnc(hub, fd, o); });
  int bpp = getenv("BPP16") ? 2 : 4; rfbClient* c = rfbGetClient(bpp == 2 ? 5 : 8, 3, bpp);
  c->GetPassword = getpw; c->appData.encodingsString = enc; c->appData.qualityLevel = level; c->appData.compressLevel = 3; c->appData.enableJPEG = level >= 0;
  c->serverHost = strdup("127.0.0.1"); c->serverPort = port;
  if (!rfbInitClient(c, nullptr, nullptr)) { std::printf("client init failed\n"); return 1; }
  if (c->width != W || c->height != H) { std::printf("size mismatch %dx%d\n", c->width, c->height); return 1; }
  for (int i = 0; i < 200; ++i) { int r = WaitForMessage(c, 100000); if (r < 0) { std::printf("link error\n"); break; } if (r > 0 && !HandleRFBServerMessage(c)) { std::printf("decode error\n"); return 2; } }
  // compare
  double se = 0; size_t n = 0, exact = 0, total = size_t(W) * H;
  const uint32_t* fb = reinterpret_cast<const uint32_t*>(c->frameBuffer);
  for (size_t i = 0; i < total; ++i) {
    uint32_t p = bpp == 2 ? reinterpret_cast<const uint16_t*>(c->frameBuffer)[i] : fb[i], q = img[i];
    // client format is 32bpp: compare RGB channels whatever the byte order the library chose
    uint32_t pr = ((p >> c->format.redShift) & c->format.redMax) * 255 / c->format.redMax, pg = ((p >> c->format.greenShift) & c->format.greenMax) * 255 / c->format.greenMax, pb = ((p >> c->format.blueShift) & c->format.blueMax) * 255 / c->format.blueMax;
    int dr = pr - ((q >> 16) & 255), dg = pg - ((q >> 8) & 255), db = pb - (q & 255);
    se += dr * dr + dg * dg + db * db; n += 3; if (!dr && !dg && !db) ++exact;
  }
  std::printf("%-9s enc=%-6s q=%3d level=%2d  PSNR %.1f dB, %.1f%% of pixels exact\n", name.c_str(), enc, quality, level, se == 0 ? 99.0 : 10 * std::log10(255.0 * 255 / (se / n)), 100.0 * exact / total);
  rfbClientCleanup(c); hub.kick_all(); srv.join(); return 0;
}
