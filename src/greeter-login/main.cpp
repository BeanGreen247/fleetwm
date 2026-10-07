// fleetwm-greeter-login: the login card shown by fleetwm-greet. A fullscreen
// xdg_toplevel inside the greeter's own tiny compositor, drawn with cairo on
// fleetkit (no GTK). Two pages: a user picker (tiles + "Other User") and the
// login page (avatar, name or username entry, password entry with peek,
// submit). Credentials go to fleetwm-greet over the inherited IPC socket
// (FLEETWM_GREETER_IPC_FD); an AuthFailed reply is shown inline.

#include <pwd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "quit_signals.hpp"
#include "fleetkit.hpp"
#include "image.hpp"
#include "login_ipc.hpp"
#include "malloc_tuning.hpp"
#include "theme.hpp"
#include "wallpaper_config.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr uint32_t kBtnLeft = 0x110;
constexpr size_t kMaxField = 255;
constexpr double kFont = 14.67;

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

std::vector<std::string> list_login_users() {
  std::vector<std::string> users;
  setpwent();
  while (passwd* pw = getpwent()) {
    if (pw->pw_uid < 1000 || pw->pw_uid == 65534) continue;
    if (pw->pw_shell == nullptr || pw->pw_shell[0] == '\0') continue;
    const std::string shell = pw->pw_shell;
    if (shell == "/usr/sbin/nologin" || shell == "/sbin/nologin" || shell == "/bin/false" ||
        shell == "/usr/bin/false")
      continue;
    users.emplace_back(pw->pw_name);
  }
  endpwent();
  std::sort(users.begin(), users.end());
  return users;
}

size_t prev_len(const std::string& s, size_t pos) {
  size_t i = pos;
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
  return i > 0 ? pos - (i - 1) : 0;
}

struct Login {
  App app;
  Palette pal;
  ThemeConfig theme;
  WallpaperConfig wallpaper;
  std::unique_ptr<Surface> surface;
  int ipc_fd = -1;
  int ipc_watch = 0;

  std::vector<std::string> users;
  bool picker = true;
  std::string selected;  // "" = Other User
  std::string username;  // typed name for Other User
  std::string password;
  std::string error;
  bool busy = false, reveal = false;
  int focus = 1;  // 0 = username entry, 1 = password entry
  int hover = -1;  // picker tile index, or -2 power reboot, -3 power off, -4 back, -5 submit, -6 eye
  int tile_focus = 0;

  cairo_surface_t* bg = nullptr;
  int bg_w = 0, bg_h = 0, bg_scale = 0;

  Rect tiles[32];
  Rect back, eye, submit, reboot, poweroff, user_entry, pass_entry;
  int ntiles = 0;

  ~Login() {
    if (bg) cairo_surface_destroy(bg);
    wipe();
  }

  void wipe() {
    if (!password.empty()) explicit_bzero(password.data(), password.size());
    password.clear();
  }

