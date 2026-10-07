#pragma once

// Settings -> Network: every network card with its state, and for Wi-Fi cards the networks in
// range (one block per card) with connect, disconnect and forget. Works with NetworkManager
// or wpa_supplicant (see src/netmgr); with neither it shows what the kernel reports.

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "fleetkit.hpp"
#include "network_glyphs.hpp"
#include "network_parse.hpp"
#include "network_types.hpp"
#include "ui.hpp"

namespace fleetwm {

class NetworkTab {
 public:
  NetworkTab(kit::App& app, std::function<void()> redraw) : app_(app), redraw_(std::move(redraw)) {}
  ~NetworkTab() { stop_timer(); }

  // Call every frame the tab is on screen.
  void draw(kit::Ui& ui, cairo_t* cr) {
    ++frames_;
    if (!backend_) {
      backend_ = net::make_backend();
      refresh(true);
    }
    if (!timer_) start_timer();
    if (refresh_pending_) {  // an action was taken during the last frame
      refresh_pending_ = false;
      refresh(false);
    }

    const kit::Palette& p = ui.palette();
    ui.label("Managed by: " + snap_.backend, true);
    ui.newline();
    if (!snap_.note.empty()) {
      ui.paragraph(snap_.note);
    }
    if (!message_.empty()) {
      ui.label(message_);
      ui.newline();
    }
    if (snap_.devices.empty()) {
      ui.space(8);
      ui.paragraph("No network cards were found.");
      return;
    }

    for (const net::Device& d : snap_.devices) {
      ui.space(4);
      ui.section(std::string(d.kind == net::Kind::Wifi ? "Wi-Fi" : d.kind == net::Kind::Mobile ? "Mobile data" : "Ethernet") + "  -  " + d.name);
      status_card(ui, cr, p, d);
      if (d.kind == net::Kind::Wifi) wifi_block(ui, cr, p, d);
    }
    if (snap_.can_switch_radio && !any_wifi_) {
      // NetworkManager with the radio off lists no cards; keep the switch reachable.
      ui.space(4);
      ui.row("Wi-Fi radio");
      bool on = snap_.wifi_enabled;
      if (ui.toggle(&on)) switch_radio(on);
      ui.newline();
    }
  }

 private:
  using Clock = std::chrono::steady_clock;

  void status_card(kit::Ui& ui, cairo_t* cr, const kit::Palette& p, const net::Device& d) {
    // One fact per line: addresses, hardware address, gateway, driver.
    std::vector<std::pair<std::string, std::string>> lines;
    for (const std::string& a : d.addresses) lines.emplace_back("IP address", a);
    if (!d.hw_addr.empty()) lines.emplace_back("MAC address", d.hw_addr);
    if (!d.gateway.empty()) lines.emplace_back("Gateway", d.gateway);
    if (!d.driver.empty()) lines.emplace_back("Driver", d.driver);

    kit::UiRect r;
    ui.canvas(std::max(58.0, 46.0 + 18.0 * static_cast<double>(lines.size()) + 8), &r);
    const net::GlyphColor fg{p.fg_secondary.r, p.fg_secondary.g, p.fg_secondary.b, 1.0};
    const net::GlyphColor accent{p.accent.r, p.accent.g, p.accent.b, 1.0};
    const bool up = d.state == net::State::Connected;
    const double cy = r.y + 29;
    if (d.kind == net::Kind::Wifi)
      net::draw_wifi_glyph(cr, r.x + 28, cy, 34, up ? net::wifi_bars_lit(d.signal > 0 ? d.signal : 100) : 0, fg,
                           d.state == net::State::Unavailable);
    else if (d.kind == net::Kind::Mobile)
      net::draw_mobile_glyph(cr, r.x + 28, cy, 34, up ? 5 : 0, fg, d.state == net::State::Unavailable);
    else
      net::draw_ethernet_glyph(cr, r.x + 28, cy, 32, up, up ? accent : fg, d.state == net::State::Unavailable);

    kit::draw_text(cr, net::describe_device(d), r.x + 64, cy + 5, 17, p.fg_primary, true);
    double y = r.y + 58;
    for (const auto& [label, value] : lines) {
      kit::draw_text(cr, label, r.x + 64, y, 13, p.fg_secondary);
      kit::draw_text(cr, value, r.x + 64 + 104, y, 13, p.fg_primary);
      y += 18;
    }
  }

