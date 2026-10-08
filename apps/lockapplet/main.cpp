// fleetwm-lockapplet: a padlock in the system tray that keeps the screen on and the computer
// awake when clicked (the padlock is crossed out while that is on), and shows on hover what
// is switched off and which programs are keeping the computer awake.
//
// How it works:
//  - It is a StatusNotifierItem (the same tray protocol the bar hosts), so the bar draws it.
//  - Keep-awake means two things for as long as it is on: a "keep awake" request to the
//    compositor (IDLE_INHIBIT on its control socket, which stops the screen-off and sleep
//    timers) and a systemd-logind "idle:sleep" block inhibitor (so nothing else suspends
//    the machine either). Both disappear by themselves if this program exits.
//  - It also provides org.freedesktop.ScreenSaver, the interface browsers and video players
//    call to say "a video is playing"; while any program holds such a request the computer
//    is kept awake too, and the program is named in the hover text.
//  - The hover text is built on demand (when the bar asks for the ToolTip property): the
//    compositor's idle-inhibit clients, logind's inhibitor list and the ScreenSaver holders.

#include <systemd/sd-bus.h>

#include <cairo.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "battery_reading.hpp"
#include "ipc_client.hpp"
#include "lock_tooltip.hpp"
#include "power_config.hpp"
#include "version.hpp"

extern "C" {
extern const sd_bus_vtable lock_sni_vtable[];
extern const sd_bus_vtable lock_screensaver_vtable[];
}

namespace {

using fleetwm::LockHolder;

constexpr const char* kItemPath = "/StatusNotifierItem";
constexpr const char* kItemIface = "org.kde.StatusNotifierItem";
constexpr const char* kWatcherName = "org.kde.StatusNotifierWatcher";
constexpr const char* kInhibitWho = "Fleetwm lock applet";
constexpr int kIconPx = 32;
constexpr int kReconnectMs = 5000;

struct ScreenSaverHolder {
  std::string sender;  // unique bus name of the program, to release its requests when it exits
  LockHolder holder;
};

struct Applet {
  sd_bus* user_bus = nullptr;
  sd_bus* system_bus = nullptr;
  bool keep_awake = false;  // switched on from the padlock
  int logind_fd = -1;       // the logind inhibitor, held while anything wants the machine awake
  fleetwm::IpcClient inhibit_ipc;  // its open connection is what keeps the compositor inhibited
  bool ipc_inhibiting = false;
  std::map<uint32_t, ScreenSaverHolder> screensaver;
  uint32_t next_cookie = 1;

  // ---------------------------------------------------------------- icon --
  std::vector<unsigned char> icon_argb(bool crossed) const {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kIconPx, kIconPx);
    cairo_t* cr = cairo_create(s);
    const double size = kIconPx;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);

    const double body_w = size * 0.56, body_h = size * 0.42;
    const double body_x = (size - body_w) / 2, body_y = size * 0.46;
    cairo_set_line_width(cr, std::max(1.5, size * 0.09));
    cairo_arc(cr, size / 2, body_y, body_w * 0.42, M_PI, 0);  // shackle
    cairo_stroke(cr);

    const double r = size * 0.06;  // body: rounded rectangle
    cairo_new_sub_path(cr);
    cairo_arc(cr, body_x + body_w - r, body_y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, body_x + body_w - r, body_y + body_h - r, r, 0, M_PI / 2);
    cairo_arc(cr, body_x + r, body_y + body_h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, body_x + r, body_y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);  // keyhole
    cairo_arc(cr, size / 2, body_y + body_h * 0.42, size * 0.05, 0, 2 * M_PI);
    cairo_fill(cr);

    if (crossed) {
      cairo_set_line_width(cr, std::max(2.0, size * 0.11));
      cairo_set_source_rgb(cr, 0.90, 0.25, 0.20);
      const double m = size * 0.08;
      cairo_move_to(cr, size - m, m);
      cairo_line_to(cr, m, size - m);
      cairo_stroke(cr);
    }
    cairo_destroy(cr);
    cairo_surface_flush(s);

