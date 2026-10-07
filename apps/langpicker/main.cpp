// fleetwm-langpicker: a checklist of every keyboard layout (with its variants) and every
// language (locale) the system knows. Tick what you want and press Done: the choice is saved
// to keyboard.toml (the compositor and the bar pick it up at once), then the locales are
// generated and the font cache rebuilt in the background so everything works right away.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "quit_signals.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "keyboard_config.hpp"
#include "malloc_tuning.hpp"
#include "paths_config.h"
#include "theme.hpp"
#include "ui.hpp"
#include "xkb_rules.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kWindowW = 780, kWindowH = 680;

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

struct Picker {
  App app;
  ThemeConfig theme;
  Palette pal;
  Ui ui{pal};
  std::unique_ptr<Surface> surface;

  KeyboardConfig config;
  std::vector<LayoutInfo> layouts;
  std::vector<LocaleInfo> locales;
  std::set<std::string> layout_on, locale_on;  // layout specs ("cz:qwerty") and locale codes
  int tab = 0;
  std::string search, last_search;
  double scroll[2] = {0, 0};

  void load() {
    theme = load_theme_config();
    pal = load_palette(theme);
    ui.set_palette(pal);
    config = load_keyboard_config();
    layouts = load_xkb_layouts();
    locales = load_locales();
    for (const KeyboardLayout& l : config.layouts) layout_on.insert(layout_spec(l));
    for (const std::string& l : config.locales) locale_on.insert(l);
  }

  void redraw() { surface->queue_draw(); }

  bool matches(const std::string& a, const std::string& b) const {
    if (search.empty()) return true;
    const std::string q = lower(search);
    return lower(a).find(q) != std::string::npos || lower(b).find(q) != std::string::npos;
  }

  void done() {
    // Keep the order the user already had; new layouts go after them in list order.
    KeyboardConfig out = config;
    out.layouts.clear();
    for (const KeyboardLayout& l : config.layouts)
      if (layout_on.count(layout_spec(l))) out.layouts.push_back(l);
    for (const LayoutInfo& info : layouts) {
      KeyboardLayout l{info.layout, info.variant};
      if (layout_on.count(layout_spec(l)) &&
          std::find(out.layouts.begin(), out.layouts.end(), l) == out.layouts.end())
        out.layouts.push_back(l);
    }
    if (out.layouts.empty()) out.layouts.push_back({"us", ""});
    out.locales.assign(locale_on.begin(), locale_on.end());
    const bool changed = !(out == config);
    try {
      save_keyboard_config(out);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "fleetwm-langpicker: %s\n", e.what());
    }
    if (changed || !out.locales.empty()) rebuild(out.locales);
    app.quit();
  }

  // Generate the picked locales and rebuild the font cache. The system part needs root, which
  // goes through polkit (a rule installed with Fleetwm allows it for you without a password);
  // the per-user font cache is rebuilt as you. Runs on its own after this window has closed.
  static void rebuild(const std::vector<std::string>& wanted) {
    static const char* kScript =
        "notify-send -a Fleetwm 'Languages' 'Building locales and the font cache...' 2>/dev/null;\n"
        "ok=1\n"
        "if [ $# -gt 0 ]; then pkexec " FLEETWM_BINDIR "/fleetwm-locale-build \"$@\" || ok=0;\n"
        "else pkexec " FLEETWM_BINDIR "/fleetwm-locale-build || ok=0; fi\n"
        "fc-cache -f 2>/dev/null\n"
        "if [ $ok = 1 ]; then notify-send -a Fleetwm 'Languages' 'Locales and fonts are ready.' 2>/dev/null;\n"
        "else notify-send -a Fleetwm 'Languages' 'Could not build the locales (no permission?). Run: sudo fleetwm-locale-build' 2>/dev/null; fi\n";
    std::vector<std::string> argv{"sh", "-c", kScript, "sh"};
    for (const std::string& l : wanted) argv.push_back(l);
    spawn_detached(argv);
  }

  void draw(cairo_t*, int w, int h) {
    ui.begin(cairo_ptr, w, h);
    ui.set_margins(24, 18, 24);
    ui.set_label_width(160);
    ui.title("Languages and keyboard layouts");
    ui.label("Tick everything you want to use, then press Done. Locales and the font cache are built for you.", true);
    ui.newline();
    ui.space(4);
    ui.tabs({"Keyboard layouts", "Languages"}, &tab);
    ui.newline();
    ui.text_entry(&search, 300);
    if (search != last_search) {  // a new filter: start the list from the top
      last_search = search;
      scroll[0] = scroll[1] = 0;
    }
    ui.same_line();
    if (ui.button("Done", true, true)) done();
    ui.same_line();
    if (ui.button("Cancel")) app.quit();
    ui.newline();
    const size_t selected = tab == 0 ? layout_on.size() : locale_on.size();
    ui.label(std::to_string(selected) + " selected" + (search.empty() ? "" : "  -  filtered by \"" + search + "\""), true);
    ui.newline();
    ui.space(4);

    const double top = ui.content_bottom();
    const double view_h = std::max(80.0, h - top - 12.0);
    ui.begin_scroll({0, top, static_cast<double>(w), view_h}, &scroll[tab]);
    if (tab == 0) draw_layouts(); else draw_locales();
    ui.space(16);
    ui.end_scroll();
    ui.end();
    if (ui.wants_another_frame()) redraw();
  }

  void draw_layouts() {
    if (layouts.empty()) {
      ui.paragraph("No keyboard layout list was found (is xkb-data installed?).");
      return;
    }
    auto row = [&](const LayoutInfo& info) {
      const std::string spec = info.variant.empty() ? info.layout : info.layout + ":" + info.variant;
      bool on = layout_on.count(spec) > 0;
      const std::string text = info.description + "   [" + spec + "]";
      if (ui.checkbox(text, &on)) {
        if (on) layout_on.insert(spec); else layout_on.erase(spec);
      }
      ui.newline();
    };
    bool any_selected = false;
    for (const LayoutInfo& info : layouts) {  // the ticked ones first so they are easy to find again
      const std::string spec = info.variant.empty() ? info.layout : info.layout + ":" + info.variant;
      if (!layout_on.count(spec) || !matches(info.description, spec)) continue;
      if (!any_selected) ui.section("Selected");
      any_selected = true;
      row(info);
    }
    if (any_selected) ui.section("All layouts");
    for (const LayoutInfo& info : layouts) {
      const std::string spec = info.variant.empty() ? info.layout : info.layout + ":" + info.variant;
      if (layout_on.count(spec) || !matches(info.description, spec)) continue;
      row(info);
    }
  }

  void draw_locales() {
    if (locales.empty()) {
      ui.paragraph("No locale list was found (is the 'locales' package installed?).");
      return;
    }
    auto row = [&](const LocaleInfo& info) {
      bool on = locale_on.count(info.code) > 0;
      const std::string text = (info.name.empty() ? info.code : info.name + "   [" + info.code + "]");
      if (ui.checkbox(text, &on)) {
        if (on) locale_on.insert(info.code); else locale_on.erase(info.code);
      }
      ui.newline();
    };
    bool any_selected = false;
    for (const LocaleInfo& info : locales) {
      if (!locale_on.count(info.code) || !matches(info.name, info.code)) continue;
      if (!any_selected) ui.section("Selected");
      any_selected = true;
      row(info);
    }
    if (any_selected) ui.section("All languages");
    for (const LocaleInfo& info : locales)
      if (!locale_on.count(info.code) && matches(info.name, info.code)) row(info);
  }

  cairo_t* cairo_ptr = nullptr;
};

}  // namespace