  void wifi_block(kit::Ui& ui, cairo_t* cr, const kit::Palette& p, const net::Device& d) {
    if (!d.controllable) {
      ui.paragraph("This card is managed by something else, so it can only be viewed here.");
      return;
    }
    if (snap_.can_switch_radio) {
      ui.row("Wi-Fi radio");
      bool on = snap_.wifi_enabled;
      if (ui.toggle(&on)) switch_radio(on);
      ui.newline();
    }
    if (d.state == net::State::Unavailable && snap_.can_switch_radio && !snap_.wifi_enabled) return;

    if (ui.button("Scan for networks")) {
      backend_->scan(d.name);
      message_ = "Looking for networks...";
      scan_due_ = Clock::now() + std::chrono::seconds(2);
    }
    ui.newline();
    ui.space(2);
    if (!d.failure.empty() && attempt_.device == d.name) message_ = d.failure;

    if (d.access_points.empty()) {
      ui.paragraph("No networks found yet. Use Scan if the list stays empty.");
      return;
    }
    for (const net::AccessPoint& ap : d.access_points) ap_row(ui, cr, p, d, ap);
  }

  void ap_row(kit::Ui& ui, cairo_t* cr, const kit::Palette& p, const net::Device& d, const net::AccessPoint& ap) {
    const std::string key = d.name + "/" + ap.ssid;
    const bool open_row = selected_ == key;
    kit::UiRect r;
    const kit::Ui::CanvasEvent ev = ui.canvas(40, &r);
    std::string tag;
    if (ap.active) tag = d.state == net::State::Connecting ? "Connecting..." : "Connected";
    else if (attempt_.device == d.name && attempt_.ssid == ap.ssid) tag = "Connecting...";
    else if (ap.saved) tag = "Saved";
    if (ap.secured) tag += tag.empty() ? "Secured" : "  -  Secured";
    else tag += tag.empty() ? "Open" : "  -  Open";

    // "Forget" sits in the row of every saved network, left of its status text.
    const double tag_w = kit::measure_text(cr, tag, 12).width;
    kit::UiRect forget{r.x + r.w - tag_w - 14 - 74, r.y + 8, 62, 24};
    const bool over_forget = ap.saved && ev.x >= forget.x - r.x && ev.x < forget.x - r.x + forget.w &&
                             ev.y >= forget.y - r.y && ev.y < forget.y - r.y + forget.h;
    if (ev.pressed) {
      if (over_forget) {
        act(d.name, ap.ssid, "", Action::Forget);
        return;
      }
      selected_ = open_row ? "" : key;
      password_.clear();
      show_password_ = false;
    }
    const bool hover = ev.x >= 0 && ev.x < r.w && ev.y >= 0 && ev.y < r.h;
    kit::rounded_rect(cr, r.x, r.y, r.w, r.h, p.rounded ? 8 : 0);
    kit::Color bg = ap.active ? p.accent : p.bg_secondary;
    bg.a = ap.active ? 0.16 : (hover || open_row ? 0.9 : 0.55);
    kit::set_source(cr, bg);
    cairo_fill(cr);

    const net::GlyphColor fg{p.fg_primary.r, p.fg_primary.g, p.fg_primary.b, 1.0};
    net::draw_signal_bars(cr, r.x + 14, r.y + 28, 18, net::signal_bars_lit(ap.strength), ap.active ? net::GlyphColor{p.accent.r, p.accent.g, p.accent.b, 1.0} : fg);
    const double nx = r.x + 46;
    kit::draw_text(cr, ap.ssid, nx, r.y + 25, 15, p.fg_primary, ap.active);
    const kit::TextExtents te = kit::measure_text(cr, tag, 12);
    kit::draw_text(cr, tag, r.x + r.w - te.width - 14, r.y + 25, 12, ap.active ? p.accent : p.fg_secondary);
    if (ap.saved) {
      kit::rounded_rect(cr, forget.x, forget.y, forget.w, forget.h, p.rounded ? 6 : 0);
      kit::Color edge = over_forget ? p.accent : p.fg_secondary;
      edge.a = over_forget ? 1.0 : 0.45;
      kit::set_source(cr, edge);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
      const kit::TextExtents fe = kit::measure_text(cr, "Forget", 12);
      kit::draw_text(cr, "Forget", forget.x + (forget.w - fe.width) / 2, forget.y + 16.5, 12, over_forget ? p.accent : p.fg_secondary);
    }

    if (!open_row) return;
    // The panel under the row: what can be done with this network.
    if (ap.active) {
      if (ui.button("Disconnect")) act(d.name, ap.ssid, "", Action::Disconnect);
      ui.newline();
    } else if (ap.saved || !ap.secured) {
      if (ui.button("Connect", true, true)) act(d.name, ap.ssid, "", Action::Connect);
      ui.newline();
    } else {
      ui.label("Password for " + ap.ssid, true);
      ui.newline();
      ui.text_entry(&password_, 300, true, true, &show_password_);
      const bool enter = ui.entry_submitted();
      ui.same_line();
      if ((ui.button("Join", !password_.empty(), true) || (enter && !password_.empty()))) {
        act(d.name, ap.ssid, password_, Action::Connect);
        password_.clear();
      }
      ui.newline();
    }
    ui.space(4);
  }

