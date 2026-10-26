#pragma once

// Settings -> Remote Desktop: switches for the built-in VNC and RDP servers (fleetwm-remote). Switching one on
// makes a new random password; the address, port, user name and password shown here are what a viewer or Apache
// Guacamole needs. The daemon watches remote.toml, so saving the file is all it takes to start or stop a server.

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "remote_config.hpp"
#include "remote_usage.hpp"
#include "ui.hpp"

namespace fleetwm {

class RemoteTab {
 public:
  RemoteTab(kit::App& app, std::function<void()> redraw) : app_(app), redraw_(std::move(redraw)) { cfg_ = load_remote_config(); }
  ~RemoteTab() {
    if (timer_) app_.unwatch(timer_);
  }

  void draw(kit::Ui& ui, cairo_t*) {
    ++frames_;
    if (!timer_) {  // the daemon's status file changes while someone connects
      last_frames_ = frames_;
      timer_ = app_.add_timer(2000, [this] {
        if (frames_ == last_frames_) {  // not drawn since the last tick: the tab was left
          app_.unwatch(timer_);
          timer_ = 0;
          return;
        }
        last_frames_ = frames_;
        redraw_();
      });
    }
    read_status();
    if (addresses_.empty()) addresses_ = local_addresses();

    ui.paragraph("Look at and control this desktop from another computer, a phone, or Apache Guacamole. Switching a server on makes a new "
                 "random password; switching it off closes it and disconnects everyone.");
    ui.space(6);
    if (!message_.empty()) {
      ui.label(message_);
      ui.newline();
    }

    ui.section("VNC");
    if (server(ui, {"VNC server", &cfg_.vnc_enabled, &cfg_.vnc_port, &cfg_.vnc_password, kVncPasswordLength, &cfg_.vnc_tls, &cfg_.vnc_quality,
                    &cfg_.vnc_max_kbps, &cfg_.vnc_resolution, &cfg_.vnc_depth, false, vnc_status_, "", &cfg_.vnc_fps, &cfg_.vnc_password_keep, &vnc_edit_}))
      save();
    ui.space(6);
    ui.section("RDP (Remote Desktop)");
    if (server(ui, {"RDP server", &cfg_.rdp_enabled, &cfg_.rdp_port, &cfg_.rdp_password, kRdpPasswordLength, &cfg_.rdp_tls, &cfg_.rdp_quality,
                    &cfg_.rdp_max_kbps, &cfg_.rdp_resolution, &cfg_.rdp_depth, true, rdp_status_, remote_login_name(), &cfg_.rdp_fps, &cfg_.rdp_password_keep, &rdp_edit_}))
      save();

    ui.space(8);
    activity(ui);
    ui.space(8);
    ui.paragraph("Open the port in your firewall to reach it from other computers (for example: ufw allow 5900/tcp and ufw allow 3389/tcp). "
                 "Guacamole: protocol VNC or RDP, host = the address above, RDP security \"TLS\" (or \"any\"), ignore the certificate.");
    ui.paragraph("The screen shown is the first monitor. Whoever knows the password sees and controls everything you do, so keep it private.");
    if (clients_ > 0) {
      ui.space(4);
      ui.label(std::to_string(clients_) + (clients_ == 1 ? " viewer is connected." : " viewers are connected."));
      ui.newline();
    }
  }

 private:
  struct Server {
    const char* label;
    bool* enabled;
    int* port;
    std::string* password;
    int password_length;
    bool* tls;
    int* quality;
    int* max_kbps;
    std::string* resolution;
    int* depth;
    bool rdp;
    const std::string& status;
    std::string user;
    int* fps;
    bool* keep;         // keep the password when the server is switched off and on
    std::string* edit;  // the password being typed
  };