  // ------------------------------------------------------------ drawing --
  void ensure_background(int w, int h, int scale) {
    if (wallpaper.use_solid_color || wallpaper.path.empty()) return;
    if (bg && bg_w == w && bg_h == h && bg_scale == scale) return;
    if (bg) {
      cairo_surface_destroy(bg);
      bg = nullptr;
    }
    Image img = load_image(wallpaper.path);
    if (!img.ok()) return;
    const int pw = w * scale, ph = h * scale;
    bg = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pw, ph);
    cairo_surface_flush(bg);
    render_cover(img, pw, ph, cairo_image_surface_get_data(bg));
    cairo_surface_mark_dirty(bg);
    cairo_surface_set_device_scale(bg, scale, scale);
    bg_w = w;
    bg_h = h;
    bg_scale = scale;
  }

  void avatar(cairo_t* cr, double cx, double cy, double size, bool hot) {
    const double x = cx - size / 2, y = cy - size / 2;
    rounded_rect(cr, x, y, size, size, pal.rounded ? 10 : 0);
    set_source(cr, pal.bg_secondary);
    cairo_fill_preserve(cr);
    set_source(cr, hot ? pal.accent : pal.bg_primary);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    // person glyph: head + shoulders
    set_source(cr, pal.fg_secondary);
    const double k = size / 88.0;
    cairo_arc(cr, cx, cy - 9 * k, 12 * k, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_save(cr);
    rounded_rect(cr, x + 2, y + 2, size - 4, size - 4, pal.rounded ? 9 : 0);
    cairo_clip(cr);
    cairo_arc(cr, cx, cy + 32 * k, 23 * k, M_PI, 2 * M_PI);
    cairo_line_to(cr, cx + 23 * k, cy + 48 * k);
    cairo_line_to(cr, cx - 23 * k, cy + 48 * k);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
  }

  void power_glyph(cairo_t* cr, bool is_reboot, double cx, double cy, const Color& c) {
    cairo_save(cr);
    cairo_new_path(cr);
    set_source(cr, c);
    cairo_set_line_width(cr, 1.7);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    if (is_reboot) {
      cairo_arc(cr, cx, cy, 7, -M_PI / 3, 4 * M_PI / 3 + M_PI / 6);
      cairo_stroke(cr);
      cairo_move_to(cr, cx + 3.5, cy - 9.5);
      cairo_line_to(cr, cx + 4, cy - 5.2);
      cairo_line_to(cr, cx + 8.2, cy - 5.8);
      cairo_stroke(cr);
    } else {
      cairo_arc(cr, cx, cy + 0.5, 7.5, -M_PI / 2 + 0.6, -M_PI / 2 - 0.6 + 2 * M_PI);
      cairo_stroke(cr);
      cairo_move_to(cr, cx, cy - 9);
      cairo_line_to(cr, cx, cy - 1);
      cairo_stroke(cr);
    }
    cairo_restore(cr);
  }

  void power_row(cairo_t* cr, double cx, double y) {
    reboot = {cx - 34, y, 32, 32};
    poweroff = {cx + 2, y, 32, 32};
    for (int i = 0; i < 2; ++i) {
      const Rect& r = i == 0 ? reboot : poweroff;
      const bool hot = hover == (i == 0 ? -2 : -3);
      rounded_rect(cr, r.x, r.y, r.w, r.h, 16);
      set_source(cr, hot ? pal.bg_secondary : Color{0, 0, 0, 0});
      cairo_fill(cr);
      power_glyph(cr, i == 0, r.x + 16, r.y + 16, hot ? pal.accent : pal.fg_secondary);
    }
  }

  void entry_box(cairo_t* cr, const Rect& r, const std::string& text, const std::string& placeholder,
                 bool focused, bool password_mode) {
    rounded_rect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, pal.rounded ? 8 : 0);
    set_source(cr, pal.bg_secondary);
    cairo_fill_preserve(cr);
    set_source(cr, focused ? pal.accent : pal.bg_secondary);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_save(cr);
    cairo_rectangle(cr, r.x + 6, r.y, r.w - 12 - (password_mode ? 26 : 0), r.h);
    cairo_clip(cr);
    const TextExtents te = measure_text(cr, "Ag", kFont);
    const double base = r.y + (r.h - te.height) / 2 + te.ascent;
    double px = r.x + 10;
    if (text.empty() && !focused && !placeholder.empty()) {
      draw_text(cr, placeholder, px, base, kFont, with_alpha(pal.fg_secondary, 0.7));
    } else if (password_mode && !reveal) {
      set_source(cr, pal.fg_primary);
      size_t chars = 0;
      for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++chars;
      for (size_t i = 0; i < chars; ++i) {
        cairo_arc(cr, px + 4, r.y + r.h / 2, 3.5, 0, 2 * M_PI);
        cairo_fill(cr);
        px += 11;
      }
    } else {
      px += draw_text(cr, text, px, base, kFont, pal.fg_primary);
    }
    if (focused) {
      set_source(cr, pal.fg_primary);
      cairo_rectangle(cr, px + 1, r.y + 8, 1.2, r.h - 16);
      cairo_fill(cr);
    }
    cairo_restore(cr);
  }

  static Color with_alpha(Color c, double a) {
    c.a = a;
    return c;
  }

  void draw_picker(cairo_t* cr, int w, int h) {
    ntiles = 0;
    const double tile = 88, name_h = 26, gap = 28;
    const int count = static_cast<int>(users.size()) + 1;
    const double row_w = count * tile + (count - 1) * gap;
    const double total_h = tile + 8 + name_h + 20 + 22 + 20 + 32;
    double x = (w - row_w) / 2, y = (h - total_h) / 2;
    for (int i = 0; i < count && i < 32; ++i) {
      const bool is_other = i == count - 1;
      const std::string name = is_other ? "Other User" : users[static_cast<size_t>(i)];
      tiles[i] = {x, y, tile, tile + 8 + name_h};
      const bool hot = hover == i || (hover < 0 && tile_focus == i && false);
      avatar(cr, x + tile / 2, y + tile / 2, tile, hot || tile_focus == i);
      const TextExtents te = measure_text(cr, name, kFont);
      draw_text(cr, name, x + (tile - te.width) / 2, y + tile + 8 + te.ascent, kFont, pal.fg_primary);
      x += tile + gap;
      ++ntiles;
    }
    y += tile + 8 + name_h + 20;
    const TextExtents be = measure_text(cr, "fleetwm", kFont * 1.1, true);
    draw_text(cr, "fleetwm", (w - be.width) / 2, y + be.ascent, kFont * 1.1, pal.fg_secondary, true);
    y += 22 + 20;
    power_row(cr, w / 2.0, y);
  }

  void draw_login(cairo_t* cr, int w, int h) {
    const bool locked = !selected.empty();
    const double avatar_s = 124, entry_w = 222, entry_h = 40, btn = 40;
    const double row_w = entry_w + 6 + btn;
    double total_h = avatar_s + 12 + (locked ? 30 : entry_h) + 10 + entry_h + (error.empty() ? 0 : 10 + 20) + 24 + 32;
    double y = (h - total_h) / 2;
    const double cx = w / 2.0;

    // back button (top-left of the card) when there is a picker to go back to
    back = {};
    if (!users.empty()) {
      back = {cx - row_w / 2, y - 44, 44, 34};
      if (y - 44 < 8) back.y = 8;
      rounded_rect(cr, back.x, back.y, back.w, back.h, pal.rounded ? 8 : 0);
      set_source(cr, hover == -4 ? mix(pal.bg_secondary, pal.accent, 0.3) : pal.bg_secondary);
      cairo_fill(cr);
      set_source(cr, pal.fg_primary);
      cairo_set_line_width(cr, 1.8);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_move_to(cr, back.x + 24, back.y + 10);
      cairo_line_to(cr, back.x + 18, back.y + 17);
      cairo_line_to(cr, back.x + 24, back.y + 24);
      cairo_stroke(cr);
    }

    avatar(cr, cx, y + avatar_s / 2, avatar_s, false);
    y += avatar_s + 12;
    user_entry = {};
    if (locked) {
      const TextExtents te = measure_text(cr, selected, kFont * 1.3, true);
      draw_text(cr, selected, cx - te.width / 2, y + te.ascent + 2, kFont * 1.3, pal.fg_primary, true);
      y += 30;
    } else {
      user_entry = {cx - row_w / 2, y, row_w, entry_h};
      entry_box(cr, user_entry, username, "Username", focus == 0, false);
      y += entry_h;
    }
    y += 10;
    pass_entry = {cx - row_w / 2, y, entry_w, entry_h};
    entry_box(cr, pass_entry, password, "", focus == 1, true);
    eye = {pass_entry.x + pass_entry.w - 28, pass_entry.y + 6, 24, entry_h - 12};
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
    submit = {pass_entry.x + entry_w + 6, y, btn, entry_h};
    rounded_rect(cr, submit.x, submit.y, submit.w, submit.h, pal.rounded ? 8 : 0);
    set_source(cr, busy ? pal.fg_secondary : (hover == -5 ? mix(pal.accent, pal.fg_primary, 0.2) : pal.accent));
    cairo_fill(cr);
    set_source(cr, pal.bg_primary);
    cairo_set_line_width(cr, 2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, submit.x + 17, submit.y + 12);
    cairo_line_to(cr, submit.x + 24, submit.y + 20);
    cairo_line_to(cr, submit.x + 17, submit.y + 28);
    cairo_stroke(cr);
    y += entry_h;
    if (!error.empty()) {
      y += 10;
      const TextExtents ee = measure_text(cr, error, 14);
      draw_text(cr, error, cx - ee.width / 2, y + ee.ascent, 14, parse_color("#f38ba8"));
      y += 20;
    }
    y += 24;
    power_row(cr, cx, y);
  }

  static Color mix(const Color& a, const Color& b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
  }

  void draw(cairo_t* cr, int w, int h) {
    set_source(cr, pal.bg_primary);
    cairo_paint(cr);
    ensure_background(w, h, surface->scale());
    if (bg) {
      cairo_set_source_surface(cr, bg, 0, 0);
      cairo_paint(cr);
    } else if (wallpaper.use_solid_color) {
      set_source(cr, parse_color(wallpaper.solid_color, pal.bg_primary));
      cairo_paint(cr);
    }
    if (picker) draw_picker(cr, w, h);
    else draw_login(cr, w, h);
  }

  // --------------------------------------------------------------- logic --
  void show_login(const std::string& user) {
    picker = false;
    selected = user;
    username.clear();
    wipe();
    error.clear();
    busy = false;
    reveal = false;
    focus = user.empty() ? 0 : 1;
    surface->queue_draw();
  }

  void show_picker() {
    picker = true;
    wipe();
    error.clear();
    busy = false;
    surface->queue_draw();
  }

  void attempt_login() {
    if (busy) return;
    const std::string& user = selected.empty() ? username : selected;
    if (user.empty()) return;
    busy = true;
    greeter_ipc::send_login_attempt(ipc_fd, user, password);
    surface->queue_draw();
  }

  void show_error(const std::string& msg) {
    error = msg;
    wipe();
    busy = false;
    focus = 1;
    surface->queue_draw();
  }

  void set_hover(int h) {
    if (h != hover) {
      hover = h;
      surface->queue_draw();
    }
  }

  int hit_test(double x, double y) const {
    if (picker) {
      for (int i = 0; i < ntiles; ++i)
        if (tiles[i].hit(x, y)) return i;
    } else {
      if (back.w > 0 && back.hit(x, y)) return -4;
      if (submit.hit(x, y)) return -5;
      if (eye.hit(x, y)) return -6;
    }
    if (reboot.hit(x, y)) return -2;
    if (poweroff.hit(x, y)) return -3;
    return -1;
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (!pressed || b != kBtnLeft) return;
    const int t = hit_test(x, y);
    if (t >= 0 && picker) {
      show_login(t == static_cast<int>(users.size()) ? "" : users[static_cast<size_t>(t)]);
    } else if (t == -2) {
      greeter_ipc::send_power_action(ipc_fd, "reboot");
    } else if (t == -3) {
      greeter_ipc::send_power_action(ipc_fd, "poweroff");
    } else if (t == -4) {
      show_picker();
    } else if (t == -5) {
      attempt_login();
    } else if (t == -6) {
      reveal = !reveal;
      surface->queue_draw();
    } else if (!picker) {
      if (user_entry.w > 0 && user_entry.hit(x, y)) {
        focus = 0;
        surface->queue_draw();
      } else if (pass_entry.hit(x, y)) {
        focus = 1;
        surface->queue_draw();
      }
    }
  }

  std::string& field() { return focus == 0 && selected.empty() ? username : password; }

  void type_text(const std::string& s) {
    std::string& f = field();
    for (char c : s) {
      if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) continue;
      if (f.size() >= kMaxField) break;
      f += c;
    }
    error.clear();
    surface->queue_draw();
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    if (picker) {
      const int count = static_cast<int>(users.size()) + 1;
      switch (ev.sym) {
        case XKB_KEY_Left: tile_focus = (tile_focus + count - 1) % count; break;
        case XKB_KEY_Right:
        case XKB_KEY_Tab: tile_focus = (tile_focus + 1) % count; break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
          show_login(tile_focus == static_cast<int>(users.size()) ? "" : users[static_cast<size_t>(tile_focus)]);
          return;
        default: return;
      }
      surface->queue_draw();
      return;
    }
    if (busy) return;
    switch (ev.sym) {
      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
        if (focus == 0 && selected.empty() && !username.empty()) {
          focus = 1;
          surface->queue_draw();
        } else {
          attempt_login();
        }
        return;
      case XKB_KEY_Escape:
        if (!users.empty()) show_picker();
        return;
      case XKB_KEY_Tab:
      case XKB_KEY_ISO_Left_Tab:
        if (selected.empty()) {
          focus = 1 - focus;
          surface->queue_draw();
        }
        return;
      case XKB_KEY_BackSpace: {
        std::string& f = field();
        const size_t n = prev_len(f, f.size());
        if (n) {
          if (&f == &password) explicit_bzero(f.data() + f.size() - n, n);
          f.erase(f.size() - n);
        }
        surface->queue_draw();
        return;
      }
      case XKB_KEY_u:
        if (ev.mods & kCtrl) {
          if (&field() == &password) wipe();
          else field().clear();
          surface->queue_draw();
          return;
        }
        break;
      case XKB_KEY_v:
      case XKB_KEY_Insert:
        if ((ev.sym == XKB_KEY_v && (ev.mods & kCtrl)) || (ev.sym == XKB_KEY_Insert && (ev.mods & kShift))) {
          app.paste_text([this](const std::string& t) { type_text(t); });
          return;
        }
        break;
      default: break;
    }
    if (ev.mods & (kCtrl | kAlt | kSuper)) return;
    if (ev.utf8.empty() || static_cast<unsigned char>(ev.utf8[0]) < 0x20 || ev.utf8[0] == 0x7f) return;
    type_text(ev.utf8);
  }
};

}  // namespace

