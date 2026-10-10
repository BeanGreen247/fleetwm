// fleetwm-ctxmenu: the taskbar's right-click menu. A small OVERLAY layer surface next to the taskbar with a list of items; a click on
// one runs it and closes the menu, Escape or a click anywhere else closes it (the compositor does that for its namespace).
//
//   fleetwm-ctxmenu --edge bottom|top|left|right --at N --item 'Label|exec|prog args' --item '-' --item 'Label|ipc|COMMAND'
//                   --item 'Label|pin|app.desktop' --item 'Label|unpin|app.desktop'
// `--at` is where the click was along the taskbar (x for a horizontal one, y for a vertical one), in output pixels.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "bar_config.hpp"
#include "ctx_menu.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "ipc_client.hpp"
#include "malloc_tuning.hpp"
#include "menu_metrics.hpp"
#include "popup_namespaces.hpp"
#include "popup_spot.hpp"
#include "prewarm.hpp"
#include "quit_signals.hpp"
#include "single_instance.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr uint32_t kBtnLeft = 0x110;
struct Menu {
  App app;
  Palette pal;
  MenuTheme theme;
  std::unique_ptr<Surface> surface;
  std::vector<MenuItem> items;
  std::vector<double> top;  // y of each row inside the card
  int hover = -1;
  int margin = 12;  // room around the card for its shadow

  void layout() {
    top.clear();
    double y = menu::kOuterPadding;
    for (const MenuItem& i : items) {
      top.push_back(y);
      y += i.kind == MenuItem::Kind::Separator ? menu::kSeparatorHeight : menu::kRowHeight;
    }
  }

  int item_at(double x, double y) const {
    const double cx = x - margin, cy = y - margin;
    for (size_t i = 0; i < items.size(); ++i) {
      if (items[i].kind == MenuItem::Kind::Separator) continue;
      if (cy >= top[i] && cy < top[i] + menu::kRowHeight && cx >= 0 && cx < surface_w() - 2.0 * margin) return static_cast<int>(i);
    }
    return -1;
  }
  int surface_w() const { return card_w + 2 * margin; }
  int card_w = 160, card_h = 0;