  // One server's block. Returns true when the configuration changed and must be saved.
  bool server(kit::Ui& ui, const Server& sv) {
    const char* label = sv.label;
    bool* enabled = sv.enabled;
    int* port = sv.port;
    std::string* password = sv.password;
    const int password_length = sv.password_length;
    const std::string& status = sv.status;
    const std::string& user = sv.user;
    bool changed = false;
    ui.row(label);
    bool on = *enabled;
    if (ui.toggle(&on)) {
      *enabled = on;
      changed = true;
      if (!on) {
        if (!*sv.keep) password->clear();  // a switched-off server keeps no secret on disk, unless the password is permanent
      } else {
        try {
          if (!*sv.keep || password->empty()) *password = generate_remote_password(password_length);
        } catch (const std::exception& e) {
          *enabled = false;
          message_ = e.what();
        }
        if (*enabled) message_.clear();
        kit::spawn_detached({"fleetwm-remote"});  // does nothing when it already runs
      }
    }
    ui.newline();
    if (!*enabled) return changed;

    ui.row("Port");
    if (ui.spin(port, 1, 65535, 1)) changed = true;
    ui.newline();
    ui.row("Address");
    std::string text;
    for (const std::string& a : addresses_) text += (text.empty() ? "" : ",  ") + a + ":" + std::to_string(*port);
    ui.label(text.empty() ? "no network address found" : text);
    ui.newline();
    if (!user.empty()) {
      ui.row("User name");
      ui.label(user);
      ui.newline();
    }
    ui.row("Encryption (TLS)");
    if (ui.toggle(sv.tls)) changed = true;
    ui.newline();
    ui.paragraph(password_length == kVncPasswordLength
                     ? "On: the viewer must support VeNCrypt (TigerVNC, RealVNC, Remmina; not every viewer does). Off: the classic VNC login; the picture is not encrypted."
                     : "On: TLS, as Windows and Guacamole expect. Off: standard RDP security without encryption; many clients refuse that.",
                 true);
    ui.row("Quality");
    if (ui.slider(sv.quality, kQualityMin, kQualityMax, 340)) changed = true;
    ui.newline();
    ui.paragraph("90-100 keeps every colour; lower drops colour detail (text and flat colour stay exact, photographs and video are compressed harder), which saves bandwidth.", true);
    ui.row("Frame rate");
    {
      std::vector<std::string> labels;
      for (int f : remote_fps_choices()) labels.push_back(std::to_string(f) + " frames per second");
      int idx = nearest_fps_index(*sv.fps);
      if (ui.dropdown(labels, &idx, 220)) {
        *sv.fps = remote_fps_choices()[static_cast<size_t>(idx)];
        changed = true;
      }
    }
    ui.newline();
    ui.paragraph("The most updates a second a viewer gets; the picture is only sent when something changed. 24 is smooth, 30 is the usual remote desktop rate; 5-15 for a slow link.", true);
    ui.row("Bandwidth limit");
    // A limit under what the frame rate needs at this quality only makes the picture lag, so the slider skips those steps.
    int rw = 1920, rh = 1080;
    parse_resolution(*sv.resolution, &rw, &rh);
    const int floor_kbps = remote_min_kbps(*sv.quality, *sv.fps, rw, rh);
    const auto& choices = remote_bandwidth_choices();
    const int last = static_cast<int>(choices.size()) - 1;
    int lowest = 1;
    while (lowest < last && choices[static_cast<size_t>(lowest)] < floor_kbps) ++lowest;
    int bw = nearest_bandwidth_index(*sv.max_kbps);
    if (bw > 0 && bw < lowest) bw = lowest;
    if (ui.slider(&bw, 0, last, 340, true, remote_bandwidth_label(*sv.max_kbps))) {
      if (bw > 0 && bw < lowest) bw = lowest;
      *sv.max_kbps = choices[static_cast<size_t>(bw)];
      changed = true;
    }
    ui.newline();
    ui.paragraph("Needed for " + std::to_string(*sv.fps) + " frames per second at this quality and size: about " + remote_bandwidth_label(floor_kbps) +
                     " while the picture is moving. A lower limit is raised to it; lower the frame rate, the quality or the render resolution to need less.",
                 true);
    ui.row("Render resolution");
    {
      std::vector<std::string> labels;
      for (const ResolutionChoice& c : remote_resolution_choices()) labels.push_back(c.label);
      int idx = nearest_resolution_index(*sv.resolution);
      if (ui.dropdown(labels, &idx, 220)) {
        *sv.resolution = remote_resolution_choices()[static_cast<size_t>(idx)].value;
        changed = true;
      }
    }
    ui.newline();
    ui.row("Colour depth");
    {
      std::vector<std::string> labels;
      for (const DepthChoice& c : remote_depth_choices(sv.rdp)) labels.push_back(c.label);
      int idx = depth_index(sv.rdp, *sv.depth);
      if (ui.dropdown(labels, &idx, 220)) {
        *sv.depth = remote_depth_choices(sv.rdp)[static_cast<size_t>(idx)].bits;
        changed = true;
      }
    }
    ui.newline();
    ui.paragraph("A smaller render resolution shrinks the picture the viewer gets (never enlarged, proportions kept). Fewer colours compress better. "
                 "RDP reconnects when these change.", true);
    ui.paragraph("For a slow or shared connection: lower the quality and set a limit. A viewer never gets more than the limit, and on a slower link than the limit it adapts by itself.", true);
    ui.row("Password");
    ui.label(*password);
    ui.newline();
    ui.row("");
    if (ui.button("New password")) {
      try {
        *password = generate_remote_password(password_length);
        changed = true;
      } catch (const std::exception& e) {
        message_ = e.what();
      }
    }
    ui.same_line();
    if (ui.button("Copy password")) app_.set_clipboard({{"text/plain;charset=utf-8", *password}, {"text/plain", *password}});
    ui.newline();
    ui.row("Keep this password");
    if (ui.toggle(sv.keep)) changed = true;
    ui.newline();
    ui.paragraph("On: the password stays when the server is switched off and on (or the session restarts), so a saved connection in Apache Guacamole keeps working. "
                 "It is stored as plain text in ~/.config/fleetwm/remote.toml (readable only by you), because the viewer's login needs the password itself.",
                 true);
    ui.row("Set your own");
    ui.text_entry(sv.edit, 220, true, true);
    ui.same_line();
    {
      const size_t min_len = password_length == kVncPasswordLength ? 6 : 12;
      const bool ok = sv.edit->size() >= min_len && sv.edit->size() <= static_cast<size_t>(password_length == kVncPasswordLength ? 8 : 64);
      if (ui.button("Use it", ok)) {
        *password = *sv.edit;
        sv.edit->clear();
        *sv.keep = true;
        changed = true;
      }
    }
    ui.newline();
    ui.paragraph(password_length == kVncPasswordLength ? "6 to 8 characters (the protocol only looks at the first 8). Using your own password keeps it."
                                                       : "12 to 64 characters. Using your own password keeps it.",
                 true);
    if (password_length == kVncPasswordLength) {
      ui.paragraph("A VNC password is only ever 8 characters long; that is a limit of the protocol.", true);
    }
    ui.row("Status");
    ui.label(status.empty() ? "starting..." : status, true);
    ui.newline();
    return changed;
  }

