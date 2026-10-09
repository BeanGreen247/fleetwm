#pragma once

// Settings -> Bluetooth: the adapter's power and visibility, the devices (connected, paired, and nearby while scanning) with
// connect, disconnect, pair and forget, and the questions pairing asks (confirm a number, show a code, type a PIN).
// Talks to BlueZ through src/btmgr. Scanning runs only while this page is on screen.

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "bt_backend.hpp"
#include "bt_types.hpp"
#include "fleetkit.hpp"
#include "ui.hpp"

namespace fleetwm {

class BluetoothTab {
 public:
  BluetoothTab(kit::App& app, std::function<void()> redraw) : app_(app), redraw_(std::move(redraw)) {}
  ~BluetoothTab() { close(); }

  // Call every frame the tab is on screen.
  void draw(kit::Ui& ui, cairo_t* cr) {
    ++frames_;
    if (!backend_) open();
    if (!timer_) start_timer();
    if (refresh_pending_) {
      refresh_pending_ = false;
      refresh();
    }
    const kit::Palette& p = ui.palette();

    if (!backend_) {
      ui.paragraph("Bluetooth cannot be reached: the system bus is not available.");
      return;
    }
    if (!state_.available) {
      ui.paragraph(state_.error.empty() ? "No Bluetooth adapter was found. If this computer has one, make sure the bluetooth service is running (systemctl status bluetooth)."
                                        : "Bluetooth is not available: " + state_.error);
      return;
    }

    draw_prompt(ui, p);

    ui.row("Bluetooth");
    bool on = state_.adapter.powered;
    if (ui.toggle(&on)) {
      std::string err;
      if (!backend_->set_powered(on, &err)) message_ = "Could not switch Bluetooth: " + err;
      else message_.clear();
      state_.adapter.powered = on;  // shown at once; the signal confirms it
      scanning_ = false;
      refresh_pending_ = true;
    }
    ui.label(state_.adapter.name.empty() ? state_.adapter.address : state_.adapter.name, true);
    ui.newline();
    if (!state_.adapter.powered) {
      ui.paragraph("Bluetooth is off. Turn it on to find and use devices.");
      if (!message_.empty()) {
        ui.label(message_);
        ui.newline();
      }
      return;
    }
    ui.row("Visible to other devices");
    bool vis = state_.adapter.discoverable;
    if (ui.toggle(&vis)) {
      std::string err;
      if (!backend_->set_discoverable(vis, &err)) message_ = "Could not change visibility: " + err;
      refresh_pending_ = true;
    }
    ui.newline();
    if (!message_.empty()) {
      ui.label(message_);
      ui.newline();
    }

    ui.space(6);
    ui.section("Devices");
    if (ui.button(scanning_ ? "Stop searching" : "Search for devices")) {
      set_scanning(!scanning_);
    }
    ui.same_line();
    ui.checkbox("Show devices without a name", &show_unnamed_);
    ui.newline();
    if (scanning_) {
      ui.label("Searching... put the device in pairing mode.", true);
      ui.newline();
    }
    ui.space(2);
    int shown = 0;
    for (const bt::Device& d : state_.devices) {
      if (!show_unnamed_ && bt::is_unnamed(d)) continue;
      device_row(ui, cr, p, d);
      ++shown;
    }
    if (shown == 0) {
      ui.paragraph(scanning_ ? "Nothing found yet." : "No devices yet. Search for devices, with the device in pairing mode.");
    }
  }

 private:
  void open() {
    backend_ = bt::Backend::open();
    if (!backend_) return;
    backend_->on_change = [this] { schedule_refresh(); };
    backend_->on_result = [this](const std::string& op, const std::string& path, bool ok, const std::string& error) {
      busy_.erase(path);
      const std::string who = name_of(path);
      if (!ok) message_ = (op == "pair" ? "Could not pair with " : op == "connect" ? "Could not connect to " : "Could not disconnect from ") + who + ": " + error;
      else if (op == "pair") message_ = "Paired with " + who + ".";
      else if (op == "connect") message_ = "Connected to " + who + ".";
      else message_ = "Disconnected from " + who + ".";
      refresh_pending_ = true;
      redraw_();
    };
    backend_->on_prompt = [this] {
      text_.clear();
      redraw_();
    };
    bus_watch_ = app_.watch_fd(backend_->bus_fd(), [this] {
      if (backend_) backend_->process();
    });
    refresh();
  }