int main() {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  fleetwm::tune_malloc_for_low_rss();
  signal(SIGCHLD, SIG_IGN);

  Picker P;
  P.load();
  if (!P.app.connect()) return 1;

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.LangPicker";
  cfg.title = "Languages and keyboard layouts";
  cfg.width = kWindowW;
  cfg.height = kWindowH;
  cfg.min_width = 560;
  cfg.min_height = 420;
  P.surface = std::make_unique<Surface>(P.app, cfg);
  P.surface->on_draw = [&P](cairo_t* cr, int w, int h) {
    P.cairo_ptr = cr;
    P.draw(cr, w, h);
  };
  P.surface->on_motion = [&P](double x, double y) {
    P.ui.pointer_motion(x, y);
    P.redraw();
  };
  P.surface->on_leave = [&P] {
    P.ui.pointer_leave();
    P.redraw();
  };
  P.surface->on_button = [&P](double x, double y, uint32_t b, bool p) {
    P.ui.pointer_button(x, y, b, p);
    P.redraw();
  };
  P.surface->on_scroll = [&P](double, double dy) {
    P.ui.scroll(dy / 10.0);
    P.redraw();
  };
  P.surface->on_key = [&P](const KeyEvent& e) {
    if (e.pressed && ((e.sym == XKB_KEY_v && (e.mods & kCtrl)) || (e.sym == XKB_KEY_Insert && (e.mods & kShift))) && P.ui.wants_paste()) {
      P.app.paste_text([&P](const std::string& text) {
        P.ui.paste(text);
        P.redraw();
      });
      return;
    }
    if (e.pressed && e.sym == XKB_KEY_Escape && !P.ui.wants_paste()) {
      P.app.quit();
      return;
    }
    P.ui.key(e);
    P.redraw();
  };
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
