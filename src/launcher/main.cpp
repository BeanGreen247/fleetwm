// fleetwm-launcher: GTK-free application launcher. A centered OVERLAY
// layer surface with exclusive keyboard focus: a search entry and a list of
// matching applications (name + category hint). Enter launches the selected
// entry; if nothing matches the typed text can be run as a shell command.
// Escape quits. Up/Down (or Ctrl+N/Ctrl+P) move the selection.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "default_apps.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "icon_theme.hpp"
#include "malloc_tuning.hpp"
#include "theme.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

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

struct Launcher {
  App app;
  Palette pal;
  std::unique_ptr<Surface> surface;
  std::vector<Entry> entries;  // sorted by name
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
  }

  void refresh() {
    results.clear();
    if (query.empty()) {
      for (const auto& e : entries) results.push_back(&e);
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
    if (selected >= scroll + kMaxVisibleRows) scroll = selected - kMaxVisibleRows + 1;
    scroll = std::max(0, std::min(scroll, std::max(0, static_cast<int>(results.size()) - kMaxVisibleRows)));
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
    (void)W;
    (void)H;
    const double r = pal.rounded ? pal.radius + 6 : 4;
    const double cx0 = kPad, cy0 = kPad;
    draw_shadow(cr, cx0, cy0, kCardW, kCardH, r);
    rounded_rect(cr, cx0, cy0, kCardW, kCardH, r);
    set_source(cr, pal.bg_primary);
    cairo_fill_preserve(cr);
    set_source(cr, alpha(pal.fg_secondary, 0.22));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // Search field.
    const double ex = cx0 + kInner, ey = cy0 + kInner, ew = kCardW - 2 * kInner;
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
    const double ly = ey + kEntryH + 8, lx = cx0 + kInner, lw = kCardW - 2 * kInner;
    cairo_save(cr);
    cairo_rectangle(cr, lx, ly, lw, kMaxVisibleRows * kRowHeight);
    cairo_clip(cr);
    for (int i = scroll; i < static_cast<int>(results.size()) && i < scroll + kMaxVisibleRows; ++i) {
      const double ry = ly + (i - scroll) * kRowHeight;
      const bool sel = i == selected, hot = i == hover_row;
      if (sel || hot) {
        rounded_rect(cr, lx, ry + 1, lw - 8, kRowHeight - 2, pal.rounded ? 11 : 3);
        set_source(cr, alpha(pal.accent, sel ? 0.24 : 0.10));
        cairo_fill(cr);
      }
      Entry* e = const_cast<Entry*>(results[static_cast<size_t>(i)]);
      const std::string primary = e ? e->de.name : query;
      const std::string secondary = e ? (e->de.comment.empty() ? e->hint : e->de.comment) : "Run as a shell command";
      // icon
      const double isz = 34, ix = lx + 12, iy = ry + (kRowHeight - isz) / 2;
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
        keycap(cr, "Enter", lx + lw - 8 - 56, ry + (kRowHeight - 18) / 2, &kw);
      }
    }
    cairo_restore(cr);

    // Scrollbar indicator when the list overflows.
    const int total = static_cast<int>(results.size());
    if (total > kMaxVisibleRows) {
      const double track = kMaxVisibleRows * kRowHeight;
      const double th = std::max(24.0, track * kMaxVisibleRows / total);
      const double ty = ly + (track - th) * scroll / (total - kMaxVisibleRows);
      rounded_rect(cr, lx + lw - 5, ty, 3, th, 1.5);
      set_source(cr, alpha(pal.fg_secondary, 0.5));
      cairo_fill(cr);
    }

    // Footer hints.
    const double fy = ly + kMaxVisibleRows * kRowHeight + (kFooterH - 18) / 2;
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
      case XKB_KEY_Page_Down: move(kMaxVisibleRows); return;
      case XKB_KEY_Page_Up: move(-kMaxVisibleRows); return;
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
    const double ly = kPad + kInner + kEntryH + 8, lx = kPad + kInner;
    if (x < lx || x >= lx + kCardW - 2 * kInner || y < ly || y >= ly + kMaxVisibleRows * kRowHeight) return -1;
    const int i = scroll + static_cast<int>((y - ly) / kRowHeight);
    return i < static_cast<int>(results.size()) ? i : -1;
  }

  void on_motion(double x, double y) {
    const int r = row_at(x, y);
    if (r != hover_row) {
      hover_row = r;
      surface->queue_draw();
    }
  }

  void on_button(double x, double y, uint32_t b, bool pressed) {
    if (!pressed || b != kBtnLeft) return;
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
      const int max_scroll = std::max(0, static_cast<int>(results.size()) - kMaxVisibleRows);
      scroll = std::max(0, std::min(max_scroll, scroll + steps));
      surface->queue_draw();
    }
  }
};

}  // namespace

int main() {
  fleetwm::tune_malloc_for_low_rss();
  signal(SIGCHLD, SIG_IGN);

  Launcher L;
  L.pal = load_palette(load_theme_config());
  if (!L.app.connect()) return 1;
  L.load();
  L.refresh();

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = 0;  // unanchored: the compositor centers it
  cfg.width = kWindowWidth;
  cfg.height = kWindowHeight;
  cfg.exclusive_zone = -1;
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
  cfg.name = "fleetwm-launcher";
  L.surface = std::make_unique<Surface>(L.app, cfg);
  L.surface->on_draw = [&L](cairo_t* cr, int w, int h) { L.draw(cr, w, h); };
  L.surface->on_key = [&L](const KeyEvent& e) { L.on_key(e); };
  L.surface->on_button = [&L](double x, double y, uint32_t b, bool p) { L.on_button(x, y, b, p); };
  L.surface->on_motion = [&L](double x, double y) { L.on_motion(x, y); };
  L.surface->on_scroll = [&L](double dx, double dy) { L.on_scroll(dx, dy); };
  L.surface->on_closed = [&L] { L.app.quit(); };

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