  void close() {
    if (refresh_timer_) app_.unwatch(refresh_timer_);
    refresh_timer_ = 0;
    if (timer_) app_.unwatch(timer_);
    timer_ = 0;
    if (bus_watch_) app_.unwatch(bus_watch_);
    bus_watch_ = 0;
    backend_.reset();  // stops a running scan and drops the pairing agent
    scanning_ = false;
    busy_.clear();
  }

  void refresh() {
    if (!backend_) return;
    state_ = backend_->read();
    for (bt::Device& d : state_.devices) d.connecting = busy_.count(d.path) > 0;
  }

  // BlueZ reports every signal-strength change while scanning; fold a burst into one refresh.
  void schedule_refresh() {
    if (refresh_timer_) return;
    refresh_timer_ = app_.add_oneshot(300, [this] {
      refresh_timer_ = 0;
      refresh();
      redraw_();
    });
  }

  void set_scanning(bool on) {
    if (!backend_) return;
    std::string err;
    if (backend_->set_discovery(on, &err)) scanning_ = on;
    else message_ = "Could not search: " + err;
    refresh_pending_ = true;
  }

  std::string name_of(const std::string& path) const {
    for (const bt::Device& d : state_.devices)
      if (d.path == path) return bt::display_name(d);
    const std::string a = bt::address_from_path(path);
    return a.empty() ? "the device" : a;
  }

  void start_timer() {
    last_frames_ = frames_;
    if (!scanning_ && state_.available && state_.adapter.powered && !auto_started_) {  // look around when the page opens
      auto_started_ = true;
      set_scanning(true);
    }
    timer_ = app_.add_timer(2000, [this] {
      if (frames_ == last_frames_) {  // not drawn since the last tick: the page was left
        close();
        auto_started_ = false;
        return;
      }
      last_frames_ = frames_;
      refresh();  // battery levels and the like do not send a signal we watch
      redraw_();
    });
  }

  // ----------------------------------------------------------- pairing prompt --
  void draw_prompt(kit::Ui& ui, const kit::Palette& p) {
    const bt::Prompt& pr = backend_->prompt();
    if (pr.kind == bt::Prompt::Kind::None) return;
    const std::string who = pr.device_name.empty() ? "the device" : pr.device_name;
    ui.section("Pairing with " + who);
    using K = bt::Prompt::Kind;
    switch (pr.kind) {
      case K::Confirm:
        ui.paragraph("Does " + who + " show this number?", false);
        ui.title(pr.code);
        if (ui.button("Yes, they match", true, true)) backend_->answer(true);
        ui.same_line();
        if (ui.button("No")) backend_->answer(false);
        ui.newline();
        break;
      case K::DisplayCode:
        ui.paragraph("Type this code on " + who + ", then press Enter there:", false);
        ui.title(pr.code);
        if (ui.button("Close")) backend_->on_prompt();
        ui.newline();
        break;
      case K::EnterPin:
      case K::EnterPasskey: {
        ui.paragraph(pr.kind == K::EnterPin ? "Type the PIN for " + who + " (often 0000 or 1234):" : "Type the 6 digits " + who + " shows:", false);
        ui.text_entry(&text_, 200);
        const bool enter = ui.entry_submitted();
        ui.same_line();
        if (ui.button("OK", !text_.empty(), true) || (enter && !text_.empty())) backend_->answer_text(text_);
        ui.same_line();
        if (ui.button("Cancel")) backend_->answer(false);
        ui.newline();
        break;
      }
      case K::Authorize:
        ui.paragraph(who + " wants to connect to this computer.", false);
        if (ui.button("Allow", true, true)) backend_->answer(true);
        ui.same_line();
        if (ui.button("Deny")) backend_->answer(false);
        ui.newline();
        break;
      case K::None: break;
    }
    ui.space(6);
    (void)p;
  }

