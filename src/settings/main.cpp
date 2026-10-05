// fleetwm-settings: GTK-free settings window. A regular xdg_toplevel
// (app id dev.fleetwm.Settings, which the compositor floats, centres and keeps
// on top) drawn with cairo through fleetkit's immediate-mode Ui. Every change is
// saved immediately; the compositor, bar, wallpaper and clients pick the new
// config up live through their own inotify watches.

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio_mixer.hpp"
#include "bar_config.hpp"
#include "battery_reading.hpp"
#include "default_apps.hpp"
#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "mimeapps.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "wallpaper_config.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kWindowW = 620, kWindowH = 600;
constexpr double kFont = 14.67;

struct MimeRow {
  const char* label;
  const char* mime;
  std::vector<int> apps;  // indices into Settings::entries
  int selected = -1;
};

struct Settings {
  App app;
  Palette pal;
  Ui ui{Palette{}};
  std::unique_ptr<Surface> surface;

  ThemeConfig config;
  BarConfig bar;
  WallpaperConfig wallpaper;
  DefaultAppsConfig default_apps;

  int tab = 0;
  double scroll[8] = {};
  std::vector<std::string> tab_names{"Theme", "Bar", "Wallpaper", "Default Apps", "Audio", "Performance", "About"};

  // power
  bool has_battery = false;
  std::string battery_dir;
  BatteryReading battery;

  // default apps
  std::vector<DesktopEntry> entries;
  std::vector<MimeRow> mime_rows;
  std::vector<int> terminal_apps;
  int terminal_selected = -1;

  // audio
  common::AudioMixer mixer;
  bool master_available = false, master_muted = false;
  int master_value = 0;
  std::vector<common::AudioStream> streams;

  // ------------------------------------------------------------ helpers --
  void redraw() { surface->queue_draw(); }

  void apply_theme() {
    pal = load_palette(config);
    ui.set_palette(pal);
  }

  void save_theme() {
    save_theme_config(config);
    apply_theme();
  }
  void save_bar() { save_bar_config(bar); }
  void save_wallpaper() { save_wallpaper_config(wallpaper); }

  // Shortens a path in the middle so it fits `max_w` px.
  std::string ellipsize(cairo_t* cr, const std::string& s, double max_w) {
    if (measure_text(cr, s, kFont).width <= max_w) return s;
    std::string head = s.substr(0, s.size() / 2), tail = s.substr(s.size() / 2);
    while (!head.empty() && !tail.empty() &&
           measure_text(cr, head + "..." + tail, kFont).width > max_w) {
      if (head.size() > tail.size()) head.pop_back();
      else tail.erase(0, 1);
    }
    return head + "..." + tail;
  }

