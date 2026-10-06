#pragma once

// Settings -> Keyboard: the layouts you switch between (with the one in use), how to switch,
// the key held for the Tiling shortcuts, key repeat, and the languages you picked. Adding or
// removing languages and layouts happens in fleetwm-langpicker (a checklist window).

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "ipc_client.hpp"
#include "keybinds_config.hpp"
#include "keyboard_config.hpp"
#include "ui.hpp"
#include "xkb_rules.hpp"

namespace fleetwm {

class KeyboardTab {
 public:
  KeyboardTab(kit::App& app, std::function<void()> redraw) : app_(app), redraw_(std::move(redraw)) {
    reload();
    connect();
  }
  ~KeyboardTab() {
    if (watch_) app_.unwatch(watch_);
  }

  void reload() {
    config_ = load_keyboard_config();
    binds_ = load_keybinds_config();
    layouts_ = load_xkb_layouts();
  }

  void draw(kit::Ui& ui, cairo_t* cr) {
    const kit::Palette& p = ui.palette();
    ++frames_;
    if (!timer_) {  // the build status file changes while locales are generated
      timer_ = app_.add_timer(1500, [this] {
        if (frames_ == last_frames_) {
          app_.unwatch(timer_);
          timer_ = 0;
          return;
        }
        last_frames_ = frames_;
        redraw_();
      });
      last_frames_ = frames_;
    }

    // ---- layouts ----
    ui.section("Keyboard layouts");
    const int n = static_cast<int>(config_.layouts.size());
    if (current_ >= n) current_ = 0;
    ui.label("Click a layout to switch to it now. Use the shortcut below to switch while you work.", true);
    ui.newline();
    ui.space(2);
    int move_from = -1, move_to = -1, remove = -1;
    for (int i = 0; i < n; ++i) {
      const KeyboardLayout& l = config_.layouts[static_cast<size_t>(i)];
      kit::UiRect r;
      const kit::Ui::CanvasEvent ev = ui.canvas(40, &r);
      const bool is_current = i == current_;
      // Buttons at the right: up, down, remove.
      const double bx = r.w - 3 * 34 - 8;
      auto in_btn = [&](int k) { return ev.x >= bx + k * 34 && ev.x < bx + k * 34 + 30 && ev.y >= 7 && ev.y < 33; };
      if (ev.pressed) {
        if (in_btn(0) && i > 0) { move_from = i; move_to = i - 1; }
        else if (in_btn(1) && i + 1 < n) { move_from = i; move_to = i + 1; }
        else if (in_btn(2) && n > 1) remove = i;
        else if (ev.x < bx) ipc_.send_command("LAYOUT_SET " + std::to_string(i));
      }
      const bool hover = ev.x >= 0 && ev.x < r.w && ev.y >= 0 && ev.y < r.h;
      kit::rounded_rect(cr, r.x, r.y, r.w, r.h, p.rounded ? 8 : 0);
      kit::Color bg = is_current ? p.accent : p.bg_secondary;
      bg.a = is_current ? 0.18 : (hover ? 0.9 : 0.55);
      kit::set_source(cr, bg);
      cairo_fill(cr);
      // The pill with the code, as the bar shows it.
      const std::string code = layout_pill_text(l);
      const double pw = std::max(34.0, kit::measure_text(cr, code, 12, true).width + 14);
      kit::rounded_rect(cr, r.x + 10, r.y + 9, pw, 22, 11);
      kit::set_source(cr, is_current ? p.accent : kit::Color{p.fg_secondary.r, p.fg_secondary.g, p.fg_secondary.b, 0.3});
      cairo_fill(cr);
      kit::draw_text(cr, code, r.x + 10 + (pw - kit::measure_text(cr, code, 12, true).width) / 2, r.y + 25, 12,
                     is_current ? p.bg_primary : p.fg_primary, true);
      kit::draw_text(cr, describe_layout(layouts_, l.layout, l.variant), r.x + 10 + pw + 12, r.y + 25, 15, p.fg_primary, is_current);
      if (is_current) kit::draw_text(cr, "in use", r.x + bx - 56, r.y + 25, 12, p.accent);
      const char* glyphs[3] = {"^", "v", "x"};
      for (int k = 0; k < 3; ++k) {
        const bool enabled = k == 0 ? i > 0 : k == 1 ? i + 1 < n : n > 1;
        const bool over = enabled && in_btn(k) && hover;
        kit::rounded_rect(cr, r.x + bx + k * 34, r.y + 7, 30, 26, 6);
        kit::Color edge = over ? p.accent : p.fg_secondary;
        edge.a = enabled ? (over ? 1.0 : 0.45) : 0.15;
        kit::set_source(cr, edge);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        const double gw = kit::measure_text(cr, glyphs[k], 13, true).width;
        kit::Color gc = over ? p.accent : p.fg_secondary;
        gc.a = enabled ? 1.0 : 0.3;
        kit::draw_text(cr, glyphs[k], r.x + bx + k * 34 + (30 - gw) / 2, r.y + 25, 13, gc, true);
      }
    }
    if (move_from >= 0) {
      std::swap(config_.layouts[static_cast<size_t>(move_from)], config_.layouts[static_cast<size_t>(move_to)]);
      save();
    } else if (remove >= 0) {
      config_.layouts.erase(config_.layouts.begin() + remove);
      save();
    }
    if (ui.button("Add or remove languages and layouts...", true, true)) kit::spawn_detached({"fleetwm-langpicker"});
    ui.newline();
    ui.label("Opens a checklist of all layouts and languages. Closing it builds the locales and the font cache.", true);
    ui.newline();
    ui.space(4);
    ui.row("Try it here");
    ui.text_entry(&test_text_, 320);
    ui.newline();

    // ---- switching ----
    ui.space(6);
    ui.section("Switching layouts");
    ui.row("Shortcut");
    int sw = config_.switch_keys == LayoutSwitchKeys::SuperSpace ? 0 : config_.switch_keys == LayoutSwitchKeys::AltShift ? 1 : 2;
    if (ui.segmented({"Win+Space", "Alt+Shift", "Both"}, &sw)) {
      config_.switch_keys = sw == 0 ? LayoutSwitchKeys::SuperSpace : sw == 1 ? LayoutSwitchKeys::AltShift : LayoutSwitchKeys::Both;
      save();
    }
    ui.newline();
    ui.label("Win+Space goes forward and Win+Shift+Space backward. The bar shows the layout in use.", true);
    ui.newline();

    // ---- modifier ----
    ui.space(6);
    ui.section("Modifier key");
    ui.row("Tiling shortcuts use");
    int mod = binds_.tiling_modifier == "super" ? 1 : 0;
    if (ui.segmented({"Alt", "Super (Windows key)"}, &mod)) {
      binds_.tiling_modifier = mod == 1 ? "super" : "alt";
      try {
        save_keybinds_config(binds_);
      } catch (...) {
      }
    }
    ui.newline();
    ui.label("The key held for Return, D, Q and the other Tiling layout shortcuts. Every other shortcut can be", true);
    ui.newline();
    ui.label("changed in the Keyboard Shortcuts window (Super+/) or in keybinds.toml.", true);
    ui.newline();

    // ---- typing ----
    ui.space(6);
    ui.section("Typing");
    ui.row("Repeat speed");
    if (ui.slider(&config_.repeat_rate, 1, 60, 220)) save();
    ui.newline();
    ui.row("Delay before repeating");
    if (ui.slider(&config_.repeat_delay, 150, 1000, 220)) save();
    ui.newline();

    // ---- languages ----
    ui.space(6);
    ui.section("Languages");
    if (config_.locales.empty()) {
      ui.label("No extra languages picked yet.", true);
    } else {
      std::string list;
      for (const std::string& l : config_.locales) list += (list.empty() ? "" : ", ") + l;
      ui.paragraph(list);
    }
    ui.newline();
    const std::string st = build_status();
    if (!st.empty()) {
      ui.label("Locale and font cache build: " + st, true);
      ui.newline();
    }
  }

 private:
  void save() {
    try {
      save_keyboard_config(config_);
    } catch (const std::exception&) {
    }
    redraw_();
  }

  static std::string build_status() {
    std::ifstream f("/run/fleetwm-locale-build.status");
    std::string s;
    std::getline(f, s);
    return s;
  }

  void connect() {
    if (!ipc_.connect()) return;
    watch_ = app_.watch_fd(ipc_.fd(), [this] {
      ipc_.poll_lines([this](const std::string& line) {
        if (line.rfind("LAYOUTS ", 0) == 0) {
          std::istringstream in(line.substr(8));
          int idx = 0;
          if (in >> idx) current_ = idx;
          redraw_();
        }
      });
      if (!ipc_.is_connected()) {
        app_.unwatch(watch_);
        watch_ = 0;
      }
    });
    ipc_.send_command("LAYOUTS?");
  }

  kit::App& app_;
  std::function<void()> redraw_;
  KeyboardConfig config_;
  KeybindsConfig binds_;
  std::vector<LayoutInfo> layouts_;
  IpcClient ipc_;
  int watch_ = 0, timer_ = 0;
  unsigned frames_ = 0, last_frames_ = 0;
  int current_ = 0;
  std::string test_text_;
};

}  // namespace fleetwm
