#include "tray.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "icon_theme.hpp"
#include "xembed_tray.hpp"

#include <cctype>
#include <fstream>

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

namespace {
// One node of a com.canonical.dbusmenu layout: (ia{sv}av).
struct MenuNode {
  int id = 0;
  std::string label, type, toggle_type;
  int toggle_state = -1;
  bool enabled = true, visible = true;
  std::vector<MenuNode> kids;
};

int read_menu_node(sd_bus_message* m, MenuNode* n, int depth) {
  if (depth > 6) return -1;
  int r = sd_bus_message_enter_container(m, 'r', "ia{sv}av");
  if (r < 0) return r;
  r = sd_bus_message_read(m, "i", &n->id);
  if (r < 0) return r;
  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;
  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char* key = nullptr;
    if (sd_bus_message_read(m, "s", &key) < 0 || !key) return -1;
    char vt = 0;
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, &vt, &contents) < 0 || vt != 'v' || !contents) return -1;
    const std::string sig = contents;
    if (sd_bus_message_enter_container(m, 'v', sig.c_str()) < 0) return -1;
    auto str = [&](std::string& dst) {
      const char* v = nullptr;
      if (sig == "s" && sd_bus_message_read(m, "s", &v) >= 0 && v) dst = v;
      else sd_bus_message_skip(m, sig.c_str());
    };
    if (!std::strcmp(key, "label")) str(n->label);
    else if (!std::strcmp(key, "type")) str(n->type);
    else if (!std::strcmp(key, "toggle-type")) str(n->toggle_type);
    else if (!std::strcmp(key, "enabled") || !std::strcmp(key, "visible")) {
      int b = 1;
      if (sig == "b" && sd_bus_message_read(m, "b", &b) >= 0) (key[0] == 'e' ? n->enabled : n->visible) = b != 0;
      else sd_bus_message_skip(m, sig.c_str());
    } else if (!std::strcmp(key, "toggle-state")) {
      int v = -1;
      if (sig == "i" && sd_bus_message_read(m, "i", &v) >= 0) n->toggle_state = v;
      else sd_bus_message_skip(m, sig.c_str());
    } else sd_bus_message_skip(m, sig.c_str());
    sd_bus_message_exit_container(m);  // v
    sd_bus_message_exit_container(m);  // e
  }
  if (r < 0) return r;
  sd_bus_message_exit_container(m);  // a{sv}
  r = sd_bus_message_enter_container(m, 'a', "v");
  if (r < 0) return r;
  while ((r = sd_bus_message_enter_container(m, 'v', "(ia{sv}av)")) > 0) {
    MenuNode kid;
    if (read_menu_node(m, &kid, depth + 1) < 0) return -1;
    n->kids.push_back(std::move(kid));
    sd_bus_message_exit_container(m);  // v
  }
  if (r < 0) return r;
  sd_bus_message_exit_container(m);  // a v
  sd_bus_message_exit_container(m);  // struct
  return 0;
}

// dbusmenu labels mark the accelerator with an underscore ("_Store"); "__" is a literal one.
std::string clean_menu_label(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '_') {
      if (i + 1 < in.size() && in[i + 1] == '_') out += '_', ++i;
      continue;
    }
    out += in[i] == '|' ? '/' : in[i];  // '|' separates the fields of a ctxmenu item
  }
  return out;
}

// The clickable entries of a node's children in order; submenus are flattened ("Friends > Online").
void flatten_menu(const MenuNode& node, const std::string& prefix, std::vector<Tray::MenuEntry>* out) {
  for (const MenuNode& k : node.kids) {
    if (!k.visible) continue;
    if (k.type == "separator") {
      if (!out->empty() && !out->back().separator) out->push_back({k.id, "", true});
      continue;
    }
    std::string label = clean_menu_label(k.label);
    if (!k.kids.empty()) {
      flatten_menu(k, prefix + label + " > ", out);
      continue;
    }
    if (!k.enabled || label.empty()) continue;
    if (k.toggle_type == "checkmark" || k.toggle_type == "radio") label = (k.toggle_state == 1 ? "[x] " : "[ ] ") + label;
    out->push_back({k.id, prefix + label, false});
  }
  while (!out->empty() && out->back().separator) out->pop_back();
}
}  // namespace

