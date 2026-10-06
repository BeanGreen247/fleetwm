#include "tray.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "icon_theme.hpp"

extern "C" const sd_bus_vtable fleetwm_watcher_vtable[];

namespace fleetwm::bar {

namespace {
constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
constexpr const char* kWatcherIface = "org.kde.StatusNotifierWatcher";
constexpr const char* kItemIface = "org.kde.StatusNotifierItem";
constexpr int kIconPx = 32;  // rasterized at 2x the 16px draw size

// Neutral placeholder for items that supply no usable icon.
cairo_surface_t* make_placeholder() {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kIconPx, kIconPx);
  cairo_t* cr = cairo_create(s);
  cairo_set_source_rgba(cr, 0.65, 0.68, 0.78, 1.0);
  cairo_set_line_width(cr, 3);
  cairo_rectangle(cr, 6, 6, 20, 20);
  cairo_stroke(cr);
  cairo_destroy(cr);
  return s;
}
}  // namespace

struct Tray::Impl {
  struct Entry {
    Item pub;
    std::vector<std::string> theme_dirs;
    sd_bus_slot* sig_slot = nullptr;
    sd_bus_slot* props_slot = nullptr;
    sd_bus_slot* call_slot = nullptr;
    Impl* owner = nullptr;
  };

  kit::App& app;
  std::function<void()> on_change;
  sd_bus* bus = nullptr;
  sd_bus_slot* vtable_slot = nullptr;
  sd_bus_slot* owner_slot = nullptr;
  int watch_id = 0;
  std::vector<std::unique_ptr<Entry>> entries;
  std::vector<Item> view;
  cairo_surface_t* placeholder = nullptr;

  Impl(kit::App& a, std::function<void()> cb) : app(a), on_change(std::move(cb)) {}

  ~Impl() {
    for (TooltipRequest* r : tooltip_requests) {
      if (r->slot) sd_bus_slot_unref(r->slot);
      delete r;
    }
    if (watch_id) app.unwatch(watch_id);
    for (auto& e : entries) free_entry(*e);
    entries.clear();
    if (vtable_slot) sd_bus_slot_unref(vtable_slot);
    if (owner_slot) sd_bus_slot_unref(owner_slot);
    if (bus) sd_bus_flush_close_unref(bus);
    if (placeholder) cairo_surface_destroy(placeholder);
  }

  void free_entry(Entry& e) {
    if (e.sig_slot) sd_bus_slot_unref(e.sig_slot);
    if (e.props_slot) sd_bus_slot_unref(e.props_slot);
    if (e.call_slot) sd_bus_slot_unref(e.call_slot);
    e.sig_slot = e.props_slot = e.call_slot = nullptr;
    if (e.pub.icon) cairo_surface_destroy(e.pub.icon);
    e.pub.icon = nullptr;
  }

  void rebuild_view() {
    view.clear();
    for (auto& e : entries) view.push_back(e->pub);
  }

  void changed() {
    rebuild_view();
    if (on_change) on_change();
  }

  void process() {
    if (!bus) return;
    int r;
    do {
      r = sd_bus_process(bus, nullptr);
    } while (r > 0);
    if (r < 0) {
      std::fprintf(stderr, "fleetwm-bar: systray: bus error: %s\n", std::strerror(-r));
      app.unwatch(watch_id);
      watch_id = 0;
    }
  }

