// fleetwm-osd: the volume and brightness pop-up (a rounded card at the bottom of the screen with a big icon and a 16-segment bar, like
// macOS). The compositor starts `fleetwm-osd volume up|down|mute`, `fleetwm-osd brightness up|down` on the media keys; the first
// process becomes the server (a socket in XDG_RUNTIME_DIR), later ones just hand their command to it and exit, so holding a key
// does not start a process per press. The server changes the level (wpctl for the sound, logind or sysfs for the backlight), shows
// the card for about 1.6 s, and quits ten seconds after the last command.
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "osd_logic.hpp"
#include "quit_signals.hpp"
#include "speaker_glyph.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {
using namespace fleetwm;
using namespace fleetwm::kit;
namespace osd = fleetwm::osd;

constexpr int kCardW = 200, kCardH = 200, kShowMs = 1600, kQuitMs = 10000, kSegments = 16;

std::string socket_path() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  return std::string(rt && *rt ? rt : "/tmp") + "/fleetwm-osd.sock";
}

sockaddr_un socket_addr() {
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  std::snprintf(a.sun_path, sizeof a.sun_path, "%s", socket_path().c_str());
  return a;
}

// Another server is running: give it the command. False when none answers.
bool send_to_server(const std::string& line) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  const sockaddr_un a = socket_addr();
  bool ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0;
  if (ok) ok = ::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
  ::close(fd);
  return ok;
}