  // What the servers are doing now and the tally: today, and the average per day over 7 and 28 days.
  void activity(kit::Ui& ui) {
    const auto now = std::chrono::steady_clock::now();
    if (now - usage_read_ > std::chrono::seconds(2)) {  // the daemon saves the tally every 30 s and when the last viewer leaves
      usage_ = load_usage();
      usage_read_ = now;
    }
    const RemoteLive live = load_remote_live();
    const time_t t = std::time(nullptr);
    for (int p = 0; p < 2; ++p) {
      ui.section(p == kProtoVnc ? "VNC activity" : "RDP activity");
      const RemoteLiveProto& l = live.valid ? live.proto[p] : RemoteLiveProto{};
      ui.row("Now");
      if (l.clients > 0) {
        ui.label(std::to_string(l.clients) + (l.clients == 1 ? " viewer" : " viewers") + ", up " + remote_format_rate(l.tx_rate) + ", down " +
                 remote_format_rate(l.rx_rate));
      } else {
        ui.label("No viewer connected", true);
      }
      ui.newline();
      if (l.clients > 0) {
        char cpu[32];
        std::snprintf(cpu, sizeof cpu, "%.1f%%", l.cpu_percent);
        ui.row("This run");
        ui.label("Sent " + remote_format_bytes(static_cast<double>(l.tx)) + ", received " + remote_format_bytes(static_cast<double>(l.rx)));
        ui.newline();
        ui.row("CPU and memory");
        ui.label(std::string(cpu) + ", " + remote_format_bytes(static_cast<double>(l.ram)));
        ui.newline();
      }
      const struct {
        const char* label;
        int days;
      } kPeriods[] = {{"Today", 1}, {"7 days, per day", 7}, {"28 days, per day", 28}};
      for (const auto& per : kPeriods) {
        const UsageAverage a = average_usage(usage_, p, per.days, t);
        char cpu[48];
        std::snprintf(cpu, sizeof cpu, "%.0f s", a.cpu_s);
        ui.row(per.label);
        ui.label("Sent " + remote_format_bytes(a.tx) + ", received " + remote_format_bytes(a.rx), true);
        ui.newline();
        ui.row("");
        ui.label("Connected " + remote_format_duration(static_cast<uint64_t>(a.active_s)) + ", CPU " + cpu, true);
        ui.newline();
      }
      const DayUsage& all = usage_.all_time[static_cast<size_t>(p)];
      ui.row("All time");
      ui.label("Sent " + remote_format_bytes(static_cast<double>(all.tx)) + ", received " + remote_format_bytes(static_cast<double>(all.rx)), true);
      ui.newline();
      ui.space(4);
    }
    ui.paragraph("Sent is the picture going to viewers, received is their keys and mouse. The bar shows the protocol while someone is connected; "
                 "hover it for the same numbers.", true);
  }

