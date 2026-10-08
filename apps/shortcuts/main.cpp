// fleetwm-shortcuts: a window listing every keyboard shortcut and mouse gesture
// for the active window layout, read from keybinds.toml so remaps show up, with
// a link to the documentation. Alt+Shift+/ opens it; pressing that again (or
// Esc) closes it -- a second launch asks the first to quit.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "quit_signals.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "keybinds_config.hpp"
#include "malloc_tuning.hpp"
#include "prewarm.hpp"
#include "shortcut_list.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "version.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kWindowW = 760, kWindowH = 620;

std::string pid_path() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  return std::string(rt && *rt ? rt : "/tmp") + "/fleetwm-shortcuts.pid";
}

// True (after asking it to quit) when another copy is already open.
bool toggle_existing() {
  std::ifstream in(pid_path());
  long pid = 0;
  if (!(in >> pid) || pid <= 0 || pid == getpid()) return false;
  std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
  std::string name;
  if (!(comm >> name) || name != "fleetwm-shortcu") return false;  // comm is truncated to 15 chars
  kill(static_cast<pid_t>(pid), SIGTERM);
  return true;
}

struct Shortcuts {
  App app;
  ThemeConfig theme;
  KeybindsConfig binds;
  Palette pal;
  Ui ui{pal};
  std::unique_ptr<Surface> surface;
  std::vector<ShortcutEntry> list;
  bool show_inactive = false;
  double scroll = 0;

  void reload() {
    theme = load_theme_config();
    binds = load_keybinds_config();
    pal = load_palette(theme);
    ui.set_palette(pal);
    list = build_shortcut_list(binds, theme.window_layout);
  }

  void redraw() { surface->queue_draw(); }

  void draw(cairo_t* cr, int w, int h) {
    ui.begin(cr, w, h);
    ui.set_margins(28, 22, 28);
    ui.set_label_width(std::min(460.0, w * 0.6));  // long descriptions need room before the keys
    ui.title("Keyboard Shortcuts");
    const bool desktop = theme.window_layout == WindowLayout::Desktop;
    ui.label(desktop ? "Desktop layout: only the terminal and this window have shortcuts; the mouse does the rest."
                     : "Tiling layout: every shortcut below is active.",
             true);
    ui.newline();
    if (desktop) {
      ui.row("Also show Tiling shortcuts");
      ui.toggle(&show_inactive);
      ui.newline();
    }
    ui.space(4);
    if (ui.button("Open documentation", true, true)) spawn_detached({"xdg-open", kDocsUrl});
    ui.same_line();
    if (ui.button("Shortcut reference")) spawn_detached({"xdg-open", kShortcutsDocUrl});
    ui.newline();
    ui.label("Change any key in ~/.config/fleetwm/keybinds.toml (it applies right away).", true);
    ui.newline();
    ui.space(6);

    const double top = ui.content_bottom();
    const double view_h = std::max(80.0, h - top - 16.0);
    ui.begin_scroll({0, top, static_cast<double>(w), view_h}, &scroll);
    std::string section;
    for (const ShortcutEntry& e : list) {
      if (!e.active && !show_inactive) continue;
      if (e.section != section) {
        section = e.section;
        ui.space(6);
        ui.section(section);
      }
      ui.row(e.description);
      ui.label(e.keys, !e.active);
      ui.newline();
    }
    ui.end_scroll();
    ui.end();
    if (ui.wants_another_frame()) surface->queue_draw();
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-shortcuts", "")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  fleetwm::prewarm::start("fleetwm-shortcuts");
  signal(SIGCHLD, SIG_IGN);
  if (toggle_existing()) return 0;
  { std::ofstream(pid_path()) << getpid() << "\n"; }

  Shortcuts S;
  S.reload();
  if (!S.app.connect()) return 1;

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.Shortcuts";
  cfg.title = "Keyboard Shortcuts";
  cfg.width = kWindowW;
  cfg.height = kWindowH;
  cfg.min_width = 520;
  cfg.min_height = 360;
  S.surface = std::make_unique<Surface>(S.app, cfg);
  S.surface->on_draw = [&S](cairo_t* cr, int w, int h) { S.draw(cr, w, h); };
  S.surface->on_motion = [&S](double x, double y) {
    S.ui.pointer_motion(x, y);
    S.redraw();
  };
  S.surface->on_leave = [&S] {
    S.ui.pointer_leave();
    S.redraw();
  };
  S.surface->on_button = [&S](double x, double y, uint32_t b, bool p) {
    S.ui.pointer_button(x, y, b, p);
    S.redraw();
  };
  S.surface->on_scroll = [&S](double, double dy) {
    S.ui.scroll(dy / 10.0);
    S.redraw();
  };
  S.surface->on_key = [&S](const KeyEvent& e) {
    if (e.pressed && e.sym == XKB_KEY_Escape) {
      S.app.quit();
      return;
    }
    S.ui.key(e);
    S.redraw();
  };
  S.surface->on_closed = [&S] { S.app.quit(); };

  // Remapped keys and layout changes show up without reopening the window.
  kit::watch_dirs(S.app, {std::filesystem::path(user_config_path()).parent_path().string()}, [&S] {
    S.reload();
    S.redraw();
  });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  S.app.watch_fd(sfd, [&S] { S.app.quit(); });

  S.app.run();
  unlink(pid_path().c_str());
  return 0;
}