  void load_apps() {
    entries = load_desktop_entries();
    std::sort(entries.begin(), entries.end(),
              [](const DesktopEntry& a, const DesktopEntry& b) { return a.name < b.name; });
    static const struct {
      const char* label;
      const char* mime;
    } kCats[] = {{"Web Browser", "x-scheme-handler/https"}, {"File Manager", "inode/directory"},
                 {"Image Viewer", "image/png"},             {"Text Editor", "text/plain"},
                 {"Video Player", "video/mp4"},             {"PDF Viewer", "application/pdf"},
                 {"Archive Manager", "application/zip"}};
    mime_rows.clear();
    for (const auto& c : kCats) {
      MimeRow row{c.label, c.mime, {}, -1};
      const auto ids = mime_apps_for(c.mime);
      const std::string def = mime_default_for(c.mime);
      for (const auto& id : ids) {
        for (size_t i = 0; i < entries.size(); ++i)
          if (entries[i].id == id) {
            if (id == def) row.selected = static_cast<int>(row.apps.size());
            row.apps.push_back(static_cast<int>(i));
            break;
          }
      }
      mime_rows.push_back(std::move(row));
    }
    terminal_apps.clear();
    int exact_match = -1, executable_match = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
      const std::string& cats = entries[i].categories;
      size_t pos = 0;
      bool is_term = false;
      while (pos <= cats.size()) {
        size_t e = cats.find(';', pos);
        if (e == std::string::npos) e = cats.size();
        if (cats.substr(pos, e - pos) == "TerminalEmulator") is_term = true;
        pos = e + 1;
      }
      if (!is_term) continue;
      const auto argv = exec_argv(entries[i]);
      std::string cmdline;
      for (const auto& a : argv) cmdline += (cmdline.empty() ? "" : " ") + a;
      const int index = static_cast<int>(terminal_apps.size());
      if (default_apps.terminal_command == cmdline) exact_match = index;
      if (!argv.empty() && default_apps.terminal_command == argv[0]) executable_match = index;
      terminal_apps.push_back(static_cast<int>(i));
    }
    // An exact command-line match wins over a mere executable match.
    terminal_selected = exact_match >= 0 ? exact_match : executable_match;
  }

  void update_battery() {
    battery = battery_internal::read_battery_reading(battery_dir);
    redraw();
  }

  // --------------------------------------------------------------- tabs --
  void tab_theme(cairo_t*) {
    ui.row("Theme");
    int t = static_cast<int>(config.theme);
    if (ui.radio_group({"Dark", "Catppuccin", "Dracula", "OLED Black", "Light"}, &t)) {
      config.theme = static_cast<ThemeName>(t);
      save_theme();
    }
    ui.newline();

    ui.row("Accent color");
    bool autoc = config.accent.auto_extract;
    if (ui.checkbox("Auto", &autoc)) {
      config.accent.auto_extract = autoc;
      save_theme();
    }
    if (ui.color_button(&config.accent.hex, !config.accent.auto_extract)) save_theme();
    ui.newline();

    ui.row("Focus border (px)");
    if (ui.spin(&config.focus_border_thickness_px, 0, 10)) save_theme();
    ui.newline();
    ui.row("Focus border color");
    if (ui.color_button(&config.focus_border_color)) save_theme();
    ui.newline();
    ui.row("Window gap (px)");
    if (ui.spin(&config.gap_px, 0, 64)) save_theme();
    ui.newline();
    ui.row("Pinned border (px)");
    if (ui.spin(&config.pinned_border_thickness_px, 0, 10)) save_theme();
    ui.newline();
    ui.row("Pinned border color");
    if (ui.color_button(&config.pinned_border_color)) save_theme();
    ui.newline();
    ui.row("Pinned+focused border color");
    if (ui.color_button(&config.pinned_focused_border_color)) save_theme();
    ui.newline();

    if (has_battery) {
      ui.space(6);
      ui.heading("Power");
      std::string text = "Battery: N/A";
      if (battery.available) {
        text = "Battery: " + std::to_string(battery.percent) + "%";
        text += battery.charging ? " (charging)" : " (on battery)";
        if (battery.hours_remaining >= 0.0) {
          const int mins = static_cast<int>(battery.hours_remaining * 60.0 + 0.5);
          text += " -- " + std::to_string(mins / 60) + "h " + std::to_string(mins % 60) + "m " +
                  (battery.charging ? "until full" : "remaining");
        }
      }
      ui.label(text, true);
      ui.newline();
      ui.row("Power mode");
      int mode = static_cast<int>(bar.power_mode);
      if (ui.radio_group({"Normal", "Performance", "Battery Saver"}, &mode)) {
        bar.power_mode = static_cast<PowerMode>(mode);
        save_bar();  // the bar picks this up live
        spawn_detached({"powerprofilesctl", "set", power_mode_to_profiles_daemon_name(bar.power_mode)});
      }
      ui.newline();
    }
  }

  void tab_bar(cairo_t*) {
    ui.heading("Clock");
    struct {
      const char* label;
      bool* field;
    } toggles[] = {{"Show seconds", &bar.clock.show_seconds}, {"Show date", &bar.clock.show_date},
                   {"Show year", &bar.clock.show_year},       {"Show month", &bar.clock.show_month},
                   {"Show day", &bar.clock.show_day}};
    for (auto& t : toggles) {
      if (ui.checkbox(t.label, t.field)) save_bar();
      ui.newline(-2);
    }
    ui.space(8);
    ui.heading("Workspace colors");
    struct {
      const char* label;
      std::string* field;
    } colors[] = {{"Inactive background", &bar.workspace_colors.inactive_bg},
                  {"Inactive text", &bar.workspace_colors.inactive_fg},
                  {"Active background", &bar.workspace_colors.active_bg},
                  {"Active text", &bar.workspace_colors.active_fg}};
    for (auto& c : colors) {
      ui.row(c.label);
      if (ui.color_button(c.field)) save_bar();
      ui.newline();
    }
    ui.space(4);
    if (ui.checkbox("Rounded workspace buttons", &bar.workspace_colors.buttons_rounded)) save_bar();
    ui.newline();
    ui.space(8);
    ui.heading("Layout");
    ui.row("Bar layout");
    int layout = static_cast<int>(bar.layout);
    if (ui.radio_group({"Full width", "Island (floating pill, 1366px+ displays)"}, &layout)) {
      bar.layout = static_cast<BarLayout>(layout);
      save_bar();
    }
    ui.newline();
  }

  void tab_wallpaper(cairo_t* cr) {
    ui.heading("Wallpaper");
    const std::string shown =
        wallpaper.path.empty() ? "No wallpaper set" : ellipsize(cr, wallpaper.path, ui.width() - 32);
    ui.label(shown, true);
    ui.newline();
    if (ui.button("Choose Image...", !wallpaper.use_solid_color))
      ui.open_file_dialog("Choose Wallpaper", wallpaper.path, {".png", ".jpg", ".jpeg", ".webp"});
    ui.newline();
    std::string chosen;
    if (ui.take_file_result(&chosen)) {
      wallpaper.path = chosen;
      save_wallpaper();
    }
    ui.space(6);
    bool solid = wallpaper.use_solid_color;
    if (ui.checkbox("Use solid color", &solid)) {
      wallpaper.use_solid_color = solid;
      save_wallpaper();
    }
    if (ui.color_button(&wallpaper.solid_color, wallpaper.use_solid_color)) save_wallpaper();
    ui.newline();
  }

  void tab_default_apps(cairo_t*) {
    ui.heading("Default Applications");
    for (auto& row : mime_rows) {
      ui.row(row.label);
      if (row.apps.empty()) {
        ui.label("No apps found", true);
        ui.newline();
        continue;
      }
      std::vector<std::string> names;
      for (int i : row.apps) names.push_back(entries[static_cast<size_t>(i)].name);
      if (ui.radio_group(names, &row.selected) && row.selected >= 0)
        mime_set_default(row.mime, entries[static_cast<size_t>(row.apps[static_cast<size_t>(row.selected)])].id);
      ui.newline();
    }
    ui.row("Terminal");
    if (terminal_apps.empty()) {
      ui.label("No apps found", true);
    } else {
      std::vector<std::string> names;
      for (int i : terminal_apps) names.push_back(entries[static_cast<size_t>(i)].name);
      if (ui.radio_group(names, &terminal_selected) && terminal_selected >= 0) {
        const auto argv = exec_argv(entries[static_cast<size_t>(terminal_apps[static_cast<size_t>(terminal_selected)])]);
        if (!argv.empty()) {
          default_apps.terminal_command = argv[0];
          save_default_apps_config(default_apps);
        }
      }
    }
    ui.newline();
  }

  void tab_audio(cairo_t*) {
    ui.heading("Master volume");
    if (!master_available) {
      ui.label("Audio unavailable (no PipeWire)", true);
      ui.newline();
    } else {
      if (ui.button(master_muted ? "Unmute" : "Mute")) mixer.set_master_muted(!master_muted);
      int v = master_value;
      if (ui.slider(&v, 0, 100)) {
        master_value = v;
        mixer.set_master_volume(v);
      }
      char buf[8];
      std::snprintf(buf, sizeof buf, "%d", master_value);
      ui.newline();
    }
    ui.space(6);
    ui.heading("Applications");
    if (streams.empty()) {
      ui.label("Nothing is playing", true);
      ui.newline();
    }
    for (auto& s : streams) {
      ui.label(s.label);
      ui.newline(-4);
      int v = s.volume_percent;
      if (ui.slider(&v, 0, 100)) {
        s.volume_percent = v;
        mixer.set_stream_volume(s.node_id, v);
      }
      ui.newline();
    }
  }

  void tab_performance(cairo_t*) {
    ui.row("Render mode");
    int mode = static_cast<int>(config.render_mode);
    if (ui.radio_group({"Synced (Unlocked)", "Custom (FPS Cap)"}, &mode)) {
      config.render_mode = static_cast<RenderMode>(mode);
      save_theme();
    }
    ui.newline();
    ui.row("Custom FPS lock");
    if (ui.spin(&config.custom_fps_lock, 24, 5000, 1, config.render_mode == RenderMode::Custom)) save_theme();
    ui.newline();
    if (ui.checkbox("Show performance overlay on startup", &config.show_debug_overlay_on_startup)) save_theme();
    ui.newline();
  }

  void tab_about(cairo_t*) {
    ui.heading("fleetwm");
    ui.paragraph("A custom wlroots-based Wayland compositor and desktop shell (bar, settings, launcher, wallpaper, greeter).");
    if (ui.link("github.com/BeanGreen247/fleetwm"))
      spawn_detached({"xdg-open", "https://github.com/BeanGreen247/fleetwm"});
    ui.newline();
    ui.space(6);
    ui.label("Sole developer: BeanGreen247");
    ui.newline();
    ui.label("License: MIT", true);
    ui.newline();
  }

  // ---------------------------------------------------------------- draw --
  void draw(cairo_t* cr, int w, int h) {
    ui.begin(cr, w, h);
    ui.set_margins(16, 6, 16);
    ui.set_label_width(236);
    ui.tabs(tab_names, &tab);
    const double top = ui.content_bottom();
    ui.begin_scroll({0, top, static_cast<double>(w), h - top}, &scroll[tab]);
    switch (tab) {
      case 0: tab_theme(cr); break;
      case 1: tab_bar(cr); break;
      case 2: tab_wallpaper(cr); break;
      case 3: tab_default_apps(cr); break;
      case 4: tab_audio(cr); break;
      case 5: tab_performance(cr); break;
      default: tab_about(cr); break;
    }
    ui.end_scroll();
    ui.end();
    if (ui.wants_another_frame()) redraw();
  }
};

}  // namespace