struct Tray::Impl {
  struct Entry {
    std::string menu_path;  // the item's dbusmenu object, "" when it has none
    Item pub;
    std::string proc;  // name of the program that owns the item (its /proc comm, lower case), read once
    bool proc_known = false;
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
  std::vector<size_t> xembed_map;  // for a view item past the StatusNotifierItems: the index of its icon in the XEmbed tray
  cairo_surface_t* placeholder = nullptr;
  int icon_px = 22;
  std::unique_ptr<XembedTray> xembed;  // Wine / old-style tray icons, appended after the StatusNotifierItems

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

  // The program behind a StatusNotifierItem, as /proc names it.
  std::string item_program(Entry& e) {
    if (e.proc_known) return e.proc;
    e.proc_known = true;
    sd_bus_creds* creds = nullptr;
    if (bus && sd_bus_get_name_creds(bus, e.pub.bus_name.c_str(), SD_BUS_CREDS_PID, &creds) >= 0) {
      pid_t pid = 0;
      if (sd_bus_creds_get_pid(creds, &pid) >= 0 && pid > 0) {
        std::ifstream in("/proc/" + std::to_string(pid) + "/comm");
        std::getline(in, e.proc);
        for (char& ch : e.proc) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      sd_bus_creds_unref(creds);
    }
    return e.proc;
  }

  // Steam (and others) put an icon in both trays: the XEmbed one is dropped when a StatusNotifierItem of the same program exists.
  bool xembed_duplicates_item(const XembedTray::Icon& x) {
    if (x.wm_class.empty()) return false;
    for (auto& e : entries) {
      const std::string prog = item_program(*e);
      if (prog.size() < 3) continue;
      size_t from = 0;
      while (from <= x.wm_class.size()) {
        size_t comma = x.wm_class.find(',', from);
        if (comma == std::string::npos) comma = x.wm_class.size();
        const std::string word = x.wm_class.substr(from, comma - from);
        if (word.size() >= 3 && (word == prog || (prog.size() == 15 && word.compare(0, 15, prog) == 0))) return true;
        from = comma + 1;
      }
    }
    return false;
  }

  void rebuild_view() {
    view.clear();
    xembed_map.clear();
    for (auto& e : entries) view.push_back(e->pub);
    if (xembed) {
      const auto& icons = xembed->icons();
      for (size_t i = 0; i < icons.size(); ++i) {
        if (xembed_duplicates_item(icons[i])) continue;
        Item it;
        it.icon = icons[i].surface;
        it.title = icons[i].title;
        it.xembed = true;
        view.push_back(std::move(it));
        xembed_map.push_back(i);
      }
    }
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
    if (!xembed) {
      xembed = std::make_unique<XembedTray>(app, [this] { changed(); });
      xembed->set_icon_px(icon_px);
      xembed->start();
    }
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
      else if (!std::strcmp(key, "Menu")) {
        if (sd_bus_message_enter_container(m, 'v', "o") >= 0) {
          const char* path = nullptr;
          if (sd_bus_message_read(m, "o", &path) >= 0 && path) e.menu_path = path;
          sd_bus_message_exit_container(m);
          handled = true;
        }
      }
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
    if (index >= entries.size()) {
      const size_t k = index - entries.size();
      if (xembed && k < xembed_map.size()) xembed->click(xembed_map[k], button, x, y);
      return;
    }
    if (!bus) return;
    const Entry& e = *entries[index];
    const char* method = button == 0x111 ? "ContextMenu" : "Activate";
    struct Req {
      Impl* owner;
      std::string bus_name, menu_path;
      int x, y;
    };
    auto* req = new Req{this, e.pub.bus_name, e.menu_path, x, y};
    sd_bus_call_method_async(
        bus, nullptr, e.pub.bus_name.c_str(), e.pub.object_path.c_str(), kItemIface, method,
        [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
          std::unique_ptr<Req> r(static_cast<Req*>(ud));
          // no such method (Steam and other libappindicator items): their menu is what a click should show
          if (sd_bus_message_is_method_error(m, nullptr) && !r->menu_path.empty()) r->owner->show_menu(r->bus_name, r->menu_path, r->x, r->y);
          return 0;
        },
        req, "ii", x, y);
    sd_bus_flush(bus);
  }

  std::function<void(const MenuRequest&)> menu_handler;

  // com.canonical.dbusmenu: AboutToShow(0) lets the program fill the menu, then GetLayout reads it.
  void show_menu(const std::string& bus_name, const std::string& menu_path, int x, int y) {
    if (!bus || !menu_handler) return;
    struct Req {
      Impl* owner;
      std::string bus_name, menu_path;
      int x, y;
    };
    auto* req = new Req{this, bus_name, menu_path, x, y};
    sd_bus_call_method_async(
        bus, nullptr, bus_name.c_str(), menu_path.c_str(), "com.canonical.dbusmenu", "AboutToShow",
        [](sd_bus_message*, void* ud, sd_bus_error*) -> int {  // an error here (not implemented) does not matter
          std::unique_ptr<Req> r(static_cast<Req*>(ud));
          auto* next = new Req(*r);
          Impl* owner = r->owner;
          sd_bus_call_method_async(
              owner->bus, nullptr, next->bus_name.c_str(), next->menu_path.c_str(), "com.canonical.dbusmenu", "GetLayout",
              [](sd_bus_message* m, void* ud2, sd_bus_error*) -> int {
                std::unique_ptr<Req> q(static_cast<Req*>(ud2));
                if (sd_bus_message_is_method_error(m, nullptr)) return 0;
                unsigned revision = 0;
                MenuNode root;
                if (sd_bus_message_read(m, "u", &revision) < 0 || read_menu_node(m, &root, 0) < 0) return 0;
                MenuRequest rq;
                rq.bus_name = q->bus_name;
                rq.menu_path = q->menu_path;
                rq.x = q->x;
                rq.y = q->y;
                flatten_menu(root, "", &rq.entries);
                if (!rq.entries.empty() && q->owner->menu_handler) q->owner->menu_handler(rq);
                return 0;
              },
              next, "iias", 0, -1, 0);
          sd_bus_flush(owner->bus);
          return 0;
        },
        req, "i", 0);
    sd_bus_flush(bus);
  }

  struct TooltipRequest {
    std::function<void(std::string)> done;
    sd_bus_slot* slot = nullptr;
    Impl* owner = nullptr;
  };
  std::vector<TooltipRequest*> tooltip_requests;

  void tooltip(size_t index, std::function<void(std::string)> done) {
    if (index >= entries.size()) {
      const size_t k = index - entries.size();
      const bool ok = xembed && k < xembed_map.size() && xembed_map[k] < xembed->icons().size();
      return done(ok ? xembed->icons()[xembed_map[k]].title : "");
    }
    if (!bus) return;
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
void Tray::set_menu_handler(std::function<void(const MenuRequest&)> handler) { impl_->menu_handler = std::move(handler); }
void Tray::start() { impl_->start(); }
void Tray::set_icon_px(int px) {
  impl_->icon_px = px;
  if (impl_->xembed) impl_->xembed->set_icon_px(px);
}
const std::vector<Tray::Item>& Tray::items() const { return impl_->view; }
void Tray::click(size_t index, uint32_t button, int x, int y) { impl_->click(index, button, x, y); }
void Tray::tooltip(size_t index, std::function<void(std::string)> done) { impl_->tooltip(index, std::move(done)); }

}  // namespace fleetwm::bar
