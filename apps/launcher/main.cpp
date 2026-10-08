// fleetwm-launcher: GTK-free application launcher. A centered OVERLAY
// layer surface with exclusive keyboard focus: a search entry and a list of
// matching applications (name + category hint). Enter launches the selected
// entry; if nothing matches the typed text can be run as a shell command.
// Escape quits. Up/Down (or Ctrl+N/Ctrl+P) move the selection.

#include <pwd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <filesystem>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "quit_signals.hpp"
#include "backdrop.hpp"
#include "bar_config.hpp"
#include "default_apps.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "icon_theme.hpp"
#include "ipc_client.hpp"
#include "popup_namespaces.hpp"
#include "malloc_tuning.hpp"
#include "prewarm.hpp"
#include "mimeapps.hpp"
#include "theme.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "version.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kPad = 14;          // transparent margin around the card (room for the shadow)
constexpr int kCardW = 620;
constexpr int kInner = 10;
constexpr int kEntryH = 48;
constexpr int kRowHeight = 52;
constexpr int kMaxVisibleRows = 7;
constexpr int kFooterH = 30;
constexpr int kCardH = kInner + kEntryH + 8 + kMaxVisibleRows * kRowHeight + kFooterH;
constexpr int kWindowWidth = kCardW + 2 * kPad;
constexpr int kWindowHeight = kCardH + 2 * kPad;
constexpr double kFont = 14.0;
constexpr uint32_t kBtnLeft = 0x110;

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string category_hint(const std::string& categories) {
  static const std::unordered_map<std::string, std::string> labels = {
      {"WebBrowser", "Web Browser"}, {"TerminalEmulator", "Terminal"}, {"Utility", "Utility"},
      {"Development", "Developer Tool"}, {"Game", "Game"}, {"Graphics", "Graphics"},
      {"AudioVideo", "Media"}, {"Office", "Office"}};
  size_t pos = 0;
  while (pos <= categories.size()) {
    size_t e = categories.find(';', pos);
    if (e == std::string::npos) e = categories.size();
    auto it = labels.find(categories.substr(pos, e - pos));
    if (it != labels.end()) return it->second;
    pos = e + 1;
  }
  return "Application";
}

struct Entry {
  DesktopEntry de;
  cairo_surface_t* icon = nullptr;
  bool icon_tried = false;
  std::string hint;
  std::string name_lower, comment_lower;
};

// Number of UTF-8 bytes ending at `pos` that form the previous character.
size_t prev_char_len(const std::string& s, size_t pos) {
  size_t i = pos;
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
  return i > 0 ? pos - (i - 1) : 0;
}
size_t next_char_len(const std::string& s, size_t pos) {
  if (pos >= s.size()) return 0;
  size_t n = 1;
  while (pos + n < s.size() && (static_cast<unsigned char>(s[pos + n]) & 0xC0) == 0x80) ++n;
  return n;
}

std::string pid_path() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  return std::string(rt && *rt ? rt : "/tmp") + "/fleetwm-startmenu.pid";
}

// The start menu is a toggle: if one is already open, ask it to close and report
// true so this launch does nothing (comm is truncated to 15 chars).
bool toggle_existing_start_menu() {
  std::ifstream in(pid_path());
  long pid = 0;
  if (!(in >> pid) || pid <= 0 || pid == getpid()) return false;
  std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
  std::string name;
  if (!(comm >> name) || name != "fleetwm-launche") return false;
  kill(static_cast<pid_t>(pid), SIGTERM);
  return true;
}

// `--default browser|files|editor`: runs the user's default application for that
// job (from mimeapps.list, as set in Settings -> Default Apps) and exits.
int run_default_app(const std::string& kind) {
  static const struct {
    const char* kind;
    const char* mime;
  } kJobs[] = {{"browser", "x-scheme-handler/https"}, {"files", "inode/directory"}, {"editor", "text/plain"}};
  for (const auto& job : kJobs) {
    if (kind != job.kind) continue;
    std::string id = mime_default_for(job.mime);
    if (id.empty()) {  // nothing chosen in Settings: use the first installed app that handles it
      const std::vector<std::string> any = mime_apps_for(job.mime);
      if (!any.empty()) id = any.front();
    }
    for (const DesktopEntry& de : load_desktop_entries()) {
      if (de.id != id) continue;
      std::vector<std::string> argv = exec_argv(de);
      if (argv.empty()) return 1;
      if (de.terminal) {
        std::vector<std::string> t = {"foot", "-e"};
        argv.insert(argv.begin(), t.begin(), t.end());
      }
      return spawn_detached(argv, de.path) ? 0 : 1;
    }
    std::fprintf(stderr, "fleetwm-launcher: no default %s application is set\n", job.kind);
    // Nobody reads stderr when this runs from a shortcut, so say it on screen too.
    spawn_detached({"notify-send", "Fleetwm", std::string("No default ") + job.kind +
                                                  " application is set. Choose one in Settings > Default Apps."});
    return 1;
  }
  std::fprintf(stderr, "fleetwm-launcher: unknown --default kind '%s'\n", kind.c_str());
  return 2;
}

