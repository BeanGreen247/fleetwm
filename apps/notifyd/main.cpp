// fleetwm-notifyd: the desktop notification service (org.freedesktop.Notifications). Programs call it through libnotify, Qt, Electron, Steam and so on;
// without a server on the bus every one of those calls waits for D-Bus to give up on starting one (25 s), which freezes the program that made it:
// Heroic stopped answering its Wayland connection during such a wait and the compositor dropped it. The compositor starts this at login.
//
// Each notification is a rounded card in the top right corner (a layer surface in the overlay layer): the program's icon, its name, a summary and up to
// three lines of text. A click closes it (and runs the "default" action when the program offered one); the card goes by itself after its timeout.

#include <signal.h>
#include <sys/signalfd.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app_icon.hpp"
#include "fleetkit.hpp"
#include "ipc_client.hpp"
#include "malloc_tuning.hpp"
#include "quit_signals.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

extern "C" const sd_bus_vtable fleetwm_notifyd_vtable[];

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;
using Clock = std::chrono::steady_clock;

constexpr const char* kName = "org.freedesktop.Notifications";
constexpr const char* kPath = "/org/freedesktop/Notifications";
constexpr int kCardW = 360, kMargin = 14, kGap = 10, kPad = 12, kIconPx = 36, kMaxShown = 5, kHeaderH = 26;
constexpr double kSummaryPx = 14, kBodyPx = 12.5, kAppPx = 11;
constexpr int kDefaultTimeoutMs = 6000;

enum Reason : uint32_t { kExpired = 1, kDismissed = 2, kClosedByCall = 3 };

struct Note {
  uint32_t id = 0;
  std::string app, summary, body, default_action;
  cairo_surface_t* icon = nullptr;  // owned by the icon cache
  Clock::time_point expires;
  bool persistent = false;
  bool critical = false;
  std::vector<std::string> lines;  // the body, wrapped
  int height = 0;
  std::unique_ptr<Surface> surface;
};

struct Daemon {
  App app;
  sd_bus* bus = nullptr;
  sd_bus_slot* slot = nullptr;
  Palette pal;
  MenuTheme theme;
  std::vector<std::unique_ptr<Note>> notes;  // newest last
  uint32_t next_id = 1;
  cairo_t* measure = nullptr;
  cairo_surface_t* measure_surface = nullptr;

  void init_measure() {
    measure_surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    measure = cairo_create(measure_surface);
  }

  // Splits `text` into at most `max_lines` lines no wider than `width` px; the last one ends in an ellipsis when text was left over.
  std::vector<std::string> wrap(const std::string& text, double px, double width, size_t max_lines) {
    std::vector<std::string> out;
    std::string line, word;
    auto push_word = [&](const std::string& w) {
      const std::string trial = line.empty() ? w : line + " " + w;
      if (line.empty() || measure_text(measure, trial, px).width <= width) {
        line = trial;
      } else {
        out.push_back(line);
        line = w;
      }
    };
    for (size_t i = 0; i <= text.size(); ++i) {
      const char c = i < text.size() ? text[i] : ' ';
      if (c == '\n' || c == ' ') {
        if (!word.empty()) push_word(word);
        word.clear();
        if (c == '\n' && !line.empty()) {
          out.push_back(line);
          line.clear();
        }
      } else {
        word += c;
      }
    }
    if (!line.empty()) out.push_back(line);
    if (out.size() > max_lines) {
      out.resize(max_lines);
      out.back() += "…";
    }
    return out;
  }

  // The body may carry the markup the spec allows (<b>, <i>, <u>, <a href>, <img>): drop the tags, keep the text.
  static std::string plain(const std::string& in) {
    std::string out;
    bool tag = false;
    for (size_t i = 0; i < in.size(); ++i) {
      if (in[i] == '<') tag = true;
      else if (in[i] == '>' && tag) tag = false;
      else if (!tag) out += in[i];
    }
    for (const auto& [from, to] : {std::pair<const char*, const char*>{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}}) {
      for (size_t at = out.find(from); at != std::string::npos; at = out.find(from, at + 1)) out.replace(at, std::strlen(from), to);
    }
    return out;
  }

  void relayout() {
    int y = kMargin;
    const size_t first = notes.size() > static_cast<size_t>(kMaxShown) ? notes.size() - kMaxShown : 0;
    for (size_t i = 0; i < notes.size(); ++i) {
      Note& n = *notes[i];
      if (!n.surface) continue;
      if (i < first) {  // more than fit: the oldest wait their turn off screen
        n.surface->set_margins(-4000, kMargin, 0, 0);
        continue;
      }
      n.surface->set_margins(y, kMargin, 0, 0);
      y += n.height + kGap;
    }
  }