  void start() {
    if (bus) return;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
      std::fprintf(stderr, "fleetwm-bar: systray: cannot connect to the session bus: %s\n",
                   std::strerror(-r));
      bus = nullptr;
      return;
    }
    r = sd_bus_add_object_vtable(bus, &vtable_slot, kWatcherPath, kWatcherIface,
                                 fleetwm_watcher_vtable, this);
    if (r < 0) {
      std::fprintf(stderr, "fleetwm-bar: systray: failed to export watcher object: %s\n",
                   std::strerror(-r));
      return;
    }
    r = sd_bus_request_name(bus, kWatcherIface, 0);
    if (r < 0) {
      std::fprintf(stderr,
                   "fleetwm-bar: systray: could not own org.kde.StatusNotifierWatcher (already "
                   "taken?) -- tray icons disabled this session\n");
      return;
    }
    // Detect tray apps vanishing from the bus.
    sd_bus_add_match(
        bus, &owner_slot,
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged'",
        [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
          const char *name = nullptr, *old_owner = nullptr, *new_owner = nullptr;
          if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) >= 0 && name &&
              new_owner && new_owner[0] == '\0')
            static_cast<Impl*>(ud)->unregister_item(name);
          return 0;
        },
        this);
    watch_id = app.watch_fd(sd_bus_get_fd(bus), [this] { process(); });
    sd_bus_emit_signal(bus, kWatcherPath, kWatcherIface, "StatusNotifierHostRegistered", "");
    process();
  }

  void register_item(const std::string& bus_name, const std::string& path) {
    for (auto& e : entries)
      if (e->pub.bus_name == bus_name && e->pub.object_path == path) return;  // some apps register twice
    auto e = std::make_unique<Entry>();
    e->owner = this;
    e->pub.bus_name = bus_name;
    e->pub.object_path = path;
    Entry* raw = e.get();
    entries.push_back(std::move(e));

    // Any signal from the item (NewIcon, NewTitle, ...) or a PropertiesChanged
    // => re-read its properties.
    const std::string match1 = "type='signal',sender='" + bus_name + "',path='" + path +
                               "',interface='" + kItemIface + "'";
    const std::string match2 = "type='signal',sender='" + bus_name + "',path='" + path +
                               "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'";
    auto cb = [](sd_bus_message*, void* ud, sd_bus_error*) -> int {
      auto* en = static_cast<Entry*>(ud);
      en->owner->fetch(*en);
      return 0;
    };
    sd_bus_add_match(bus, &raw->sig_slot, match1.c_str(), cb, raw);
    sd_bus_add_match(bus, &raw->props_slot, match2.c_str(), cb, raw);
    fetch(*raw);

    sd_bus_emit_signal(bus, kWatcherPath, kWatcherIface, "StatusNotifierItemRegistered", "s",
                       bus_name.c_str());
    sd_bus_flush(bus);
  }

  void unregister_item(const std::string& bus_name) {
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const auto& e) { return e->pub.bus_name == bus_name; });
    if (it == entries.end()) return;
    free_entry(**it);
    entries.erase(it);
    sd_bus_emit_signal(bus, kWatcherPath, kWatcherIface, "StatusNotifierItemUnregistered", "s",
                       bus_name.c_str());
    changed();
  }

  void fetch(Entry& e) {
    if (e.call_slot) {  // a previous fetch is still in flight; replace it
      sd_bus_slot_unref(e.call_slot);
      e.call_slot = nullptr;
    }
    const int r = sd_bus_call_method_async(
        bus, &e.call_slot, e.pub.bus_name.c_str(), e.pub.object_path.c_str(),
        "org.freedesktop.DBus.Properties", "GetAll",
        [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
          auto* en = static_cast<Entry*>(ud);
          en->owner->on_properties(*en, m);
          return 0;
        },
        &e, "s", kItemIface);
    if (r < 0)
      std::fprintf(stderr, "fleetwm-bar: systray: GetAll failed for %s: %s\n",
                   e.pub.bus_name.c_str(), std::strerror(-r));
    sd_bus_flush(bus);
  }

  static void argb_to_surface(const unsigned char* data, int w, int h, cairo_surface_t** out) {
    // StatusNotifierItem pixmaps are ARGB32 in network byte order (A,R,G,B).
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    unsigned char* dst = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    cairo_surface_flush(s);
    for (int y = 0; y < h; ++y) {
      auto* row = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * stride);
      for (int x = 0; x < w; ++x) {
        const unsigned char* p = data + (static_cast<size_t>(y) * w + x) * 4;
        const unsigned a = p[0];
        row[x] = (a << 24) | (((p[1] * a + 127) / 255) << 16) | (((p[2] * a + 127) / 255) << 8) |
                 ((p[3] * a + 127) / 255);
      }
    }
    cairo_surface_mark_dirty(s);
    *out = s;
  }

  void on_properties(Entry& e, sd_bus_message* m) {
    e.call_slot = nullptr;  // the slot is released once the reply is delivered
    if (sd_bus_message_is_method_error(m, nullptr)) return;
    std::string icon_name, theme_path, title;
    int best_w = 0, best_h = 0;
    std::vector<unsigned char> best;

    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
      const char* key = nullptr;
      if (sd_bus_message_read(m, "s", &key) < 0 || !key) {
        sd_bus_message_exit_container(m);
        break;
      }
      auto read_string = [&](std::string& dst) {
        if (sd_bus_message_enter_container(m, 'v', "s") < 0) return false;
        const char* s = nullptr;
        if (sd_bus_message_read(m, "s", &s) >= 0 && s) dst = s;
        sd_bus_message_exit_container(m);
        return true;
      };
      bool handled = false;
      if (!std::strcmp(key, "IconName")) handled = read_string(icon_name);
      else if (!std::strcmp(key, "IconThemePath")) handled = read_string(theme_path);
      else if (!std::strcmp(key, "Title")) handled = read_string(title);
      else if (!std::strcmp(key, "IconPixmap")) {
        if (sd_bus_message_enter_container(m, 'v', "a(iiay)") >= 0) {
          if (sd_bus_message_enter_container(m, 'a', "(iiay)") >= 0) {
            while (sd_bus_message_enter_container(m, 'r', "iiay") > 0) {
              int w = 0, h = 0;
              const void* bytes = nullptr;
              size_t len = 0;
              if (sd_bus_message_read(m, "ii", &w, &h) >= 0 &&
                  sd_bus_message_read_array(m, 'y', &bytes, &len) >= 0 && w > 0 && h > 0 &&
                  len >= static_cast<size_t>(w) * h * 4 &&
                  static_cast<long long>(w) * h > static_cast<long long>(best_w) * best_h) {
                best_w = w;
                best_h = h;
                best.assign(static_cast<const unsigned char*>(bytes),
                            static_cast<const unsigned char*>(bytes) + static_cast<size_t>(w) * h * 4);
              }
              sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
          }
          sd_bus_message_exit_container(m);
          handled = true;
        }
      }
      if (!handled) sd_bus_message_skip(m, "v");
      sd_bus_message_exit_container(m);
    }

    cairo_surface_t* icon = nullptr;
    if (!best.empty()) argb_to_surface(best.data(), best_w, best_h, &icon);
    if (!icon && !icon_name.empty()) {
      std::vector<std::string> dirs;
      if (!theme_path.empty()) dirs.push_back(theme_path);
      icon = kit::load_icon(icon_name, kIconPx, dirs);
    }
    if (!icon) {
      if (!placeholder) placeholder = make_placeholder();
      icon = cairo_surface_reference(placeholder);
    }
    if (e.pub.icon) cairo_surface_destroy(e.pub.icon);
    e.pub.icon = icon;
    e.pub.title = title;
    changed();
  }

  void click(size_t index, uint32_t button, int x, int y) {
    if (index >= entries.size() || !bus) return;
    const Entry& e = *entries[index];
    const char* method = button == 0x111 ? "ContextMenu" : "Activate";
    sd_bus_call_method_async(bus, nullptr, e.pub.bus_name.c_str(), e.pub.object_path.c_str(),
                             kItemIface, method, nullptr, nullptr, "ii", x, y);
    sd_bus_flush(bus);
  }

  struct TooltipRequest {
    std::function<void(std::string)> done;
    sd_bus_slot* slot = nullptr;
    Impl* owner = nullptr;
  };
  std::vector<TooltipRequest*> tooltip_requests;

  void tooltip(size_t index, std::function<void(std::string)> done) {
    if (index >= entries.size() || !bus) return done("");
    const Entry& e = *entries[index];
    auto* req = new TooltipRequest{std::move(done), nullptr, this};
    tooltip_requests.push_back(req);
    const int r = sd_bus_call_method_async(
        bus, &req->slot, e.pub.bus_name.c_str(), e.pub.object_path.c_str(),
        "org.freedesktop.DBus.Properties", "Get",
        [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
          auto* rq = static_cast<TooltipRequest*>(ud);
          std::string text;
          // ToolTip is (sa(iiay)ss): icon name, icon pixmaps, title, description.
          if (!sd_bus_message_is_method_error(m, nullptr) &&
              sd_bus_message_enter_container(m, 'v', "(sa(iiay)ss)") >= 0 &&
              sd_bus_message_enter_container(m, 'r', "sa(iiay)ss") >= 0) {
            const char *icon = nullptr, *title = nullptr, *desc = nullptr;
            sd_bus_message_read(m, "s", &icon);
            sd_bus_message_skip(m, "a(iiay)");
            if (sd_bus_message_read(m, "ss", &title, &desc) >= 0) {
              text = title ? title : "";
              if (desc && *desc) text += (text.empty() ? "" : "\n") + std::string(desc);
            }
          }
          Impl* owner = rq->owner;
          auto done = std::move(rq->done);
          owner->tooltip_requests.erase(
              std::remove(owner->tooltip_requests.begin(), owner->tooltip_requests.end(), rq),
              owner->tooltip_requests.end());
          sd_bus_slot_unref(rq->slot);
          delete rq;
          done(text);
          return 0;
        },
        req, "ss", kItemIface, "ToolTip");
    if (r < 0) {
      tooltip_requests.pop_back();
      auto d = std::move(req->done);
      delete req;
      d("");
      return;
    }
    sd_bus_flush(bus);
  }
};