  // ------------------------------------------------------------------- rows --
  void device_row(kit::Ui& ui, cairo_t* cr, const kit::Palette& p, const bt::Device& d) {
    const bool open_row = selected_ == d.path;
    kit::UiRect r;
    const kit::Ui::CanvasEvent ev = ui.canvas(46, &r);
    if (ev.pressed) selected_ = open_row ? "" : d.path;
    const bool hover = ev.x >= 0 && ev.x < r.w && ev.y >= 0 && ev.y < r.h;
    kit::rounded_rect(cr, r.x, r.y, r.w, r.h, p.rounded ? 8 : 0);
    kit::Color bg = d.connected ? p.accent : p.bg_secondary;
    bg.a = d.connected ? 0.16 : (hover || open_row ? 0.9 : 0.55);
    kit::set_source(cr, bg);
    cairo_fill(cr);

    // A coloured dot for the state: accent when connected, outline when paired, faint when only nearby.
    const double dx = r.x + 18, dy = r.y + r.h / 2;
    cairo_arc(cr, dx, dy, 5, 0, 2 * M_PI);
    if (d.connected) {
      kit::set_source(cr, p.accent);
      cairo_fill(cr);
    } else {
      kit::Color c = p.fg_secondary;
      c.a = d.paired ? 0.9 : 0.35;
      kit::set_source(cr, c);
      cairo_set_line_width(cr, 1.5);
      cairo_stroke(cr);
    }
    kit::draw_text(cr, bt::display_name(d), r.x + 36, r.y + 20, 15, p.fg_primary, d.connected);
    const std::string kind = bt::kind_label(bt::kind_from_icon(d.icon));
    kit::draw_text(cr, kind, r.x + 36, r.y + 38, 12, p.fg_secondary);
    const std::string status = bt::status_line(d);
    const kit::TextExtents te = kit::measure_text(cr, status, 12);
    kit::draw_text(cr, status, r.x + r.w - te.width - 14, r.y + 28, 12, d.connected ? p.accent : p.fg_secondary);

    if (!open_row) return;
    const bool busy = busy_.count(d.path) > 0;
    if (!d.paired) {
      if (ui.button("Pair", !busy, true)) start(d, "pair");
    } else if (d.connected) {
      if (ui.button("Disconnect", !busy)) start(d, "disconnect");
    } else {
      if (ui.button("Connect", !busy, true)) start(d, "connect");
    }
    if (d.paired) {
      ui.same_line();
      if (ui.button("Forget", !busy)) forget(d);
    }
    ui.newline();
    ui.label(d.address, true);
    ui.newline();
    ui.space(4);
  }

  void start(const bt::Device& d, const char* op) {
    busy_.insert(d.path);
    message_ = std::string(op[0] == 'p' ? "Pairing with " : op[0] == 'c' ? "Connecting to " : "Disconnecting from ") + bt::display_name(d) + "...";
    if (op[0] == 'p') backend_->pair_device(d.path);
    else if (op[0] == 'c') backend_->connect_device(d.path);
    else backend_->disconnect_device(d.path);
    refresh_pending_ = true;
    redraw_();
  }

  void forget(const bt::Device& d) {
    std::string err;
    if (backend_->remove_device(d.path, &err)) message_ = "Forgot " + bt::display_name(d) + ".";
    else message_ = "Could not forget it: " + err;
    selected_.clear();
    refresh_pending_ = true;
    redraw_();
  }

  kit::App& app_;
  std::function<void()> redraw_;
  std::unique_ptr<bt::Backend> backend_;
  bt::State state_;
  std::set<std::string> busy_;  // device paths with a call in flight
  std::string selected_, message_, text_;
  bool scanning_ = false, show_unnamed_ = false, refresh_pending_ = false, auto_started_ = false;
  int timer_ = 0, bus_watch_ = 0, refresh_timer_ = 0;
  unsigned frames_ = 0, last_frames_ = 0;
};

}  // namespace fleetwm