  // The card is a hybrid: a Windows 7 balloon's glass body with its light inner rim, a Windows 10 toast's accent bar and close button, and a macOS
  // banner's soft shadow, header line (app icon, name, "now") and large icon beside the text.
  void draw(cairo_t* cr, const Note& n) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const double x0 = 6, y0 = 6, w = kCardW, h = n.height, r = 10;
    for (int i = 6; i >= 1; --i) {  // a wide soft shadow
      rounded_rect(cr, x0 - i + 0.5, y0 - i + 3 - i * 0.2, w + 2 * i - 1, h + 2 * i - 1, r + i);
      set_source(cr, Color{theme.shadow.r, theme.shadow.g, theme.shadow.b, theme.shadow.a * 0.8});
      cairo_fill(cr);
    }
    const Color accent = n.critical ? Color{0.92, 0.33, 0.30, 1} : pal.accent;
    rounded_rect(cr, x0, y0, w, h, r);
    cairo_save(cr);
    cairo_clip_preserve(cr);
    {  // glass: the card colour, lighter at the top like a Windows 7 pane
      cairo_pattern_t* g = cairo_pattern_create_linear(0, y0, 0, y0 + h);
      cairo_pattern_add_color_stop_rgba(g, 0, std::min(1.0, theme.background.r + 0.10), std::min(1.0, theme.background.g + 0.10),
                                        std::min(1.0, theme.background.b + 0.12), 0.97);
      cairo_pattern_add_color_stop_rgba(g, 0.45, theme.background.r, theme.background.g, theme.background.b, 0.96);
      cairo_pattern_add_color_stop_rgba(g, 1, theme.background.r * 0.9, theme.background.g * 0.9, theme.background.b * 0.9, 0.97);
      cairo_set_source(cr, g);
      cairo_fill(cr);
      cairo_pattern_destroy(g);
    }
    set_source(cr, accent);  // the Windows 10 accent bar down the left edge
    cairo_rectangle(cr, x0, y0, 4, h);
    cairo_fill(cr);
    cairo_restore(cr);
    rounded_rect(cr, x0 + 0.5, y0 + 0.5, w - 1, h - 1, r);  // dark outer line, light inner line: the Windows 7 rim
    cairo_set_source_rgba(cr, 0, 0, 0, 0.65);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    rounded_rect(cr, x0 + 1.5, y0 + 1.5, w - 3, h - 3, r - 1);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
    cairo_stroke(cr);