    // StatusNotifierItem pixmaps are ARGB32 in network byte order (A, R, G, B), not premultiplied.
    std::vector<unsigned char> out(static_cast<size_t>(kIconPx) * kIconPx * 4);
    const unsigned char* src = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < kIconPx; ++y)
      for (int x = 0; x < kIconPx; ++x) {
        const uint32_t px = *reinterpret_cast<const uint32_t*>(src + static_cast<size_t>(y) * stride + x * 4);
        const unsigned a = px >> 24;
        auto un = [a](unsigned c) { return a ? std::min(255u, (c * 255 + a / 2) / a) : 0u; };
        unsigned char* d = &out[(static_cast<size_t>(y) * kIconPx + x) * 4];
        d[0] = a;
        d[1] = un((px >> 16) & 0xff);
        d[2] = un((px >> 8) & 0xff);
        d[3] = un(px & 0xff);
      }
    cairo_surface_destroy(s);
    return out;
  }

  // ------------------------------------------------------- who holds what --
  std::vector<LockHolder> compositor_holders() const {
    std::vector<LockHolder> out;
    fleetwm::IpcClient query;
    if (!query.connect() || !query.send_command("IDLE_INHIBITORS?")) return out;
    bool done = false;
    for (int waited = 0; !done && waited < 500; waited += 50) {
      pollfd p{query.fd(), POLLIN, 0};
      if (poll(&p, 1, 50) <= 0) continue;
      query.poll_lines([&](const std::string& line) {
        if (line == "END") done = true;
        // INHIBITOR wayland <pid> <program>
        else if (line.rfind("INHIBITOR wayland ", 0) == 0) {
          const size_t sp = line.find(' ', 18);
          if (sp != std::string::npos) out.push_back({line.substr(sp + 1), "screen kept on"});
        }
      });
      if (!query.is_connected()) break;
    }
    return out;
  }

  std::vector<LockHolder> logind_holders() const {
    std::vector<LockHolder> out;
    if (!system_bus) return out;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(system_bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager", "ListInhibitors", nullptr, &reply, "") < 0)
      return out;
    if (sd_bus_message_enter_container(reply, 'a', "(ssssuu)") >= 0) {
      const char *what, *who, *why, *mode;
      uint32_t uid, pid;
      while (sd_bus_message_read(reply, "(ssssuu)", &what, &who, &why, &mode, &uid, &pid) > 0) {
        const std::string w = what, m = mode, name = who;
        if (m != "block" || name == kInhibitWho) continue;
        if (w.find("idle") == std::string::npos && w.find("sleep") == std::string::npos) continue;
        out.push_back({name, why});
      }
      sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return out;
  }

  std::vector<LockHolder> holders() const {
    std::vector<LockHolder> all;
    for (const auto& [cookie, h] : screensaver) all.push_back(h.holder);
    for (LockHolder& h : compositor_holders()) all.push_back(std::move(h));
    for (LockHolder& h : logind_holders()) all.push_back(std::move(h));
    return fleetwm::unique_holders(std::move(all));
  }

  fleetwm::LockTooltip tooltip() const {
    const bool battery = !fleetwm::find_battery_dir().empty() && !fleetwm::ac_online();
    return fleetwm::describe_lock_state(keep_awake, holders(), fleetwm::load_power_config(), battery);
  }

  // ------------------------------------------------------- doing the work --
  bool want_awake() const { return keep_awake || !screensaver.empty(); }

  void apply() {
    if (want_awake()) {
      if (logind_fd < 0) take_logind_inhibitor();
      if (!inhibit_ipc.is_connected()) inhibit_ipc.connect();
      if (inhibit_ipc.is_connected() && !ipc_inhibiting)
        ipc_inhibiting = inhibit_ipc.send_command("IDLE_INHIBIT 1");
    } else {
      if (logind_fd >= 0) {
        close(logind_fd);
        logind_fd = -1;
      }
      if (inhibit_ipc.is_connected()) inhibit_ipc.send_command("IDLE_INHIBIT 0");
      ipc_inhibiting = false;
    }
  }

  void take_logind_inhibitor() {
    if (!system_bus) return;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(system_bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager", "Inhibit", nullptr, &reply, "ssss",
                           "idle:sleep", kInhibitWho, "Keep awake is on", "block") < 0)
      return;
    int fd = -1;
    if (sd_bus_message_read(reply, "h", &fd) >= 0 && fd >= 0) logind_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    sd_bus_message_unref(reply);
  }

  void toggle() {
    keep_awake = !keep_awake;
    apply();
    sd_bus_emit_signal(user_bus, kItemPath, kItemIface, "NewIcon", "");
    sd_bus_emit_signal(user_bus, kItemPath, kItemIface, "NewToolTip", "");
  }

  void register_with_tray() {
    sd_bus_call_method(user_bus, kWatcherName, "/StatusNotifierWatcher", kWatcherName,
                       "RegisterStatusNotifierItem", nullptr, nullptr, "s", kItemPath);
  }

  void drop_screensaver_holders_of(const std::string& sender) {
    const size_t before = screensaver.size();
    for (auto it = screensaver.begin(); it != screensaver.end();)
      it = it->second.sender == sender ? screensaver.erase(it) : std::next(it);
    if (screensaver.size() != before) apply();
  }
};