int main() {
  fleetwm::tune_malloc_for_low_rss();
  signal(SIGCHLD, SIG_IGN);

  Settings S;
  S.config = load_theme_config();
  S.bar = load_bar_config();
  S.wallpaper = load_wallpaper_config();
  S.default_apps = load_default_apps_config();
  S.apply_theme();
  S.battery_dir = find_battery_dir();
  S.has_battery = !S.battery_dir.empty();
  if (const char* d = std::getenv("FLEETWM_BATTERY_DIR")) {  // test hook, same as the bar
    S.battery_dir = d;
    S.has_battery = true;
  }
  S.load_apps();

  if (!S.app.connect()) return 1;

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.Settings";  // the compositor floats/centres/keeps this on top by app id
  cfg.title = "Fleetwm Settings";
  cfg.width = kWindowW;
  cfg.height = kWindowH;
  cfg.min_width = 420;
  cfg.min_height = 320;
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
    if (e.pressed && S.ui.wants_paste() &&
        ((e.sym == XKB_KEY_v && (e.mods & kCtrl)) || (e.sym == XKB_KEY_Insert && (e.mods & kShift)))) {
      S.app.paste_text([&S](const std::string& text) {
        S.ui.paste(text);
        S.redraw();
      });
      return;
    }
    S.ui.key(e);
    S.redraw();
  };
  S.surface->on_closed = [&S] { S.app.quit(); };

  S.mixer.start(
      [&S](int percent, bool muted, bool available) {
        S.master_available = available;
        S.master_muted = muted;
        S.master_value = percent;
        S.redraw();
      },
      [&S](const std::vector<common::AudioStream>& s) {
        S.streams = s;
        S.redraw();
      },
      [&S](std::function<void()> fn) { S.app.post(std::move(fn)); });

  if (S.has_battery) {
    S.update_battery();
    S.app.add_timer(15000, [&S] { S.update_battery(); });
  }

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  S.app.watch_fd(sfd, [&S] { S.app.quit(); });

  S.app.run();
  return 0;
}