    const double left = x0 + kPad + 4;
    double y = y0 + 9;
    // header: small icon, app name, "now" and the close cross
    if (n.icon) draw_app_icon(cr, n.icon, left, y, 14);
    const double name_x = left + (n.icon ? 20 : 0);
    const TextExtents ne = measure_text(cr, n.app.empty() ? "Notification" : n.app, kAppPx);
    draw_text(cr, n.app.empty() ? "Notification" : n.app, name_x, y + 7 + ne.ascent / 2, kAppPx, theme.secondary);
    const double close_x = x0 + w - kPad - 6, close_y = y + 7;
    const TextExtents te_now = measure_text(cr, "now", kAppPx);
    draw_text(cr, "now", close_x - 22 - te_now.width, y + 7 + te_now.ascent / 2, kAppPx, theme.secondary);
    set_source(cr, theme.secondary);
    cairo_set_line_width(cr, 1.4);
    cairo_move_to(cr, close_x - 4, close_y - 4);
    cairo_line_to(cr, close_x + 4, close_y + 4);
    cairo_move_to(cr, close_x + 4, close_y - 4);
    cairo_line_to(cr, close_x - 4, close_y + 4);
    cairo_stroke(cr);
    y += kHeaderH;
    double text_x = left;
    if (n.icon) {  // the large icon beside the text, as a macOS banner shows the app
      draw_app_icon(cr, n.icon, left, y + 2, kIconPx);
      text_x += kIconPx + 10;
    }
    const double text_w = x0 + w - kPad - text_x;
    const std::string summary = ellipsize(cr, n.summary, kSummaryPx, text_w);
    TextExtents te = measure_text(cr, summary, kSummaryPx, true);
    draw_text(cr, summary, text_x, y + te.ascent, kSummaryPx, theme.text, true);
    y += te.height + 4;
    for (const std::string& line : n.lines) {
      te = measure_text(cr, line, kBodyPx);
      draw_text(cr, line, text_x, y + te.ascent, kBodyPx, theme.text);
      y += te.height + 3;
    }
  }

  // `s` cut at a character boundary and ended with an ellipsis when it is wider than `width` px.
  static std::string ellipsize(cairo_t* cr, const std::string& s, double px, double width) {
    if (measure_text(cr, s, px, true).width <= width) return s;
    std::string out = s;
    while (!out.empty()) {
      do out.pop_back();
      while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);  // not inside a UTF-8 sequence
      if (measure_text(cr, out + "…", px, true).width <= width) break;
    }
    return out + "…";
  }

  Note* find(uint32_t id) {
    for (auto& n : notes)
      if (n->id == id) return n.get();
    return nullptr;
  }

  void close(uint32_t id, Reason why, bool signal = true) {
    for (auto it = notes.begin(); it != notes.end(); ++it) {
      if ((*it)->id != id) continue;
      notes.erase(it);
      if (signal && bus) sd_bus_emit_signal(bus, kPath, kName, "NotificationClosed", "uu", id, static_cast<uint32_t>(why));
      relayout();
      return;
    }
  }

  uint32_t add(const std::string& app_name, uint32_t replaces, const std::string& icon_name, const std::string& summary, const std::string& body,
               const std::string& default_action, int timeout_ms, bool critical, const std::string& desktop_entry) {
    Note* note = replaces ? find(replaces) : nullptr;
    const bool fresh = !note;
    if (fresh) {
      notes.push_back(std::make_unique<Note>());
      note = notes.back().get();
      note->id = next_id++;
      if (next_id == 0) next_id = 1;
    }
    note->app = app_name;
    note->summary = summary;
    note->body = plain(body);
    note->default_action = default_action;
    note->critical = critical;
    note->icon = nullptr;
    if (!icon_name.empty()) note->icon = app_icon(icon_name, 48);
    if (!note->icon && !app_name.empty()) note->icon = app_icon(app_name, 48);
    note->persistent = timeout_ms == 0 || critical;
    note->expires = Clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? kDefaultTimeoutMs : timeout_ms);
    const double text_w = kCardW - 2 * kPad - 4 - (note->icon ? kIconPx + 10 : 0);
    note->lines = wrap(note->body, kBodyPx, text_w, 3);
    const double summary_h = measure_text(measure, "Ag", kSummaryPx, true).height + 4;
    const double body_h = note->lines.size() * (measure_text(measure, "Ag", kBodyPx).height + 3);
    const double content = std::max<double>(note->icon ? kIconPx + 4 : 0, summary_h + body_h);
    note->height = static_cast<int>(9 + kHeaderH + content + 12);
    if (fresh) {
      Surface::Config cfg;
      cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
      cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
      cfg.width = kCardW + 12;
      cfg.height = note->height + 12;
      cfg.exclusive_zone = -1;
      cfg.margin_top = kMargin;
      cfg.margin_right = kMargin;
      cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
      cfg.name = "fleetwm-notification";
      const uint32_t id = note->id;
      note->surface = std::make_unique<Surface>(app, cfg);
      note->surface->on_draw = [this, id](cairo_t* cr, int, int) {
        if (Note* n = find(id)) draw(cr, *n);
      };
      note->surface->on_button = [this, id](double, double, uint32_t, bool pressed) {
        if (pressed) return;
        Note* n = find(id);
        if (!n) return;
        if (!n->default_action.empty() && bus) sd_bus_emit_signal(bus, kPath, kName, "ActionInvoked", "us", id, n->default_action.c_str());
        close(id, kDismissed);
      };
    } else {
      note->surface->set_size(kCardW + 12, note->height + 12);
      note->surface->queue_draw();
    }
    relayout();
    // the program's taskbar button blinks (and in the Tiling layout its border) until the window is focused
    send_urgent(desktop_entry.empty() ? app_name : desktop_entry);
    return note->id;
  }

  // One connection kept open: the compositor drops the lines of a client that has already hung up.
  IpcClient ipc;
  void send_urgent(const std::string& program) {
    if (program.empty()) return;
    if (!ipc.send_command("URGENT " + program)) {
      ipc.connect();
      ipc.send_command("URGENT " + program);
    }
  }

  void sweep() {
    const auto now = Clock::now();
    std::vector<uint32_t> gone;
    for (auto& n : notes)
      if (!n->persistent && n->expires <= now) gone.push_back(n->id);
    for (uint32_t id : gone) close(id, kExpired);
  }

  void process() {
    if (!bus) return;
    int r;
    do {
      r = sd_bus_process(bus, nullptr);
    } while (r > 0);
  }
};

Daemon* g_daemon = nullptr;

}  // namespace