Applet* g_applet = nullptr;

int append_icon(sd_bus_message* reply, bool crossed) {
  const std::vector<unsigned char> px = g_applet->icon_argb(crossed);
  int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'r', "iiay");
  if (r < 0) return r;
  if ((r = sd_bus_message_append(reply, "ii", kIconPx, kIconPx)) < 0) return r;
  if ((r = sd_bus_message_append_array(reply, 'y', px.data(), px.size())) < 0) return r;
  if ((r = sd_bus_message_close_container(reply)) < 0) return r;
  return sd_bus_message_close_container(reply);
}

}  // namespace

extern "C" {

int lock_sni_get(sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void*,
                 sd_bus_error*) {
  const std::string p = property;
  if (p == "Category") return sd_bus_message_append(reply, "s", "SystemServices");
  if (p == "Id") return sd_bus_message_append(reply, "s", "fleetwm-lockapplet");
  if (p == "Title") return sd_bus_message_append(reply, "s", "Keep awake");
  if (p == "Status") return sd_bus_message_append(reply, "s", "Active");
  if (p == "WindowId") return sd_bus_message_append(reply, "u", 0u);
  if (p == "IconName") return sd_bus_message_append(reply, "s", "");
  if (p == "ItemIsMenu") return sd_bus_message_append(reply, "b", 0);
  if (p == "IconPixmap") return append_icon(reply, g_applet->keep_awake);
  if (p == "ToolTip") {
    const fleetwm::LockTooltip t = g_applet->tooltip();
    int r = sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
    if (r < 0) return r;
    if ((r = sd_bus_message_append(reply, "s", "")) < 0) return r;  // no icon name
    if ((r = sd_bus_message_open_container(reply, 'a', "(iiay)")) < 0) return r;
    if ((r = sd_bus_message_close_container(reply)) < 0) return r;
    if ((r = sd_bus_message_append(reply, "ss", t.title.c_str(), t.body.c_str())) < 0) return r;
    return sd_bus_message_close_container(reply);
  }
  return -ENOENT;
}

int lock_sni_activate(sd_bus_message* m, void*, sd_bus_error*) {
  g_applet->toggle();
  return sd_bus_reply_method_return(m, nullptr);
}

int lock_sni_ignore(sd_bus_message* m, void*, sd_bus_error*) { return sd_bus_reply_method_return(m, nullptr); }

int lock_ss_inhibit(sd_bus_message* m, void*, sd_bus_error*) {
  const char *app = nullptr, *reason = nullptr;
  if (sd_bus_message_read(m, "ss", &app, &reason) < 0) return -EINVAL;
  const char* sender = sd_bus_message_get_sender(m);
  const uint32_t cookie = g_applet->next_cookie++;
  g_applet->screensaver[cookie] = {sender ? sender : "", {app && *app ? app : "unknown program", reason ? reason : ""}};
  g_applet->apply();
  return sd_bus_reply_method_return(m, "u", cookie);
}

int lock_ss_uninhibit(sd_bus_message* m, void*, sd_bus_error*) {
  uint32_t cookie = 0;
  if (sd_bus_message_read(m, "u", &cookie) < 0) return -EINVAL;
  g_applet->screensaver.erase(cookie);
  g_applet->apply();
  return sd_bus_reply_method_return(m, nullptr);
}

int lock_ss_get_active(sd_bus_message* m, void*, sd_bus_error*) { return sd_bus_reply_method_return(m, "b", 0); }

}  // extern "C"

