// fleetwm-locker: lean, GTK-free lock screen. A fullscreen OVERLAY layer
// surface with EXCLUSIVE keyboard focus, drawn with cairo on wl_shm. Same
// look as the greeter's login card (avatar, name, password entry, submit
// button). PAM re-authenticates the running session; on success it sends
// UNLOCK over the compositor IPC and exits. Zero CPU while idle.

#include <pwd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>

#include "ipc_client.hpp"
#include "lean.hpp"
#include "malloc_tuning.hpp"
#include "pam_verify.hpp"
#include "theme.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {

using namespace fleetwm;
using namespace fleetwm::lean;

constexpr size_t kMaxPassword = 255;
constexpr uint32_t kBtnLeft = 0x110;

std::string current_username() {
  if (struct passwd* pw = getpwuid(getuid())) return pw->pw_name;
  return "user " + std::to_string(getuid());
}

struct Locker {
  App app;
  Palette pal;
  IpcClient ipc;
  std::string username;
  std::string password;
  std::string error;
  bool busy = false;
  bool reveal = false;
  std::unique_ptr<Surface> surface;

  // Layout (logical px), recomputed per draw from the surface size.
  struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  };
  Rect entry, button, eye;

  void wipe() {
    if (!password.empty()) explicit_bzero(password.data(), password.size());
    password.clear();
  }

  void draw(cairo_t* cr, int w, int h) {
    set_source(cr, pal.bg_primary);
    cairo_paint(cr);

    const double cx = w / 2.0;
    const double avatar_r = 64, entry_w = 220, entry_h = 36, gap = 10;
    const double name_px = 20;
    const double total_h = 2 * avatar_r + gap + name_px + gap + entry_h + (error.empty() ? 0 : gap + 16);
    double y = (h - total_h) / 2.0;

    // Avatar: filled disc + 2px border, with a head/shoulders glyph.
    const double acy = y + avatar_r;
    cairo_arc(cr, cx, acy, avatar_r, 0, 2 * M_PI);
    set_source(cr, pal.bg_secondary);
    cairo_fill_preserve(cr);
    set_source(cr, pal.bg_primary);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    set_source(cr, pal.fg_secondary);
    cairo_arc(cr, cx, acy - 14, 18, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_save(cr);
    cairo_arc(cr, cx, acy, avatar_r - 2, 0, 2 * M_PI);
    cairo_clip(cr);
    cairo_arc(cr, cx, acy + 50, 34, M_PI, 2 * M_PI);
    cairo_line_to(cr, cx + 34, acy + 70);
    cairo_line_to(cr, cx - 34, acy + 70);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
    y += 2 * avatar_r + gap;

    // Name.
    TextExtents te = measure_text(cr, username, name_px, true);
    draw_text(cr, username, cx - te.width / 2, y + te.ascent, name_px, pal.fg_primary, true);
    y += te.height + gap;

    // Entry + submit button row.
    const double btn = entry_h, row_w = entry_w + 6 + btn;
    entry = {cx - row_w / 2, y, entry_w, entry_h};
    button = {entry.x + entry_w + 6, y, btn, btn};
    const double rad = pal.rounded ? 6 : 0;
    rounded_rect(cr, entry.x, entry.y, entry.w, entry.h, rad);
    set_source(cr, pal.bg_secondary);
    cairo_fill_preserve(cr);
    set_source(cr, pal.accent);  // always focused: it is the only widget
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // Password text / bullets, clipped to the entry.
    cairo_save(cr);
    cairo_rectangle(cr, entry.x + 8, entry.y, entry.w - 16 - 24, entry.h);
    cairo_clip(cr);
    const double ty = entry.y + entry.h / 2;
    double px = entry.x + 10;
    if (reveal) {
      TextExtents pe = measure_text(cr, password, 15);
      px += draw_text(cr, password, px, ty + pe.ascent / 2 - 1, 15, pal.fg_primary);
    } else {
      set_source(cr, pal.fg_primary);
      size_t chars = 0;
      for (unsigned char c : password)
        if ((c & 0xC0) != 0x80) ++chars;
      for (size_t i = 0; i < chars; ++i) {
        cairo_arc(cr, px + 4, ty, 3.5, 0, 2 * M_PI);
        cairo_fill(cr);
        px += 11;
      }
    }
    // Caret.
    set_source(cr, pal.fg_primary);
    cairo_rectangle(cr, px + 1, ty - 9, 1.2, 18);
    cairo_fill(cr);
    cairo_restore(cr);

    // Peek (eye) toggle at the right inside the entry.
    eye = {entry.x + entry.w - 28, entry.y + 4, 24, entry.h - 8};
    {
      const double ex = eye.x + eye.w / 2, ey = eye.y + eye.h / 2;
      set_source(cr, reveal ? pal.accent : pal.fg_secondary);
      cairo_set_line_width(cr, 1.5);
      cairo_move_to(cr, ex - 8, ey);
      cairo_curve_to(cr, ex - 4, ey - 6, ex + 4, ey - 6, ex + 8, ey);
      cairo_curve_to(cr, ex + 4, ey + 6, ex - 4, ey + 6, ex - 8, ey);
      cairo_stroke(cr);
      cairo_arc(cr, ex, ey, 2.5, 0, 2 * M_PI);
      cairo_fill(cr);
    }

    // Submit button: accent fill with a "next" arrow in bg_primary.
    rounded_rect(cr, button.x, button.y, button.w, button.h, rad);
    set_source(cr, busy ? pal.fg_secondary : pal.accent);
    cairo_fill(cr);
    set_source(cr, pal.bg_primary);
    cairo_set_line_width(cr, 2);
    const double bx = button.x + button.w / 2, by = button.y + button.h / 2;
    cairo_move_to(cr, bx - 5, by - 6);
    cairo_line_to(cr, bx + 3, by);
    cairo_line_to(cr, bx - 5, by + 6);
    cairo_stroke(cr);
    y += entry_h;

    if (!error.empty()) {
      y += gap;
      TextExtents ee = measure_text(cr, error, 14);
      draw_text(cr, error, cx - ee.width / 2, y + ee.ascent, 14, parse_color("#f38ba8"));
    }
  }

  void attempt_unlock() {
    if (busy) return;
    busy = true;
    error.clear();
    surface->queue_draw();
    // Let one frame show the busy state before PAM blocks the loop.
    app.add_oneshot(30, [this] {
      const bool ok = fleetwm::locker::verify_password(username, password);
      wipe();
      if (!ok) {
        error = "Incorrect password";
        busy = false;
        surface->queue_draw();
        return;
      }
      // Best-effort like the initial connect(): if the socket dropped there is
      // nothing more graceful than quitting anyway.
      if (ipc.is_connected()) {
        ipc.send_command("UNLOCK");
      } else {
        std::fprintf(stderr, "fleetwm-locker: not connected to compositor IPC; cannot send UNLOCK\n");
      }
      app.quit();
    });
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed || busy) return;
    switch (ev.sym) {
      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
        attempt_unlock();
        return;
      case XKB_KEY_BackSpace:
        if (ev.mods & kCtrl) {
          wipe();
        } else {
          while (!password.empty() && (static_cast<unsigned char>(password.back()) & 0xC0) == 0x80)
            password.pop_back();
          if (!password.empty()) password.pop_back();
        }
        break;
      case XKB_KEY_Escape:
        wipe();
        break;
      case XKB_KEY_v:
      case XKB_KEY_Insert:
        if ((ev.sym == XKB_KEY_v && (ev.mods & kCtrl)) || (ev.sym == XKB_KEY_Insert && (ev.mods & kShift))) {
          app.paste_text([this](const std::string& text) {
            for (char c : text)
              if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f && password.size() < kMaxPassword)
                password += c;  // newlines and control characters are dropped
            surface->queue_draw();
          });
          return;
        }
        if (ev.sym == XKB_KEY_Insert) return;
        goto default_char;
      case XKB_KEY_u:
        if (ev.mods & kCtrl) {
          wipe();
          break;
        }
        [[fallthrough]];
      default:
      default_char:
        if (ev.mods & (kCtrl | kAlt | kSuper)) return;
        if (ev.utf8.empty() || static_cast<unsigned char>(ev.utf8[0]) < 0x20 || ev.utf8[0] == 0x7f)
          return;
        if (password.size() + ev.utf8.size() > kMaxPassword) return;
        password += ev.utf8;
        error.clear();
        break;
    }
    surface->queue_draw();
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (!pressed || b != kBtnLeft || busy) return;
    if (button.hit(x, y)) attempt_unlock();
    else if (eye.hit(x, y)) {
      reveal = !reveal;
      surface->queue_draw();
    }
  }
};

}  // namespace

int main() {
  fleetwm::tune_malloc_for_low_rss();

  Locker L;
  L.pal = load_palette(load_theme_config());
  L.username = current_username();
  L.password.reserve(kMaxPassword + 1);
  L.ipc.connect();  // best-effort; attempt_unlock() re-checks is_connected()

  if (!L.app.connect()) return 1;

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  // All four edges anchored and 0x0: a fullscreen surface that fully occludes
  // every toplevel and the bar, so hit-testing can never reach a real window
  // while locked.
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.exclusive_zone = -1;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
  cfg.name = "fleetwm-locker";
  L.surface = std::make_unique<Surface>(L.app, cfg);
  L.surface->on_draw = [&L](cairo_t* cr, int w, int h) { L.draw(cr, w, h); };
  L.surface->on_key = [&L](const KeyEvent& e) { L.on_key(e); };
  L.surface->on_button = [&L](double x, double y, uint32_t b, bool p) { L.on_button(x, y, b, p); };
  L.surface->on_closed = [&L] { L.app.quit(); };

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  L.app.watch_fd(sfd, [&L] { L.app.quit(); });

  L.app.run();
  L.wipe();
  return 0;
}