extern "C" int fleetwm_notifyd_caps(sd_bus_message* m, void*, sd_bus_error*) {
  sd_bus_message* reply = nullptr;
  int r = sd_bus_message_new_method_return(m, &reply);
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "as", 3, "body", "body-markup", "actions");
  if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
  sd_bus_message_unref(reply);
  return r;
}

extern "C" int fleetwm_notifyd_info(sd_bus_message* m, void*, sd_bus_error*) {
  return sd_bus_reply_method_return(m, "ssss", "fleetwm-notifyd", "fleetwm", FLEETWM_VERSION, "1.2");
}

extern "C" int fleetwm_notifyd_close(sd_bus_message* m, void*, sd_bus_error*) {
  uint32_t id = 0;
  int r = sd_bus_message_read(m, "u", &id);
  if (r < 0) return r;
  if (g_daemon) g_daemon->close(id, kClosedByCall);
  return sd_bus_reply_method_return(m, "");
}

extern "C" int fleetwm_notifyd_notify(sd_bus_message* m, void*, sd_bus_error*) {
  const char *app = "", *icon = "", *summary = "", *body = "";
  uint32_t replaces = 0;
  int32_t timeout = -1;
  int r = sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body);
  if (r < 0) return r;
  std::string default_action;
  r = sd_bus_message_enter_container(m, 'a', "s");
  if (r < 0) return r;
  for (;;) {  // actions: key, label, key, label ...
    const char *key = nullptr, *label = nullptr;
    if (sd_bus_message_read(m, "s", &key) <= 0) break;
    if (sd_bus_message_read(m, "s", &label) <= 0) break;
    if (key && std::strcmp(key, "default") == 0) default_action = "default";
  }
  sd_bus_message_exit_container(m);
  bool critical = false;
  std::string desktop_entry;
  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;
  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char* key = nullptr;
    if (sd_bus_message_read(m, "s", &key) < 0) break;
    if (key && std::strcmp(key, "urgency") == 0 && sd_bus_message_enter_container(m, 'v', "y") > 0) {
      uint8_t u = 1;
      sd_bus_message_read(m, "y", &u);
      critical = u >= 2;
      sd_bus_message_exit_container(m);
    } else if (key && std::strcmp(key, "desktop-entry") == 0 && sd_bus_message_enter_container(m, 'v', "s") > 0) {
      const char* de = nullptr;
      sd_bus_message_read(m, "s", &de);
      if (de) desktop_entry = de;
      sd_bus_message_exit_container(m);
    } else {
      sd_bus_message_skip(m, "v");
    }
    sd_bus_message_exit_container(m);
  }
  sd_bus_message_exit_container(m);
  r = sd_bus_message_read(m, "i", &timeout);
  if (r < 0) return r;
  if (!g_daemon) return sd_bus_reply_method_return(m, "u", 0u);
  std::string icon_name = icon ? icon : "";
  if (icon_name.empty() && !desktop_entry.empty()) icon_name = desktop_entry;
  const uint32_t id = g_daemon->add(app ? app : "", replaces, icon_name, summary ? summary : "", body ? body : "", default_action, timeout, critical, desktop_entry);
  return sd_bus_reply_method_return(m, "u", id);
}

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-notifyd", "")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  signal(SIGCHLD, SIG_IGN);

  Daemon D;
  g_daemon = &D;
  D.pal = load_palette(load_theme_config());
  D.theme = menu_theme(D.pal);
  D.init_measure();

  int r = sd_bus_open_user(&D.bus);
  if (r < 0) {
    std::fprintf(stderr, "fleetwm-notifyd: cannot connect to the session bus: %s\n", std::strerror(-r));
    return 1;
  }
  r = sd_bus_add_object_vtable(D.bus, &D.slot, kPath, kName, fleetwm_notifyd_vtable, nullptr);
  if (r < 0) {
    std::fprintf(stderr, "fleetwm-notifyd: cannot export the service: %s\n", std::strerror(-r));
    return 1;
  }
  r = sd_bus_request_name(D.bus, kName, SD_BUS_NAME_REPLACE_EXISTING | SD_BUS_NAME_ALLOW_REPLACEMENT);
  if (r < 0) {
    std::fprintf(stderr, "fleetwm-notifyd: %s is taken by another notification service\n", kName);
    return 0;
  }
  if (!D.app.connect()) return 1;  // no screen to show cards on: leave the name unowned so a program is not left waiting on us
  D.app.watch_fd(sd_bus_get_fd(D.bus), [&D] { D.process(); });
  D.app.add_timer(250, [&D] { D.sweep(); });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  D.app.watch_fd(sfd, [&D] { D.app.quit(); });
  D.process();
  D.app.run();
  g_daemon = nullptr;
  return 0;
}