int listen_on_socket() {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return -1;
  sockaddr_un a = socket_addr();
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
    ::unlink(a.sun_path);  // only reached after send_to_server failed: a dead server's file
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
      ::close(fd);
      return -1;
    }
  }
  ::chmod(a.sun_path, 0600);
  if (::listen(fd, 8) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Runs a fixed command line and returns what it printed.
std::string run_capture(const char* cmd) {
  std::string out;
  if (std::FILE* p = ::popen(cmd, "r")) {
    char buf[256];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    ::pclose(p);
  }
  return out;
}

struct Level {
  int percent = 0;
  bool muted = false;
  bool ok = false;
};

Level read_volume() {
  Level l;
  l.ok = osd::parse_wpctl_volume(run_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null"), &l.percent, &l.muted);
  return l;
}

void set_volume_percent(int percent) {
  char cmd[128];
  std::snprintf(cmd, sizeof cmd, "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%% >/dev/null 2>&1", percent);
  (void)!std::system(cmd);
}

osd::Backlight g_backlight;

// FLEETWM_BACKLIGHT_DIR points at a fake tree for tests and screenshots.
std::string backlight_root() {
  const char* e = std::getenv("FLEETWM_BACKLIGHT_DIR");
  return e && *e ? e : "/sys/class/backlight";
}

bool write_backlight(long value) {
  const std::string path = backlight_root() + "/" + g_backlight.name + "/brightness";
  if (std::FILE* f = std::fopen(path.c_str(), "w")) {  // works when a udev rule grants the video group write access
    const bool ok = std::fprintf(f, "%ld", value) > 0;
    return std::fclose(f) == 0 && ok;
  }
  sd_bus* bus = nullptr;  // otherwise logind sets it for the active session, no root needed
  if (sd_bus_open_system(&bus) < 0) return false;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  const int r = sd_bus_call_method(bus, "org.freedesktop.login1", "/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session",
                                   "SetBrightness", &err, nullptr, "ssu", "backlight", g_backlight.name.c_str(), static_cast<uint32_t>(value));
  sd_bus_error_free(&err);
  sd_bus_unref(bus);
  return r >= 0;
}

struct Osd {
  App app;
  Palette pal;
  std::unique_ptr<Surface> surface;
  osd::Kind kind = osd::Kind::Volume;
  Level level;
  int hide_timer = 0, quit_timer = 0;

  void draw(cairo_t* cr, int w, int h) {
    set_source(cr, alpha(pal.bg_secondary, 0.94));
    rounded_rect(cr, 0.5, 0.5, w - 1, h - 1, pal.rounded ? 22 : 0);
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.30));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    const Color fg = pal.fg_primary;
    const double cx = w / 2.0, cy = 82;
    if (kind == osd::Kind::Volume) {
      draw_speaker_glyph(cr, cx, cy, 92, level.muted || !level.ok ? 0 : speaker_waves(level.percent), fg.r, fg.g, fg.b, level.muted || !level.ok);
    } else {
      draw_sun(cr, cx, cy, 92, fg);
    }
    const int lit = level.muted || !level.ok ? 0 : osd::lit_segments(level.percent, kSegments);
    const double seg_w = 6.0, gap = 2.5, total = kSegments * seg_w + (kSegments - 1) * gap;
    double x = (w - total) / 2.0;
    const double y = 150, seg_h = 9;
    for (int i = 0; i < kSegments; ++i, x += seg_w + gap) {
      set_source(cr, i < lit ? fg : alpha(fg, 0.22));
      cairo_rectangle(cr, x, y, seg_w, seg_h);
      cairo_fill(cr);
    }
  }

  static Color alpha(Color c, double a) {
    c.a = a;
    return c;
  }

  // A sun: a disc with eight rays; the rays grow with nothing, the disc is the same at every level (the bar says how much).
  static void draw_sun(cairo_t* cr, double cx, double cy, double size, const Color& c) {
    cairo_save(cr);
    set_source(cr, c);
    cairo_arc(cr, cx, cy, size * 0.17, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_line_width(cr, std::max(1.5, size * 0.07));
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < 8; ++i) {
      const double a = i * M_PI / 4;
      cairo_move_to(cr, cx + std::cos(a) * size * 0.28, cy + std::sin(a) * size * 0.28);
      cairo_line_to(cr, cx + std::cos(a) * size * 0.42, cy + std::sin(a) * size * 0.42);
    }
    cairo_stroke(cr);
    cairo_restore(cr);
  }

  void show() {
    if (!surface) {
      Surface::Config cfg;
      cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
      cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;  // centred sideways
      cfg.width = kCardW;
      cfg.height = kCardH;
      cfg.margin_bottom = 90;
      cfg.exclusive_zone = -1;
      cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
      cfg.name = "fleetwm-osd";
      surface = std::make_unique<Surface>(app, cfg);
      surface->set_input_passthrough();
      surface->on_draw = [this](cairo_t* cr, int w, int h) { draw(cr, w, h); };
    } else {
      surface->queue_draw();
    }
    if (hide_timer) app.unwatch(hide_timer);
    hide_timer = app.add_oneshot(kShowMs, [this] {
      hide_timer = 0;
      surface.reset();
    });
    if (quit_timer) app.unwatch(quit_timer);
    quit_timer = app.add_oneshot(kQuitMs, [this] { app.quit(); });
  }

  void handle(const osd::Command& c) {
    kind = c.kind;
    if (c.kind == osd::Kind::Volume) {
      Level l = read_volume();
      if (l.ok && c.action != osd::Action::Show) {
        if (c.action == osd::Action::Mute) {
          (void)!std::system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle >/dev/null 2>&1");
        } else {
          if (l.muted) (void)!std::system("wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 >/dev/null 2>&1");
          set_volume_percent(osd::step_percent(l.percent, c.action == osd::Action::Up));
        }
        l = read_volume();
      }
      level = l;
    } else {
      level = {};
      if (osd::read_backlight(backlight_root(), &g_backlight)) {
        int pct = osd::backlight_percent(g_backlight.current, g_backlight.max);
        if (c.action != osd::Action::Show) {
          pct = osd::step_percent(pct, c.action == osd::Action::Up);
          if (write_backlight(osd::backlight_value(pct, g_backlight.max))) level.ok = true;
          else level.ok = false;
        } else {
          level.ok = true;
        }
        level.percent = pct;
      }
    }
    show();
  }

  void accept_client(int listen_fd) {
    const int fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return;
    char buf[64];
    const ssize_t n = ::read(fd, buf, sizeof buf - 1);  // the client has already written its one line and closed
    ::close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    osd::Command c;
    if (osd::parse_command(buf, &c)) handle(c);
  }
};
}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-osd", "volume up|down|mute|show | brightness up|down|show")) return 0;
  if (argc != 3) {
    std::fprintf(stderr, "usage: fleetwm-osd volume up|down|mute|show | brightness up|down|show\n");
    return 2;
  }
  const std::string line = std::string(argv[1]) + " " + argv[2];
  osd::Command first;
  if (!osd::parse_command(line, &first)) {
    std::fprintf(stderr, "fleetwm-osd: unknown command '%s'\n", line.c_str());
    return 2;
  }
  if (send_to_server(line)) return 0;

  fleetwm::tune_malloc_for_low_rss();
  const int listen_fd = listen_on_socket();
  if (listen_fd < 0) return send_to_server(line) ? 0 : 1;  // lost the race to another first process

  Osd O;
  O.pal = load_palette(load_theme_config());
  if (!O.app.connect()) {
    ::unlink(socket_addr().sun_path);
    return 1;
  }
  O.app.watch_fd(listen_fd, [&O, listen_fd] { O.accept_client(listen_fd); });
  O.handle(first);
  O.app.run();
  ::unlink(socket_addr().sun_path);
  ::close(listen_fd);
  return 0;
}