  void draw(cairo_t* cr, int, int) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const double x0 = margin, y0 = margin, r = theme.radius;
    // A soft shadow, then the card with a thin rim.
    for (int i = 4; i >= 1; --i) {
      rounded_rect(cr, x0 - i + 0.5, y0 - i + menu::kShadowOffset - i * 0.25, card_w + 2 * i - 1, card_h + 2 * i - 1, r + i);
      set_source(cr, theme.shadow);
      cairo_fill(cr);
    }
    rounded_rect(cr, x0, y0, card_w, card_h, r);
    set_source(cr, theme.background);
    cairo_fill(cr);
    rounded_rect(cr, x0 + 0.5, y0 + 0.5, card_w - 1, card_h - 1, r);
    set_source(cr, theme.border);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    for (size_t i = 0; i < items.size(); ++i) {
      const double y = y0 + top[i];
      if (items[i].kind == MenuItem::Kind::Separator) {
        set_source(cr, Color{theme.secondary.r, theme.secondary.g, theme.secondary.b, 0.28});
        cairo_rectangle(cr, x0 + 8, y + menu::kSeparatorHeight / 2, card_w - 16, 1);
        cairo_fill(cr);
        continue;
      }
      const bool on = static_cast<int>(i) == hover;
      if (on) {
        rounded_rect(cr, x0 + menu::kItemInset, y, card_w - 2 * menu::kItemInset, menu::kRowHeight, theme.item_radius);
        set_source(cr, theme.hover);
        cairo_fill(cr);
      }
      const TextExtents te = measure_text(cr, items[i].label, menu::kFontPx);
      draw_text(cr, items[i].label, x0 + menu::kTextPadding, y + (menu::kRowHeight - te.height) / 2 + te.ascent, menu::kFontPx,
                on ? theme.hover_text : theme.text);
    }
  }

  void set_hover(int i) {
    if (i == hover) return;
    hover = i;
    surface->queue_draw();
  }

  void run(const MenuItem& it) {
    switch (it.kind) {
      case MenuItem::Kind::Exec: {
        const std::vector<std::string> argv = split_exec_arg(it.arg);
        if (!argv.empty()) spawn_detached(argv);
        break;
      }
      case MenuItem::Kind::Ipc: {
        IpcClient ipc;
        if (ipc.connect()) ipc.send_command(it.arg);
        break;
      }
      case MenuItem::Kind::Pin:
      case MenuItem::Kind::Unpin: {
        BarConfig cfg = load_bar_config();
        if (apply_pin(&cfg, it.arg, it.kind == MenuItem::Kind::Pin)) {
          try {
            save_bar_config(cfg);
          } catch (const std::exception& e) {
            std::fprintf(stderr, "fleetwm-ctxmenu: %s\n", e.what());
          }
        }
        break;
      }
      case MenuItem::Kind::Separator: break;
    }
    app.quit();
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    auto step = [&](int d) {
      int i = hover;
      for (size_t n = 0; n < items.size(); ++n) {
        i = (i + d + static_cast<int>(items.size())) % static_cast<int>(items.size());
        if (items[static_cast<size_t>(i)].kind != MenuItem::Kind::Separator) break;
      }
      set_hover(i);
    };
    switch (ev.sym) {
      case XKB_KEY_Escape: app.quit(); break;
      case XKB_KEY_Down:
      case XKB_KEY_Tab: step(+1); break;
      case XKB_KEY_Up:
      case XKB_KEY_ISO_Left_Tab: step(-1); break;
      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
      case XKB_KEY_space:
        if (hover >= 0) run(items[static_cast<size_t>(hover)]);
        break;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-ctxmenu", "--edge bottom|top|left|right --at N --item 'Label|exec|prog args' ...")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  fleetwm::prewarm::start("fleetwm-ctxmenu");

  // A second right-click replaces the menu instead of stacking another one.
  const std::string pidfile = single_instance_pid_file("fleetwm-ctxmenu");
  toggle_running_instance(pidfile, "fleetwm-ctxmenu");

  Menu M;
  TaskbarPosition edge = TaskbarPosition::Bottom;
  int at = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--edge" && i + 1 < argc) edge = taskbar_position_from_string(argv[++i]);
    else if (a == "--at" && i + 1 < argc) at = std::atoi(argv[++i]);
    else if (a == "--item" && i + 1 < argc) {
      if (auto it = parse_menu_item(argv[++i])) M.items.push_back(*it);
    }
  }
  if (M.items.empty()) return 2;
  M.pal = load_palette(load_theme_config());
  M.theme = menu_theme(M.pal);
  M.layout();
  if (!M.app.connect()) return 1;

  // Size from the longest label.
  cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* sc = cairo_create(scratch);
  int widest = 0;
  for (const MenuItem& i : M.items) widest = std::max(widest, static_cast<int>(measure_text(sc, i.label, menu::kFontPx).width));
  cairo_destroy(sc);
  cairo_surface_destroy(scratch);
  const MenuSize size = ctx_menu_size(M.items, widest);
  M.card_w = size.w;
  M.card_h = size.h;
  const int sw = M.card_w + 2 * M.margin, sh = M.card_h + 2 * M.margin;

  int screen_w = 1920, screen_h = 1080;
  if (const OutputInfo* o = M.app.preferred_output()) {
    const int sc2 = std::max(1, o->scale);
    screen_w = o->width / sc2;
    screen_h = o->height / sc2;
  }
  // The surface is the card plus its shadow margin; place it so that the card sits where the menu would.
  const MenuSpot spot = ctx_menu_spot(edge, at, sw, sh, screen_w, screen_h, 6 - M.margin, 8 - M.margin);

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = ((spot.anchor & kAnchorTop) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : 0) | ((spot.anchor & kAnchorBottom) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : 0) |
               ((spot.anchor & kAnchorLeft) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT : 0) | ((spot.anchor & kAnchorRight) ? ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT : 0);
  cfg.width = sw;
  cfg.height = sh;
  cfg.exclusive_zone = -1;
  cfg.margin_top = spot.top;
  cfg.margin_right = spot.right;
  cfg.margin_bottom = spot.bottom;
  cfg.margin_left = spot.left;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
  cfg.name = fleetwm::kCtxMenuNamespace;  // the compositor closes it on a press outside (popup_namespaces.hpp)
  M.surface = std::make_unique<Surface>(M.app, cfg);
  M.surface->on_draw = [&M](cairo_t* cr, int w, int h) { M.draw(cr, w, h); };
  M.surface->on_key = [&M](const KeyEvent& e) { M.on_key(e); };
  M.surface->on_motion = [&M](double x, double y) { M.set_hover(M.item_at(x, y)); };
  M.surface->on_button = [&M](double x, double y, uint32_t b, bool pressed) {
    if (pressed || b != kBtnLeft) return;  // act on release, like a button
    const int i = M.item_at(x, y);
    if (i >= 0) M.run(M.items[static_cast<size_t>(i)]);
  };
  M.surface->on_leave = [&M] { M.set_hover(-1); };
  M.surface->on_closed = [&M] { M.app.quit(); };

  write_pid_file(pidfile);
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  M.app.watch_fd(sfd, [&M] { M.app.quit(); });

  M.app.run();
  remove_pid_file(pidfile);
  return 0;
}