// ---- extern "C" handlers referenced by tray_vtable.c ----
extern "C" {

int fleetwm_tray_register_item(sd_bus_message* m, void* ud, sd_bus_error*) {
  auto* impl = static_cast<Tray::Impl*>(ud);
  const char* s = nullptr;
  if (sd_bus_message_read(m, "s", &s) < 0) return -EINVAL;
  const char* sender = sd_bus_message_get_sender(m);
  if (sender) impl->register_item(sender, (s && s[0] == '/') ? s : "/StatusNotifierItem");
  return sd_bus_reply_method_return(m, nullptr);
}

int fleetwm_tray_register_host(sd_bus_message* m, void*, sd_bus_error*) {
  return sd_bus_reply_method_return(m, nullptr);
}

int fleetwm_tray_prop_items(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                            void* ud, sd_bus_error*) {
  auto* impl = static_cast<Tray::Impl*>(ud);
  int r = sd_bus_message_open_container(reply, 'a', "s");
  if (r < 0) return r;
  for (auto& e : impl->entries) {
    r = sd_bus_message_append(reply, "s", e->pub.bus_name.c_str());
    if (r < 0) return r;
  }
  return sd_bus_message_close_container(reply);
}

int fleetwm_tray_prop_host(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                           void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", 1);
}

int fleetwm_tray_prop_version(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                              void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "i", 0);
}

}  // extern "C"

Tray::Tray(kit::App& app, std::function<void()> on_change)
    : impl_(new Impl(app, std::move(on_change))) {}
Tray::~Tray() = default;
void Tray::start() { impl_->start(); }
const std::vector<Tray::Item>& Tray::items() const { return impl_->view; }
void Tray::click(size_t index, uint32_t button, int x, int y) { impl_->click(index, button, x, y); }
void Tray::tooltip(size_t index, std::function<void(std::string)> done) { impl_->tooltip(index, std::move(done)); }

}  // namespace fleetwm::bar