  void save() {
    try {
      save_remote_config(cfg_);
      if (cfg_.vnc_enabled || cfg_.rdp_enabled) kit::spawn_detached({"fleetwm-remote"});
      message_.clear();
    } catch (const std::exception& e) {
      message_ = std::string("Could not save: ") + e.what();
    }
  }

  void read_status() {
    std::ifstream in(remote_status_path());
    vnc_status_.clear();
    rdp_status_.clear();
    clients_ = 0;
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
      any = true;
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
      if (key == "clients") clients_ = std::atoi(val.c_str());
      else if (key == "vnc") vnc_status_ = describe(val);
      else if (key == "rdp") rdp_status_ = describe(val);
    }
    if (!any) vnc_status_ = rdp_status_ = "";
  }

  static std::string describe(const std::string& v) {
    if (v == "off") return "Off";
    if (v.rfind("listening:", 0) == 0) return "Running, waiting for a viewer on port " + v.substr(10);
    if (v.rfind("error:", 0) == 0) return "Not running: " + v.substr(6);
    return v;
  }

  static std::vector<std::string> local_addresses() {
    std::vector<std::string> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
      if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP)) continue;
      char buf[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
      out.push_back(buf);
    }
    freeifaddrs(list);
    return out;
  }

  kit::App& app_;
  std::function<void()> redraw_;
  RemoteConfig cfg_;
  std::string message_, vnc_status_, rdp_status_, vnc_edit_, rdp_edit_;
  std::vector<std::string> addresses_;
  int clients_ = 0;
  UsageHistory usage_;
  std::chrono::steady_clock::time_point usage_read_{};
  unsigned frames_ = 0, last_frames_ = 0;
  int timer_ = 0;
};

}  // namespace fleetwm