int main(int argc, char** argv) {
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-lockapplet", "")) return 0;
  Applet applet;
  g_applet = &applet;

  if (sd_bus_open_user(&applet.user_bus) < 0) {
    std::fprintf(stderr, "fleetwm-lockapplet: cannot connect to the session bus\n");
    return 1;
  }
  sd_bus_open_system(&applet.system_bus);  // only needed for sleep blocking; the applet works without it

  if (sd_bus_request_name(applet.user_bus, "org.fleetwm.LockApplet", 0) < 0) return 0;  // already running
  sd_bus_add_object_vtable(applet.user_bus, nullptr, kItemPath, kItemIface, lock_sni_vtable, nullptr);
  for (const char* path : {"/org/freedesktop/ScreenSaver", "/ScreenSaver"})
    sd_bus_add_object_vtable(applet.user_bus, nullptr, path, "org.freedesktop.ScreenSaver", lock_screensaver_vtable,
                             nullptr);
  if (sd_bus_request_name(applet.user_bus, "org.freedesktop.ScreenSaver", 0) < 0)
    std::fprintf(stderr, "fleetwm-lockapplet: another program provides org.freedesktop.ScreenSaver; "
                         "browser and video-player requests will not be listed\n");

  // A program leaving the bus drops its ScreenSaver requests; the tray host appearing (the bar
  // starting after us, or restarting) means we must register again.
  sd_bus_add_match(
      applet.user_bus, nullptr,
      "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
      [](sd_bus_message* m, void*, sd_bus_error*) -> int {
        const char *name = nullptr, *old_owner = nullptr, *new_owner = nullptr;
        if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0) return 0;
        if (std::strcmp(name, kWatcherName) == 0 && new_owner[0]) g_applet->register_with_tray();
        else if (new_owner[0] == '\0' && old_owner[0]) g_applet->drop_screensaver_holders_of(old_owner);
        return 0;
      },
      nullptr);

  applet.register_with_tray();

  for (;;) {
    int r;
    while ((r = sd_bus_process(applet.user_bus, nullptr)) > 0) {}
    if (r < 0) return 1;

    uint64_t bus_timeout = UINT64_MAX;
    sd_bus_get_timeout(applet.user_bus, &bus_timeout);
    pollfd fds[2] = {{sd_bus_get_fd(applet.user_bus), static_cast<short>(sd_bus_get_events(applet.user_bus)), 0},
                     {applet.inhibit_ipc.is_connected() ? applet.inhibit_ipc.fd() : -1, POLLIN, 0}};
    int timeout_ms = -1;
    if (bus_timeout != UINT64_MAX) timeout_ms = static_cast<int>(std::min<uint64_t>(bus_timeout / 1000 + 1, 60000));
    // While we should be holding the compositor inhibit but the connection is gone (compositor
    // restarted), look again regularly.
    if (applet.want_awake() && !applet.inhibit_ipc.is_connected())
      timeout_ms = timeout_ms < 0 ? kReconnectMs : std::min(timeout_ms, kReconnectMs);
    poll(fds, 2, timeout_ms);

    if (fds[1].revents) {  // the compositor talks to every client; read and ignore, notice a hangup
      applet.inhibit_ipc.poll_lines([](const std::string&) {});
      if (!applet.inhibit_ipc.is_connected()) applet.ipc_inhibiting = false;
    }
    if (applet.want_awake() && !applet.ipc_inhibiting) applet.apply();
  }
}