  enum class Action { Connect, Disconnect, Forget };

  // Arguments are copies on purpose: they usually come from the snapshot, which a refresh replaces.
  void act(std::string device, std::string ssid, std::string password, Action what) {
    std::string err;
    bool ok = false;
    switch (what) {
      case Action::Connect: {
        // Remember whether a profile existed, to drop a bad password's profile again on failure.
        bool was_saved = false;
        for (const net::Device& d : snap_.devices)
          for (const net::AccessPoint& a : d.access_points)
            if (d.name == device && a.ssid == ssid) was_saved = a.saved;
        ok = backend_->connect(device, ssid, password, &err);
        if (ok) {
          attempt_ = {device, ssid, Clock::now(), was_saved};
          message_ = "Connecting to " + ssid + "...";
          selected_.clear();
        }
        break;
      }
      case Action::Disconnect:
        ok = backend_->disconnect(device, &err);
        if (ok) message_ = "Disconnected.";
        selected_.clear();
        break;
      case Action::Forget:
        ok = backend_->forget(device, ssid, &err);
        if (ok) message_ = "Forgot " + ssid + ".";
        selected_.clear();
        break;
    }
    if (!ok) message_ = "Could not do that: " + err;
    refresh_pending_ = true;  // not now: the list being drawn is part of the snapshot
    redraw_();
  }

  void switch_radio(bool on) {
    std::string err;
    if (!backend_->set_wifi_enabled(on, &err)) message_ = "Could not switch Wi-Fi: " + err;
    refresh_pending_ = true;
    redraw_();
  }

  void refresh(bool ask_scan) {
    snap_ = backend_->snapshot();
    any_wifi_ = std::any_of(snap_.devices.begin(), snap_.devices.end(), [](const net::Device& d) { return d.kind == net::Kind::Wifi; });
    if (ask_scan) {
      for (const net::Device& d : snap_.devices)
        if (d.kind == net::Kind::Wifi && d.controllable) backend_->scan(d.name);
      scan_due_ = Clock::now() + std::chrono::seconds(2);
    }
    // A connect attempt: done when connected, given up on after a while (wrong password).
    if (!attempt_.ssid.empty()) {
      for (const net::Device& d : snap_.devices) {
        if (d.name != attempt_.device) continue;
        if (d.state == net::State::Connected && d.connection == attempt_.ssid) {
          message_ = "Connected to " + attempt_.ssid + ".";
          attempt_ = {};
        } else if (!d.failure.empty()) {
          message_ = d.failure;
          attempt_ = {};
        } else if (Clock::now() - attempt_.started > std::chrono::seconds(30)) {
          message_ = "Could not connect to " + attempt_.ssid + ". Check the password and try again.";
          if (!attempt_.was_saved) {
            std::string ignore;
            backend_->forget(attempt_.device, attempt_.ssid, &ignore);
          }
          backend_->disconnect(attempt_.device, &ignore_);
          attempt_ = {};
        }
      }
    }
  }

  void start_timer() {
    last_frames_ = frames_;
    timer_ = app_.add_timer(2000, [this] {
      if (frames_ == last_frames_) {  // not drawn since the last tick: the tab was left
        stop_timer();
        backend_.reset();  // a fresh look next time
        return;
      }
      last_frames_ = frames_;
      refresh(false);
      if (++ticks_ % 8 == 0)  // networks come and go: ask for a fresh scan every ~16 s
        for (const net::Device& d : snap_.devices)
          if (d.kind == net::Kind::Wifi && d.controllable) backend_->scan(d.name);
      redraw_();
    });
  }
  void stop_timer() {
    if (timer_) app_.unwatch(timer_);
    timer_ = 0;
  }

  struct Attempt {
    std::string device, ssid;
    Clock::time_point started{};
    bool was_saved = false;
  };

  kit::App& app_;
  std::function<void()> redraw_;
  std::unique_ptr<net::Backend> backend_;
  net::Snapshot snap_;
  bool any_wifi_ = false, refresh_pending_ = false;
  int timer_ = 0;
  unsigned frames_ = 0, last_frames_ = 0, ticks_ = 0;
  bool show_password_ = false;
  std::string selected_, password_, message_, ignore_;
  Attempt attempt_;
  Clock::time_point scan_due_{};
};

}  // namespace fleetwm