struct Launcher {
  App app;
  // Card geometry. The default is the centered launcher; the start menu is a
  // narrower, taller card placed next to the taskbar (see main()).
  int card_w = kCardW, max_rows = kMaxVisibleRows, row_h = kRowHeight, footer_h = kFooterH;
  int card_h = kCardH;
  double card_x = kPad, card_y = kPad;
  bool start_menu = false;
  std::string edge = "bottom";  // taskbar edge, start menu only
  int inset = 44;               // taskbar thickness, start menu only
  IpcClient ipc;
  struct FooterButton {
    const char* label;
    double x = 0, y = 0, w = 0, h = 0;
  };
  FooterButton footer_buttons[4] = {{"Settings"}, {"Shortcuts"}, {"Lock"}, {"Power"}};
  Palette pal;
  std::unique_ptr<Surface> surface;
  std::vector<Entry> entries;  // sorted by name
  // ---- Windows 7 style start menu (Desktop layout) ----
  struct Hit {
    double x = 0, y = 0, w = 0, h = 0;
    int id = -1;  // right-column item index, or a kHit* code
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  };
  enum { kHitAllPrograms = -2, kHitShutdown = -3, kHitLock = -4, kHitSearch = -5 };
  std::vector<const Entry*> pinned;   // the default apps, shown before "All Programs" is opened
  bool all_programs = false;
  std::vector<Hit> hits;              // clickable things outside the program list, set while drawing
  int hover_hit = -9999;
  struct Place {
    std::string label;
    std::vector<std::string> argv;
  };
  std::vector<Place> places;          // Documents, Pictures, ... Settings
  std::string user_name = "user";
  bool glass = false;                 // theme.toml glass_effects
  cairo_surface_t* backdrop = nullptr;  // the frosted wallpaper, only loaded when glass is on
  std::string query;
  size_t cursor = 0;           // byte offset into query
  std::vector<const Entry*> results;  // nullptr = run-as-command sentinel
  int selected = 0;
  int scroll = 0;              // first visible row
  int hover_row = -1;
  double wheel_accum = 0;

