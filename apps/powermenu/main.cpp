// fleetwm-powermenu: GTK-free power menu. A fullscreen OVERLAY layer
// surface (opaque bg_primary backdrop) with a centered card of five action
// buttons: Lock, Log out, Sleep, Reboot, Shut down. Escape or a click
// outside the card dismisses it. Arrow keys / Tab / Enter also work.

#include <signal.h>
#include <spawn.h>
#include <sys/signalfd.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "quit_signals.hpp"
#include "ipc_client.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "power_actions.hpp"
#include "power_icons.hpp"
#include "theme.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

extern char** environ;

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr uint32_t kBtnLeft = 0x110;

using namespace fleetwm::power;

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct PowerMenu {
  App app;
  Palette pal;
  IpcClient ipc;
  std::unique_ptr<Surface> surface;
  Rect card, items[kCount];
  int hover = -1;  // pointer hover or keyboard selection

  void draw(cairo_t* cr, int w, int h) {
    set_source(cr, pal.bg_primary);
    cairo_paint(cr);

    const double pad = 24, gap = 8, item_h = 46, content_w = 268;
    const double card_w = content_w + 2 * pad;
    const double card_h = 2 * pad + kCount * item_h + (kCount - 1) * gap;
    card = {(w - card_w) / 2, (h - card_h) / 2, card_w, card_h};
    rounded_rect(cr, card.x, card.y, card.w, card.h, pal.rounded ? 12 : 0);
    set_source(cr, pal.bg_secondary);
    cairo_fill(cr);

    for (int i = 0; i < kCount; ++i) {
      items[i] = {card.x + pad, card.y + pad + i * (item_h + gap), content_w, item_h};
      const bool on = i == hover;
      if (on) {
        rounded_rect(cr, items[i].x, items[i].y, items[i].w, items[i].h, pal.rounded ? 6 : 0);
        set_source(cr, pal.accent);
        cairo_fill(cr);
      }
      const Color fg = on ? pal.bg_primary : pal.fg_primary;
      set_source(cr, fg);
      const double cy = items[i].y + item_h / 2;
      draw_icon(cr, i, items[i].x + 16 + kIconCell / 2, cy);
      const TextExtents te = measure_text(cr, label(i), 16);
      draw_text(cr, label(i), items[i].x + 16 + kIconCell + 12, cy - te.height / 2 + te.ascent, 16, fg);
    }
  }

  void spawn(std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
      std::fprintf(stderr, "fleetwm-powermenu: failed to launch %s\n", argv[0]);
  }

  void run_action(int a) {
    if (a == kLock) {
      if (ipc.is_connected()) ipc.send_command("LOCK");
      else std::fprintf(stderr, "fleetwm-powermenu: not connected to compositor IPC; cannot lock\n");
    } else {
      std::string session;
      if (a == kLogout) {
        char* sid = nullptr;
        if (sd_pid_get_session(getpid(), &sid) < 0 || !sid)
          std::fprintf(stderr, "fleetwm-powermenu: logout failed: could not determine session id\n");
        else session = sid;
        std::free(sid);
      }
      std::vector<std::string> cmd = command_for(a, session);  // power_actions.hpp, covered by the unit tests
      if (!cmd.empty()) spawn(std::move(cmd));
    }
    app.quit();
  }

  void set_hover(int i) {
    if (i != hover) {
      hover = i;
      surface->queue_draw();
    }
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    switch (ev.sym) {
      case XKB_KEY_Escape: app.quit(); break;
      case XKB_KEY_Down:
      case XKB_KEY_Tab: set_hover(hover < 0 ? 0 : (hover + 1) % kCount); break;
      case XKB_KEY_Up:
      case XKB_KEY_ISO_Left_Tab: set_hover(hover <= 0 ? kCount - 1 : hover - 1); break;
      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
      case XKB_KEY_space:
        if (hover >= 0) run_action(hover);
        break;
    }
  }

  void on_motion(double x, double y) {
    int h = -1;
    for (int i = 0; i < kCount; ++i)
      if (items[i].hit(x, y)) h = i;
    set_hover(h);
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (pressed || b != kBtnLeft) return;  // act on release, like a GTK button
    for (int i = 0; i < kCount; ++i)
      if (items[i].hit(x, y)) return run_action(i);
    if (!card.hit(x, y)) app.quit();
  }
};

}  // namespace

int main() {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  fleetwm::tune_malloc_for_low_rss();

  PowerMenu P;
  P.pal = load_palette(load_theme_config());
  P.ipc.connect();  // best-effort, only needed for "lock"
  if (!P.app.connect()) return 1;

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.exclusive_zone = -1;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
  cfg.name = "fleetwm-powermenu";
  P.surface = std::make_unique<Surface>(P.app, cfg);
  P.surface->on_draw = [&P](cairo_t* cr, int w, int h) { P.draw(cr, w, h); };
  P.surface->on_key = [&P](const KeyEvent& e) { P.on_key(e); };
  P.surface->on_motion = [&P](double x, double y) { P.on_motion(x, y); };
  P.surface->on_button = [&P](double x, double y, uint32_t b, bool p) { P.on_button(x, y, b, p); };
  P.surface->on_leave = [&P] { P.set_hover(-1); };
  P.surface->on_closed = [&P] { P.app.quit(); };

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  P.app.watch_fd(sfd, [&P] { P.app.quit(); });

  P.app.run();
  return 0;
}