int main() {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  fleetwm::tune_malloc_for_low_rss();
  const char* fd_env = std::getenv("FLEETWM_GREETER_IPC_FD");
  if (fd_env == nullptr) {
    std::fprintf(stderr, "fleetwm-greeter-login: FLEETWM_GREETER_IPC_FD not set\n");
    return 1;
  }

  Login L;
  L.ipc_fd = std::atoi(fd_env);
  L.theme = load_theme_config();
  L.wallpaper = load_wallpaper_config();
  L.pal = load_palette(L.theme);
  L.users = list_login_users();
  L.password.reserve(kMaxField + 1);
  L.picker = !L.users.empty();
  L.focus = L.users.empty() ? 0 : 1;
  if (!L.app.connect()) return 1;

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.GreeterLogin";
  cfg.title = "fleetwm login";
  cfg.width = 1024;  // initial buffer size; the greeter compositor resizes the window to the output on map
  cfg.height = 768;
  L.surface = std::make_unique<Surface>(L.app, cfg);
  L.surface->on_draw = [&L](cairo_t* cr, int w, int h) { L.draw(cr, w, h); };
  L.surface->on_key = [&L](const KeyEvent& e) { L.on_key(e); };
  L.surface->on_button = [&L](double x, double y, uint32_t b, bool p) { L.on_button(x, y, b, p); };
  L.surface->on_motion = [&L](double x, double y) { L.set_hover(L.hit_test(x, y)); };
  L.surface->on_leave = [&L] { L.set_hover(-1); };
  L.surface->on_closed = [&L] { L.app.quit(); };

  L.ipc_watch = L.app.watch_fd(L.ipc_fd, [&L] {
    greeter_ipc::ServerMessage msg;
    if (!greeter_ipc::recv_server_message(L.ipc_fd, msg)) {
      L.app.unwatch(L.ipc_watch);  // greeter closed the socket: nothing more will come
      L.ipc_watch = 0;
      return;
    }
    if (msg.type == greeter_ipc::ServerMsgType::AuthFailed) L.show_error(msg.message);
  });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  L.app.watch_fd(sfd, [&L] { L.app.quit(); });

  L.app.run();
  return 0;
}