  void load() {
    for (auto& de : load_desktop_entries()) {
      Entry e;
      e.hint = category_hint(de.categories);
      e.name_lower = to_lower(de.name);
      e.comment_lower = to_lower(de.comment);
      e.de = std::move(de);
      entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.de.name < b.de.name; });
    if (start_menu) load_start_menu_extras();
  }

  // What the Windows 7 style start menu needs besides the program list: the pinned programs (the
  // default apps from Settings, a terminal, Settings itself), the places on the right, the user.
  void load_start_menu_extras() {
    auto add_pinned = [&](const Entry* e) {
      if (e && std::find(pinned.begin(), pinned.end(), e) == pinned.end()) pinned.push_back(e);
    };
    auto by_id = [&](const std::string& id) -> const Entry* {
      for (const Entry& e : entries)
        if (e.de.id == id) return &e;
      return nullptr;
    };
    for (const char* mime : {"x-scheme-handler/https", "inode/directory", "text/plain"}) {
      std::string id = mime_default_for(mime);
      if (id.empty()) {
        const std::vector<std::string> any = mime_apps_for(mime);
        if (!any.empty()) id = any.front();
      }
      add_pinned(by_id(id));
    }
    const std::string term = to_lower(load_default_apps_config().terminal_command.substr(0, load_default_apps_config().terminal_command.find(' ')));
    const Entry* terminal = by_id(term + ".desktop");  // "foot.desktop", not "Foot Server"
    if (!terminal)
      for (const Entry& e : entries)
        if (exec_basename(e.de) == term) {
          terminal = &e;
          break;
        }
    add_pinned(terminal);
    add_pinned(by_id("fleetwm-settings.desktop"));

    const char* home = std::getenv("HOME");
    if (home) {
      for (const char* dir : {"Documents", "Pictures", "Music", "Downloads"}) {
        std::error_code ec;
        const std::string path = std::string(home) + "/" + dir;
        if (std::filesystem::is_directory(path, ec)) places.push_back({dir, {"xdg-open", path}});
      }
      places.push_back({"Home folder", {"xdg-open", std::string(home)}});
    }
    places.push_back({"Settings", {"fleetwm-settings"}});
    places.push_back({"Keyboard shortcuts", {"fleetwm-shortcuts"}});
    if (const passwd* pw = getpwuid(getuid())) {
      std::string n = pw->pw_gecos ? pw->pw_gecos : "";
      n = n.substr(0, n.find(','));
      user_name = !n.empty() ? n : pw->pw_name ? pw->pw_name : "user";
    }
  }

  void refresh() {
    results.clear();
    if (query.empty()) {
      if (start_menu && !all_programs) {
        for (const Entry* e : pinned) results.push_back(e);
      } else {
        for (const auto& e : entries) results.push_back(&e);
      }
    } else {
      const std::string needle = to_lower(query);
      std::vector<std::pair<size_t, const Entry*>> scored;
      for (const auto& e : entries) {
        size_t pos = e.name_lower.find(needle);
        if (pos == std::string::npos) {
          pos = e.comment_lower.find(needle);
          if (pos != std::string::npos) pos += e.name_lower.size();  // name matches rank first
        }
        if (pos != std::string::npos) scored.emplace_back(pos, &e);
      }
      std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second->de.name < b.second->de.name;
      });
      for (const auto& [p, e] : scored) results.push_back(e);
      results.push_back(nullptr);  // run-as-command fallback
    }
    selected = 0;
    scroll = 0;
  }

  void ensure_visible() {
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + max_rows) scroll = selected - max_rows + 1;
    scroll = std::max(0, std::min(scroll, std::max(0, static_cast<int>(results.size()) - max_rows)));
  }

  // Start menu: the surface is just the card plus room for its shadow (see main()), placed beside the
  // taskbar by the compositor, so the card sits at a fixed spot inside it.
  static constexpr int kStartPad = 16;
  // Where the surface's top-left corner is on the output, and the output's size: the glass backdrop is
  // lined up with the wallpaper by these. Zero size means unknown (the surface size is used instead).
  double origin_x = 0, origin_y = 0;
  int out_w = 0, out_h = 0;

  void place_card(int, int) {
    if (!start_menu) return;
    card_x = kStartPad;
    card_y = kStartPad;
  }

  int hover_footer = -1;

  void draw_footer_buttons(cairo_t* cr, double lx, double fy, double lw) {
    constexpr double kBtnH = 32, kGap = 8;
    const double bw = (lw - 3 * kGap) / 4, by = fy + (footer_h - kBtnH) / 2;
    for (int i = 0; i < 4; ++i) {
      FooterButton& b = footer_buttons[i];
      b = {b.label, lx + i * (bw + kGap), by, bw, kBtnH};
      rounded_rect(cr, b.x, b.y, b.w, b.h, pal.rounded ? 9 : 3);
      set_source(cr, i == hover_footer ? alpha(pal.accent, 0.28) : pal.bg_secondary);
      cairo_fill(cr);
      const TextExtents te = measure_text(cr, b.label, 12.5, true);
      draw_text(cr, b.label, b.x + (b.w - te.width) / 2, b.y + (b.h - te.height) / 2 + te.ascent, 12.5,
                i == hover_footer ? pal.fg_primary : pal.fg_secondary, true);
    }
  }

  int footer_at(double x, double y) const {
    for (int i = 0; i < 4; ++i) {
      const FooterButton& b = footer_buttons[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    }
    return -1;
  }

  void run_footer(int i) {
    if (i == 0) {
      spawn_detached({"fleetwm-settings"});
    } else if (i == 1) {
      spawn_detached({"fleetwm-shortcuts"});
    } else if (i == 2) {
      if (ipc.connect()) ipc.send_command("LOCK");
    } else {
      spawn_detached({"fleetwm-powermenu"});
    }
    app.quit();
  }

  // ---------------------------------------------------------------- draw --
  cairo_surface_t* icon_for(Entry& e) {
    if (!e.icon_tried) {
      e.icon_tried = true;
      if (!e.de.icon.empty()) e.icon = load_icon(e.de.icon, 64);
    }
    return e.icon;
  }

  static Color alpha(Color c, double a) {
    c.a = a;
    return c;
  }

  void draw_shadow(cairo_t* cr, double x, double y, double w, double h, double r) {
    for (int i = 10; i >= 1; --i) {  // layered translucent rounded rects: a cheap soft shadow
      rounded_rect(cr, x - i, y - i + 5, w + 2 * i, h + 2 * i, r + i);
      set_source(cr, {0, 0, 0, 0.022});
      cairo_fill(cr);
    }
  }

  void keycap(cairo_t* cr, const std::string& label, double x, double y, double* w_out) {
    const TextExtents te = measure_text(cr, label, 11.5, true);
    const double w = te.width + 12, h = 18;
    rounded_rect(cr, x, y, w, h, 5);
    set_source(cr, alpha(pal.fg_secondary, 0.18));
    cairo_fill(cr);
    draw_text(cr, label, x + 6, y + (h - te.height) / 2 + te.ascent, 11.5, alpha(pal.fg_primary, 0.9), true);
    *w_out = w;
  }

  void draw(cairo_t* cr, int W, int H) {
    if (start_menu) {
      place_card(W, H);
      draw_start_menu(cr, W, H);
      return;
    }
    const double r = pal.rounded ? 16 : 4;
    place_card(W, H);
    const double cx0 = card_x, cy0 = card_y;
    draw_shadow(cr, cx0, cy0, card_w, card_h, r);
    rounded_rect(cr, cx0, cy0, card_w, card_h, r);
    set_source(cr, pal.bg_primary);
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.22));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // Search field.
    const double ex = cx0 + kInner, ey = cy0 + kInner, ew = card_w - 2 * kInner;
    rounded_rect(cr, ex, ey, ew, kEntryH, pal.rounded ? 12 : 3);
    set_source(cr, pal.bg_secondary);
    cairo_fill(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.9));
    cairo_set_line_width(cr, 1.7);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_arc(cr, ex + 24, ey + kEntryH / 2 - 1.5, 6, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_move_to(cr, ex + 28.4, ey + kEntryH / 2 + 3);
    cairo_line_to(cr, ex + 33.5, ey + kEntryH / 2 + 8);
    cairo_stroke(cr);
    const TextExtents te = measure_text(cr, "Ag", kFont + 1.5);
    const double base = ey + (kEntryH - te.height) / 2 + te.ascent;
    const double tx = ex + 46;
    if (query.empty()) {
      draw_text(cr, "Search applications or type a command", tx, base, kFont + 1.5, alpha(pal.fg_secondary, 0.7));
    } else {
      draw_text(cr, query, tx, base, kFont + 1.5, pal.fg_primary);
    }
    const double caret_x = tx + measure_text(cr, query.substr(0, cursor), kFont + 1.5).width;
    set_source(cr, pal.accent);
    cairo_rectangle(cr, caret_x + 0.5, ey + 12, 1.6, kEntryH - 24);
    cairo_fill(cr);

    // Result rows (clipped to the list area).
    const double ly = ey + kEntryH + 8, lx = cx0 + kInner, lw = card_w - 2 * kInner;
    cairo_save(cr);
    cairo_rectangle(cr, lx, ly, lw, max_rows * row_h);
    cairo_clip(cr);
    for (int i = scroll; i < static_cast<int>(results.size()) && i < scroll + max_rows; ++i) {
      const double ry = ly + (i - scroll) * row_h;
      const bool sel = i == selected, hot = i == hover_row;
      if (sel || hot) {
        rounded_rect(cr, lx, ry + 1, lw - 8, row_h - 2, pal.rounded ? 11 : 3);
        set_source(cr, alpha(pal.accent, sel ? 0.24 : 0.10));
        cairo_fill(cr);
      }
      Entry* e = const_cast<Entry*>(results[static_cast<size_t>(i)]);
      const std::string primary = e ? e->de.name : query;
      const std::string secondary = e ? (e->de.comment.empty() ? e->hint : e->de.comment) : "Run as a shell command";
      // icon
      const double isz = 34, ix = lx + 12, iy = ry + (row_h - isz) / 2;
      cairo_surface_t* icon = e ? icon_for(*e) : nullptr;
      if (icon) {
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        cairo_scale(cr, isz / cairo_image_surface_get_width(icon), isz / cairo_image_surface_get_height(icon));
        cairo_set_source_surface(cr, icon, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
      } else {  // no icon: the first letter on a soft disc, or a prompt glyph for commands
        cairo_arc(cr, ix + isz / 2, iy + isz / 2, isz / 2, 0, 2 * M_PI);
        set_source(cr, alpha(pal.accent, 0.20));
        cairo_fill(cr);
        const std::string letter = e ? (primary.empty() ? "?" : primary.substr(0, next_char_len(primary, 0))) : ">";
        const TextExtents le = measure_text(cr, letter, 15, true);
        draw_text(cr, letter, ix + (isz - le.width) / 2, iy + (isz - le.height) / 2 + le.ascent, 15, pal.accent, true);
      }
      const double nx = ix + isz + 14;
      const TextExtents pe = measure_text(cr, primary, kFont, true);
      draw_text(cr, primary, nx, ry + 9 + pe.ascent, kFont, pal.fg_primary, true);
      std::string sec = secondary;
      while (sec.size() > 4 && measure_text(cr, sec, 12).width > lw - (nx - lx) - 70) sec.resize(sec.size() - 1);
      if (sec != secondary) sec += "...";
      draw_text(cr, sec, nx, ry + 9 + pe.height + 3 + pe.ascent * 0.9, 12, alpha(pal.fg_secondary, 0.85));
      if (sel) {
        double kw = 0;
        keycap(cr, "Enter", lx + lw - 8 - 56, ry + (row_h - 18) / 2, &kw);
      }
    }
    cairo_restore(cr);

    // Scrollbar indicator when the list overflows.
    const int total = static_cast<int>(results.size());
    if (total > max_rows) {
      const double track = max_rows * row_h;
      const double th = std::max(24.0, track * max_rows / total);
      const double ty = ly + (track - th) * scroll / (total - max_rows);
      rounded_rect(cr, lx + lw - 5, ty, 3, th, 1.5);
      set_source(cr, alpha(pal.fg_secondary, 0.5));
      cairo_fill(cr);
    }

    if (start_menu) {
      draw_footer_buttons(cr, lx, ly + max_rows * row_h, lw);
      return;
    }
    // Footer hints.
    const double fy = ly + max_rows * row_h + (footer_h - 18) / 2;
    double fx = lx + 6;
    const struct { const char* key; const char* what; } hints[] = {{"Up/Down", "navigate"}, {"Enter", "open"}, {"Esc", "close"}};
    for (const auto& h : hints) {
      double kw = 0;
      keycap(cr, h.key, fx, fy, &kw);
      const TextExtents he = measure_text(cr, h.what, 12);
      draw_text(cr, h.what, fx + kw + 7, fy + (18 - he.height) / 2 + he.ascent, 12, alpha(pal.fg_secondary, 0.85));
      fx += kw + 7 + he.width + 20;
    }
  }

  // ------------------------------------------------- Windows 7 style start menu --
  // Geometry shared by drawing and clicking. The card has a glass frame; inside it a light
  // program list with the search box under it on the left, and the user, places and Shut down
  // on the right.
  static constexpr double kFrame = 7, kLeftW = 252, kRightW = 160;
  static constexpr double kListTop = 6, kAllRowH = 34, kSearchH = 44;

  static Color mix(Color a, Color b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
  }

  // A highlighted row or button in the theme's accent.
  void accent_item(cairo_t* cr, double x, double y, double w, double h, bool strong) {
    const double rad = pal.rounded ? 8 : 2;
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, rad);
    set_source(cr, alpha(pal.accent, strong ? 0.26 : 0.12));
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.accent, strong ? 0.75 : 0.35));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }

  void draw_start_menu(cairo_t* cr, int out_w, int out_h) {
    hits.clear();
    const double x0 = card_x, y0 = card_y;
    const double frame_r = pal.rounded ? 14 : 3, panel_r = pal.rounded ? 10 : 2;
    draw_shadow(cr, x0, y0, card_w, card_h, frame_r);

    // The frame. Matte: one flat theme colour with a thin rim. Glass: the frosted wallpaper behind it,
    // a tint, a sheen and a light rim. Everything on top of it is the same in both looks, so this block
    // is the only part that differs.
    if (glass) {
      GlassStyle st;
      st.tint = mix(pal.bg_primary, pal.accent, 0.18);
      st.tint_alpha = 0.60;
      st.radius = frame_r;
      paint_glass(cr, backdrop, this->out_w > 0 ? this->out_w : out_w, this->out_h > 0 ? this->out_h : out_h,
                  origin_x + x0, origin_y + y0, x0, y0, card_w, card_h, st);
    } else {
      rounded_rect(cr, x0, y0, card_w, card_h, frame_r);
      set_source(cr, mix(pal.bg_primary, pal.accent, 0.12));
      cairo_fill_preserve(cr);
      set_source(cr, alpha(pal.fg_secondary, 0.25));
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
    }

    // ---- left panel: programs and search ----
    const double px = x0 + kFrame, py = y0 + kFrame, pw = kLeftW, ph = card_h - 2 * kFrame;
    rounded_rect(cr, px, py, pw, ph, panel_r);
    Color panel = pal.bg_secondary;
    panel.a = glass ? 0.72 : 1.0;
    set_source(cr, panel);
    cairo_fill(cr);
    const Color ink = pal.fg_primary, ink2 = pal.fg_secondary;

    const double lx = px + 4, ly = py + kListTop, lw = pw - 8;
    cairo_save(cr);
    cairo_rectangle(cr, lx, ly, lw, max_rows * row_h);
    cairo_clip(cr);
    for (int i = scroll; i < static_cast<int>(results.size()) && i < scroll + max_rows; ++i) {
      const double ry = ly + (i - scroll) * row_h;
      const bool sel = i == selected, hot = i == hover_row;
      if (sel || hot) accent_item(cr, lx, ry + 1, lw - 6, row_h - 2, sel);
      Entry* e = const_cast<Entry*>(results[static_cast<size_t>(i)]);
      const std::string primary = e ? e->de.name : query;
      const double isz = 30, ix = lx + 8, iy = ry + (row_h - isz) / 2;
      cairo_surface_t* icon = e ? icon_for(*e) : nullptr;
      if (icon) {
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        cairo_scale(cr, isz / cairo_image_surface_get_width(icon), isz / cairo_image_surface_get_height(icon));
        cairo_set_source_surface(cr, icon, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
      } else {
        cairo_arc(cr, ix + isz / 2, iy + isz / 2, isz / 2, 0, 2 * M_PI);
        set_source(cr, alpha(pal.accent, 0.20));
        cairo_fill(cr);
        const std::string letter = e ? (primary.empty() ? "?" : primary.substr(0, next_char_len(primary, 0))) : ">";
        const TextExtents le = measure_text(cr, letter, 14, true);
        draw_text(cr, letter, ix + (isz - le.width) / 2, iy + (isz - le.height) / 2 + le.ascent, 14, pal.accent, true);
      }
      const double nx = ix + isz + 12;
      const std::string full = e ? primary : "Run: " + primary;
      std::string label = full;
      while (label.size() > 4 && measure_text(cr, label, kFont, true).width > lw - (nx - lx) - 14) label.resize(label.size() - 1);
      if (label.size() < full.size()) label += "...";
      const TextExtents te = measure_text(cr, label, kFont, true);
      draw_text(cr, label, nx, ry + (row_h - te.height) / 2 + te.ascent, kFont, ink, true);
    }
    cairo_restore(cr);
    if (results.empty() && !query.empty()) draw_text(cr, "No matches", lx + 14, ly + 26, kFont, ink2);
    const int total = static_cast<int>(results.size());
    if (total > max_rows) {  // thin scroll thumb
      const double track = max_rows * row_h, th = std::max(24.0, track * max_rows / total);
      const double ty = ly + (track - th) * scroll / (total - max_rows);
      rounded_rect(cr, lx + lw - 4, ty, 3, th, 1.5);
      set_source(cr, alpha(pal.fg_secondary, 0.5));
      cairo_fill(cr);
    }

    // "All Programs" / "Back" under the list, with a separator above it.
    const double ay = ly + max_rows * row_h + 2;
    set_source(cr, alpha(pal.fg_secondary, 0.25));
    cairo_rectangle(cr, lx + 8, ay - 1, lw - 16, 1);
    cairo_fill(cr);
    if (query.empty()) {
      if (hover_hit == kHitAllPrograms) accent_item(cr, lx, ay + 2, lw - 6, kAllRowH - 4, false);
      hits.push_back({lx, ay, lw, kAllRowH, kHitAllPrograms});
      const std::string label = all_programs ? "Back" : "All Programs";
      const double ax = lx + 14, acy = ay + kAllRowH / 2;
      set_source(cr, ink);
      if (all_programs) {
        cairo_move_to(cr, ax + 7, acy - 5);
        cairo_line_to(cr, ax + 1, acy);
        cairo_line_to(cr, ax + 7, acy + 5);
      } else {
        cairo_move_to(cr, ax + 1, acy - 5);
        cairo_line_to(cr, ax + 7, acy);
        cairo_line_to(cr, ax + 1, acy + 5);
      }
      cairo_close_path(cr);
      cairo_fill(cr);
      const TextExtents te = measure_text(cr, label, kFont, true);
      draw_text(cr, label, ax + 18, acy - te.height / 2 + te.ascent, kFont, ink, true);
    }

    // The search box.
    const double sy = ay + kAllRowH + 4, sx = lx + 4, sw = lw - 14, sh = kSearchH - 16;
    rounded_rect(cr, sx + 0.5, sy + 0.5, sw - 1, sh - 1, pal.rounded ? sh / 2 : 3);
    set_source(cr, pal.bg_primary);
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.35));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    hits.push_back({sx, sy, sw, sh, kHitSearch});
    {
      const TextExtents te = measure_text(cr, "Ag", kFont);
      const double base = sy + (sh - te.height) / 2 + te.ascent;
      if (query.empty()) {
        draw_text(cr, "Search programs and files", sx + 14, base, kFont, alpha(pal.fg_secondary, 0.75));
      } else {
        std::string shown = query;
        while (shown.size() > 1 && measure_text(cr, shown, kFont).width > sw - 52) shown.erase(0, next_char_len(shown, 0));
        draw_text(cr, shown, sx + 14, base, kFont, ink);
      }
      const bool fits = measure_text(cr, query, kFont).width <= sw - 52;
      const double caret = sx + 14 + (query.empty() || !fits ? 0 : measure_text(cr, query.substr(0, cursor), kFont).width);
      if (query.empty() || fits) {
        set_source(cr, pal.accent);
        cairo_rectangle(cr, caret + 0.5, sy + 6, 1.5, sh - 12);
        cairo_fill(cr);
      }
      set_source(cr, alpha(pal.fg_secondary, 0.9));  // magnifier
      cairo_set_line_width(cr, 1.7);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_arc(cr, sx + sw - 22, sy + sh / 2 - 1, 5, 0, 2 * M_PI);
      cairo_stroke(cr);
      cairo_move_to(cr, sx + sw - 18.5, sy + sh / 2 + 2.5);
      cairo_line_to(cr, sx + sw - 14, sy + sh / 2 + 7);
      cairo_stroke(cr);
    }

    // ---- right column: user, places, Shut down ----
    const double rx = px + pw + 8, rw = kRightW - 14;
    double ry = py + 8;
    {
      const double ts = 40;
      rounded_rect(cr, rx + 2, ry, ts, ts, pal.rounded ? ts / 2 : 4);
      set_source(cr, pal.accent);
      cairo_fill(cr);
      const std::string letter = user_name.empty() ? "?" : user_name.substr(0, next_char_len(user_name, 0));
      const TextExtents le = measure_text(cr, letter, 20, true);
      draw_text(cr, letter, rx + 2 + (ts - le.width) / 2, ry + (ts - le.height) / 2 + le.ascent, 20, pal.bg_primary, true);
      std::string name = user_name;
      while (name.size() > 3 && measure_text(cr, name, kFont, true).width > rw - ts - 16) name.resize(name.size() - 1);
      draw_text(cr, name, rx + ts + 12, ry + ts / 2 + 5, kFont, pal.fg_primary, true);
      ry += ts + 14;
    }
    for (size_t i = 0; i < places.size(); ++i) {
      const double ih = 30;
      if (hover_hit == static_cast<int>(i)) accent_item(cr, rx, ry, rw, ih, false);
      hits.push_back({rx, ry, rw, ih, static_cast<int>(i)});
      const TextExtents te = measure_text(cr, places[i].label, kFont);
      draw_text(cr, places[i].label, rx + 12, ry + (ih - te.height) / 2 + te.ascent, kFont, pal.fg_primary);
      ry += ih + 2;
    }
    const double bh = 32, by = py + ph - bh - 4, lock_w = 34, sd_w = rw - lock_w - 6;
    auto button = [&](double bx, double bw, bool hot) {
      rounded_rect(cr, bx + 0.5, by + 0.5, bw - 1, bh - 1, pal.rounded ? 9 : 3);
      set_source(cr, hot ? alpha(pal.accent, 0.30) : pal.bg_secondary);
      cairo_fill_preserve(cr);
      set_source(cr, alpha(hot ? pal.accent : pal.fg_secondary, hot ? 0.8 : 0.35));
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
    };
    {
      const bool hot = hover_hit == kHitShutdown;
      button(rx, sd_w, hot);
      const TextExtents te = measure_text(cr, "Shut down", 13, true);
      draw_text(cr, "Shut down", rx + (sd_w - te.width) / 2, by + (bh - te.height) / 2 + te.ascent, 13,
                hot ? pal.fg_primary : pal.fg_secondary, true);
      hits.push_back({rx, by, sd_w, bh, kHitShutdown});
    }
    {
      const bool hot = hover_hit == kHitLock;
      const double lx2 = rx + sd_w + 6;
      button(lx2, lock_w, hot);
      const double cx = lx2 + lock_w / 2, cy = by + bh / 2 + 2;
      set_source(cr, hot ? pal.fg_primary : pal.fg_secondary);
      cairo_set_line_width(cr, 1.8);
      cairo_arc(cr, cx, cy - 3, 4.2, M_PI, 2 * M_PI);
      cairo_stroke(cr);
      cairo_rectangle(cr, cx - 6, cy - 3, 12, 9);
      cairo_fill(cr);
      hits.push_back({lx2, by, lock_w, bh, kHitLock});
    }
  }

  // -------------------------------------------------------------- launch --
  void launch_selected() {
    if (selected < 0 || static_cast<size_t>(selected) >= results.size()) return;
    const Entry* e = results[static_cast<size_t>(selected)];
    if (e) {
      std::vector<std::string> argv = exec_argv(e->de);
      if (argv.empty()) return;
      if (e->de.terminal) {
        std::vector<std::string> t = terminal_argv();
        t.push_back("-e");
        t.insert(t.end(), argv.begin(), argv.end());
        argv = std::move(t);
      }
      if (!spawn_detached(argv, e->de.path)) std::fprintf(stderr, "fleetwm-launcher: failed to launch %s\n", e->de.name.c_str());
    } else {
      launch_command(query);
    }
    app.quit();
  }

  static std::vector<std::string> terminal_argv() {
    std::vector<std::string> out;
    std::string cmd = load_default_apps_config().terminal_command;
    size_t pos = 0;
    while (pos < cmd.size()) {
      const size_t s = cmd.find_first_not_of(' ', pos);
      if (s == std::string::npos) break;
      size_t e = cmd.find(' ', s);
      if (e == std::string::npos) e = cmd.size();
      out.push_back(cmd.substr(s, e - s));
      pos = e;
    }
    if (out.empty()) out.push_back("foot");
    return out;
  }

  bool binary_wants_terminal(const std::string& binary) {
    if (binary.empty()) return false;
    for (const auto& e : entries)
      if (exec_basename(e.de) == binary) return e.de.terminal;
    return false;
  }

  void launch_command(const std::string& text) {
    const size_t s = text.find_first_not_of(" \t");
    std::string first;
    if (s != std::string::npos) {
      const size_t e = text.find_first_of(" \t", s);
      first = text.substr(s, e == std::string::npos ? std::string::npos : e - s);
    }
    std::vector<std::string> argv;
    if (binary_wants_terminal(first)) {
      argv = terminal_argv();
      argv.insert(argv.end(), {"-e", "/bin/sh", "-c", text});
    } else {
      argv = {"/bin/sh", "-c", text};
    }
    spawn_detached(argv);
  }

  // --------------------------------------------------------------- input --
  void changed() {
    refresh();
    surface->queue_draw();
  }

  void insert(const std::string& s) {
    query.insert(cursor, s);
    cursor += s.size();
    changed();
  }

  void on_key(const KeyEvent& ev) {
    if (!ev.pressed) return;
    const bool ctrl = ev.mods & kCtrl;
    switch (ev.sym) {
      case XKB_KEY_Escape: app.quit(); return;
      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter: launch_selected(); return;
      case XKB_KEY_Down: move(1); return;
      case XKB_KEY_Up: move(-1); return;
      case XKB_KEY_Page_Down: move(max_rows); return;
      case XKB_KEY_Page_Up: move(-max_rows); return;
      case XKB_KEY_Left:
        cursor -= prev_char_len(query, cursor);
        surface->queue_draw();
        return;
      case XKB_KEY_Right:
        cursor += next_char_len(query, cursor);
        surface->queue_draw();
        return;
      case XKB_KEY_Home: cursor = 0; surface->queue_draw(); return;
      case XKB_KEY_End: cursor = query.size(); surface->queue_draw(); return;
      case XKB_KEY_BackSpace: {
        const size_t n = prev_char_len(query, cursor);
        if (n) {
          query.erase(cursor - n, n);
          cursor -= n;
          changed();
        }
        return;
      }
      case XKB_KEY_Insert:
        if (ev.mods & kShift) paste();
        return;
      case XKB_KEY_Delete: {
        const size_t n = next_char_len(query, cursor);
        if (n) {
          query.erase(cursor, n);
          changed();
        }
        return;
      }
      default: break;
    }
    if (ctrl) {
      switch (ev.sym) {
        case XKB_KEY_n: move(1); return;
        case XKB_KEY_p: move(-1); return;
        case XKB_KEY_a: cursor = 0; surface->queue_draw(); return;
        case XKB_KEY_e: cursor = query.size(); surface->queue_draw(); return;
        case XKB_KEY_v: paste(); return;
        case XKB_KEY_u:
          query.erase(0, cursor);
          cursor = 0;
          changed();
          return;
        case XKB_KEY_w: {
          size_t i = cursor;
          while (i > 0 && query[i - 1] == ' ') --i;
          while (i > 0 && query[i - 1] != ' ') --i;
          query.erase(i, cursor - i);
          cursor = i;
          changed();
          return;
        }
        default: return;
      }
    }
    if (ev.mods & (kAlt | kSuper)) return;
    if (ev.utf8.empty() || static_cast<unsigned char>(ev.utf8[0]) < 0x20 || ev.utf8[0] == 0x7f) return;
    insert(ev.utf8);
  }

  void paste() {
    app.paste_text([this](const std::string& text) {
      std::string line = text.substr(0, text.find_first_of("\r\n"));
      line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }),
                 line.end());
      if (!line.empty()) insert(line);
    });
  }

  void move(int delta) {
    if (results.empty()) return;
    selected = std::max(0, std::min(static_cast<int>(results.size()) - 1, selected + delta));
    ensure_visible();
    surface->queue_draw();
  }

  int row_at(double x, double y) const {
    if (start_menu) {
      const double lx = card_x + kFrame + 3, ly = card_y + kFrame + kListTop, lw = kLeftW - 6;
      if (x < lx || x >= lx + lw || y < ly || y >= ly + max_rows * row_h) return -1;
      const int i = scroll + static_cast<int>((y - ly) / row_h);
      return i < static_cast<int>(results.size()) ? i : -1;
    }
    const double ly = card_y + kInner + kEntryH + 8, lx = card_x + kInner;
    if (x < lx || x >= lx + card_w - 2 * kInner || y < ly || y >= ly + max_rows * row_h) return -1;
    const int i = scroll + static_cast<int>((y - ly) / row_h);
    return i < static_cast<int>(results.size()) ? i : -1;
  }

  void on_motion(double x, double y) {
    if (start_menu) {
      int h = -9999;
      for (const Hit& hit : hits)
        if (hit.contains(x, y)) h = hit.id;
      if (h != hover_hit) {
        hover_hit = h;
        surface->queue_draw();
      }
    }
    const int r = row_at(x, y);
    if (r != hover_row) {
      hover_row = r;
      surface->queue_draw();
    }
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (!pressed || b != kBtnLeft) return;
    if (start_menu) {
      // The compositor closes the menu on a click anywhere outside the surface; this is the margin
      // around the card inside it.
      if (x < card_x || x >= card_x + card_w || y < card_y || y >= card_y + card_h) {
        app.quit();
        return;
      }
      for (const Hit& hit : hits) {
        if (!hit.contains(x, y)) continue;
        if (hit.id == kHitAllPrograms) {
          all_programs = !all_programs;
          refresh();
          surface->queue_draw();
        } else if (hit.id == kHitShutdown) {
          spawn_detached({"fleetwm-powermenu"});
          app.quit();
        } else if (hit.id == kHitLock) {
          if (ipc.connect()) ipc.send_command("LOCK");
          app.quit();
        } else if (hit.id >= 0 && static_cast<size_t>(hit.id) < places.size()) {
          spawn_detached(places[static_cast<size_t>(hit.id)].argv);
          app.quit();
        }
        return;
      }
    }
    const int r = row_at(x, y);
    if (r >= 0) {
      selected = r;
      launch_selected();
    }
  }

  void on_scroll(double, double dy) {
    wheel_accum += dy;
    const int steps = static_cast<int>(wheel_accum / 10.0);
    if (steps != 0) {
      wheel_accum -= steps * 10.0;
      const int max_scroll = std::max(0, static_cast<int>(results.size()) - max_rows);
      scroll = std::max(0, std::min(max_scroll, scroll + steps));
      surface->queue_draw();
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-launcher", "[--start-menu]")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  fleetwm::prewarm::start("fleetwm-launcher");
  signal(SIGCHLD, SIG_IGN);

  for (int i = 1; i + 1 < argc; ++i)
    if (std::string(argv[i]) == "--default") return run_default_app(argv[i + 1]);

  bool want_start_menu = false, edge_given = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--start-menu") want_start_menu = true;
    else if (std::string(argv[i]) == "--edge") edge_given = true;
  }
  if (want_start_menu && toggle_existing_start_menu()) return 0;

  Launcher L;
  const ThemeConfig theme_cfg = load_theme_config();
  L.pal = load_palette(theme_cfg);
  L.glass = theme_cfg.glass;
  if (!L.app.connect()) return 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--start-menu") L.start_menu = true;
    else if (arg == "--edge" && i + 1 < argc) L.edge = argv[++i];
    else if (arg == "--inset" && i + 1 < argc) L.inset = std::max(0, std::atoi(argv[++i]));
  }
  if (L.start_menu && !edge_given) {
    // Sit beside the taskbar wherever bar.toml puts it.
    const TaskbarPosition pos = load_bar_config().taskbar_position;
    L.edge = taskbar_position_to_string(pos);
    L.inset = pos == TaskbarPosition::Left || pos == TaskbarPosition::Right ? kTaskbarWidth : kTaskbarThickness;
  }
  if (L.start_menu) {
    { std::ofstream(pid_path()) << getpid() << "\n"; }
    // The Windows 7 style layout: glass frame, program list + search on the left, places on the right.
    L.max_rows = 9;
    L.row_h = 42;
    L.card_w = static_cast<int>(2 * Launcher::kFrame + Launcher::kLeftW + Launcher::kRightW);
    L.card_h = static_cast<int>(2 * Launcher::kFrame + 474);
  }
  L.load();
  if (L.start_menu && L.glass) L.backdrop = load_backdrop();
  L.refresh();

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = 0;  // unanchored: the compositor centers it
  cfg.width = kWindowWidth;
  cfg.height = kWindowHeight;
  if (L.start_menu) {
    // Just the card and its shadow, next to the taskbar. A full-output surface cost two output-sized
    // buffers here and a texture in the compositor; the compositor now closes the menu on a click
    // outside it (namespace "fleetwm-start-menu"), which is what the big surface used to catch.
    constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, B = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                       Lf = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const int sw = L.card_w + 2 * Launcher::kStartPad, sh = L.card_h + 2 * Launcher::kStartPad;
    const int off = std::max(0, L.inset - 6);  // leaves the card the same 10 px from the taskbar as before
    if (!L.app.outputs().empty()) {
      const auto& o = L.app.outputs()[0];
      const int sc = std::max(1, o.scale);
      L.out_w = o.width / sc;
      L.out_h = o.height / sc;
    }
    cfg.width = sw;
    cfg.height = sh;
    if (L.edge == "top") {
      cfg.anchor = T | Lf;
      cfg.margin_top = off;
      L.origin_y = off;
    } else if (L.edge == "left") {
      cfg.anchor = Lf | T;
      cfg.margin_left = off;
      L.origin_x = off;
    } else if (L.edge == "right") {
      cfg.anchor = R | T;
      cfg.margin_right = off;
      L.origin_x = L.out_w - off - sw;
    } else {  // bottom
      cfg.anchor = B | Lf;
      cfg.margin_bottom = off;
      L.origin_y = L.out_h - off - sh;
    }
    cfg.name = fleetwm::kStartMenuNamespace;
  }
  cfg.exclusive_zone = -1;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
  if (!L.start_menu) cfg.name = "fleetwm-launcher";
  L.surface = std::make_unique<Surface>(L.app, cfg);
  L.surface->on_draw = [&L](cairo_t* cr, int w, int h) { L.draw(cr, w, h); };
  L.surface->on_key = [&L](const KeyEvent& e) { L.on_key(e); };
  L.surface->on_button = [&L](double x, double y, uint32_t b, bool p) { L.on_button(x, y, b, p); };
  L.surface->on_motion = [&L](double x, double y) { L.on_motion(x, y); };
  L.surface->on_scroll = [&L](double dx, double dy) { L.on_scroll(dx, dy); };
  L.surface->on_closed = [&L] { L.app.quit(); };
  // Focus moved to another window or monitor: a popup menu goes away.
  if (L.start_menu) L.surface->on_keyboard_leave = [&L] { L.app.quit(); };

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  L.app.watch_fd(sfd, [&L] { L.app.quit(); });

  L.app.run();
  if (L.start_menu) unlink(pid_path().c_str());
  return 0;
}
