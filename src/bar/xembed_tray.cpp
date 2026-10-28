#include "xembed_tray.hpp"

#include <unistd.h>

#include <xcb/composite.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include <dirent.h>
#include <sys/stat.h>

namespace fleetwm::bar {

bool is_xwayland_comm(const std::string& comm) {
  std::string c = comm;
  while (!c.empty() && (c.back() == '\n' || c.back() == ' ')) c.pop_back();
  return c == "Xwayland";
}

void bgrx_to_argb(const unsigned char* src, int src_stride, unsigned char* dst, int dst_stride, int w, int h) {
  for (int y = 0; y < h; ++y) {
    const unsigned char* s = src + static_cast<size_t>(y) * src_stride;
    unsigned char* d = dst + static_cast<size_t>(y) * dst_stride;
    for (int x = 0; x < w; ++x, s += 4, d += 4) {
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = 255;
    }
  }
}

int x_button_for(uint32_t evdev) {
  switch (evdev) {
    case 0x110: return 1;
    case 0x112: return 2;
    case 0x111: return 3;
    default: return 0;
  }
}

namespace {
constexpr uint32_t kSysTrayRequestDock = 0;
constexpr int kParkCoord = -12000;  // icon containers sit far outside every output

xcb_atom_t intern(xcb_connection_t* c, const char* name) {
  xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr);
  const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
  std::free(r);
  return a;
}

// Is the Xwayland server that serves $DISPLAY running? Its command line starts "Xwayland :N ...". It is started on demand by the
// compositor and has been re-parented away from it, so the display name is the only link. When it is not running the bar stays off
// rather than starting it by connecting.
bool xwayland_running(const char* display) {
  if (!display || !*display) return false;
  DIR* d = opendir("/proc");
  if (!d) return false;
  bool found = false;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
    std::ifstream comm(std::string("/proc/") + e->d_name + "/comm");
    std::string line;
    if (!comm || !std::getline(comm, line) || !is_xwayland_comm(line)) continue;
    std::ifstream cmd(std::string("/proc/") + e->d_name + "/cmdline");
    std::string args((std::istreambuf_iterator<char>(cmd)), std::istreambuf_iterator<char>());
    std::string want(display);
    want.push_back('\0');
    size_t at = 0;
    while ((at = args.find(want, at)) != std::string::npos) {
      if (at == 0 || args[at - 1] == '\0') {
        found = true;
        break;
      }
      ++at;
    }
    if (found) break;
  }
  closedir(d);
  return found;
}
}  // namespace

struct XembedTray::Impl {
  struct Entry {
    xcb_window_t win = 0;
    xcb_window_t box = 0;
    uint64_t hash = 0;
    Icon pub;
  };

  kit::App& app;
  std::function<void()> on_change;
  xcb_connection_t* c = nullptr;
  xcb_screen_t* screen = nullptr;
  xcb_window_t owner = 0;
  xcb_atom_t a_opcode = 0, a_xembed = 0, a_info = 0, a_name = 0, a_utf8 = 0, a_wmname = 0;
  int px = 22;
  int fd_watch = 0, scan_timer = 0, grab_timer = 0;
  std::vector<std::unique_ptr<Entry>> entries;
  std::vector<Icon> view;
  bool use_composite = false;

  Impl(kit::App& a, std::function<void()> cb) : app(a), on_change(std::move(cb)) {}
  ~Impl() {
    if (scan_timer) app.unwatch(scan_timer);
    disconnect();
  }

  void rebuild_view() {
    view.clear();
    for (auto& e : entries) view.push_back(e->pub);
  }

  void changed() {
    rebuild_view();
    if (on_change) on_change();
  }

  void disconnect() {
    if (fd_watch) app.unwatch(fd_watch);
    if (grab_timer) app.unwatch(grab_timer);
    fd_watch = grab_timer = 0;
    for (auto& e : entries)
      if (e->pub.surface) cairo_surface_destroy(e->pub.surface);
    entries.clear();
    view.clear();
    if (c) xcb_disconnect(c);
    c = nullptr;
    screen = nullptr;
  }

  void start() {
    scan_timer = app.add_timer(1500, [this] { scan(); }, true);
  }

  void scan() {
    if (c) return;
    if (!xwayland_running(std::getenv("DISPLAY"))) return;
    connect();
  }

  void connect() {
    int scr = 0;
    c = xcb_connect(nullptr, &scr);
    if (!c || xcb_connection_has_error(c)) {
      if (c) xcb_disconnect(c);
      c = nullptr;
      return;
    }
    const xcb_setup_t* setup = xcb_get_setup(c);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < scr && it.rem; ++i) xcb_screen_next(&it);
    screen = it.data;
    if (!screen) {
      xcb_disconnect(c);
      c = nullptr;
      return;
    }
    const std::string sel_name = "_NET_SYSTEM_TRAY_S" + std::to_string(scr);
    const xcb_atom_t sel = intern(c, sel_name.c_str());
    a_opcode = intern(c, "_NET_SYSTEM_TRAY_OPCODE");
    a_xembed = intern(c, "_XEMBED");
    a_info = intern(c, "_XEMBED_INFO");
    a_utf8 = intern(c, "UTF8_STRING");
    a_wmname = intern(c, "_NET_WM_NAME");
    const xcb_atom_t a_orient = intern(c, "_NET_SYSTEM_TRAY_ORIENTATION");
    const xcb_atom_t a_manager = intern(c, "MANAGER");

    xcb_get_selection_owner_reply_t* cur = xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, sel), nullptr);
    const bool taken = cur && cur->owner != XCB_NONE;
    std::free(cur);
    if (taken) {
      std::fprintf(stderr, "fleetwm-bar: xembed tray: another program owns the X tray; leaving it alone\n");
      xcb_disconnect(c);
      c = nullptr;
      // do not retry every tick
      app.unwatch(scan_timer);
      scan_timer = 0;
      return;
    }

    const xcb_query_extension_reply_t* ext = xcb_get_extension_data(c, &xcb_composite_id);
    use_composite = ext && ext->present;

    owner = xcb_generate_id(c);
    const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(c, XCB_COPY_FROM_PARENT, owner, screen->root, kParkCoord, kParkCoord, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, XCB_CW_EVENT_MASK, &mask);
    const uint32_t horizontal = 0;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, owner, a_orient, XCB_ATOM_CARDINAL, 32, 1, &horizontal);
    xcb_set_selection_owner(c, owner, sel, XCB_CURRENT_TIME);
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = screen->root;
    ev.type = a_manager;
    ev.data.data32[0] = XCB_CURRENT_TIME;
    ev.data.data32[1] = sel;
    ev.data.data32[2] = owner;
    xcb_send_event(c, 0, screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char*>(&ev));
    xcb_flush(c);
    if (xcb_connection_has_error(c)) {
      disconnect();
      return;
    }
    fd_watch = app.watch_fd(xcb_get_file_descriptor(c), [this] { pump(); });
    grab_timer = app.add_timer(300, [this] { grab_all(); });
    std::fprintf(stderr, "fleetwm-bar: xembed tray: ready on %s%s\n", sel_name.c_str(), use_composite ? "" : " (no Composite)");
  }

  std::string window_title(xcb_window_t w) {
    xcb_get_property_reply_t* r = xcb_get_property_reply(c, xcb_get_property(c, 0, w, a_wmname, a_utf8, 0, 128), nullptr);
    std::string t;
    if (r && xcb_get_property_value_length(r) > 0)
      t.assign(static_cast<const char*>(xcb_get_property_value(r)), static_cast<size_t>(xcb_get_property_value_length(r)));
    std::free(r);
    if (t.empty()) {
      r = xcb_get_property_reply(c, xcb_get_property(c, 0, w, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 128), nullptr);
      if (r && xcb_get_property_value_length(r) > 0)
        t.assign(static_cast<const char*>(xcb_get_property_value(r)), static_cast<size_t>(xcb_get_property_value_length(r)));
      std::free(r);
    }
    return t;
  }

  void dock(xcb_window_t win) {
    for (auto& e : entries)
      if (e->win == win) return;
    xcb_get_geometry_reply_t* g = xcb_get_geometry_reply(c, xcb_get_geometry(c, win), nullptr);
    if (!g) return;  // gone already
    std::free(g);
    auto e = std::make_unique<Entry>();
    e->win = win;
    e->box = xcb_generate_id(c);
    const uint32_t vals[] = {1u, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};
    xcb_create_window(c, XCB_COPY_FROM_PARENT, e->box, screen->root, kParkCoord, kParkCoord, static_cast<uint16_t>(px),
                      static_cast<uint16_t>(px), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, vals);
    const uint32_t wattr = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(c, win, XCB_CW_EVENT_MASK, &wattr);
    xcb_reparent_window(c, win, e->box, 0, 0);
    const uint32_t size[] = {static_cast<uint32_t>(px), static_cast<uint32_t>(px)};
    xcb_configure_window(c, win, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
    xcb_map_window(c, win);
    xcb_map_window(c, e->box);
    if (use_composite) xcb_composite_redirect_window(c, e->box, XCB_COMPOSITE_REDIRECT_MANUAL);
    // tell the program it is embedded (XEMBED_EMBEDDED_NOTIFY, version 0)
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = win;
    ev.type = a_xembed;
    ev.data.data32[0] = XCB_CURRENT_TIME;
    ev.data.data32[1] = 0;  // XEMBED_EMBEDDED_NOTIFY
    ev.data.data32[2] = 0;
    ev.data.data32[3] = e->box;
    ev.data.data32[4] = 0;
    xcb_send_event(c, 0, win, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char*>(&ev));
    xcb_flush(c);
    e->pub.title = window_title(win);
    entries.push_back(std::move(e));
    grab_one(*entries.back(), true);
    changed();
  }

  void remove(xcb_window_t win) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e->win == win; });
    if (it == entries.end()) return;
    if (use_composite) xcb_composite_unredirect_window(c, (*it)->box, XCB_COMPOSITE_REDIRECT_MANUAL);
    xcb_destroy_window(c, (*it)->box);  // the icon window's own death is the program's business
    xcb_flush(c);
    if ((*it)->pub.surface) cairo_surface_destroy((*it)->pub.surface);
    entries.erase(it);
    changed();
  }

  // Copies the container's pixels. Returns true when the picture changed.
  bool grab_one(Entry& e, bool force) {
    xcb_get_image_reply_t* img = xcb_get_image_reply(
        c, xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, e.box, 0, 0, static_cast<uint16_t>(px), static_cast<uint16_t>(px), ~0u), nullptr);
    if (!img) return false;
    const int len = xcb_get_image_data_length(img);
    bool changed_pic = false;
    if (len >= px * px * 4) {  // 32 bits per pixel (depth 24 or 32); anything else is not drawn
      const unsigned char* data = xcb_get_image_data(img);
      uint64_t h = 1469598103934665603ull;
      for (int i = 0; i < px * px * 4; ++i) h = (h ^ data[i]) * 1099511628211ull;
      if (force || h != e.hash || !e.pub.surface) {
        e.hash = h;
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        cairo_surface_flush(s);
        bgrx_to_argb(data, px * 4, cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s), px, px);
        cairo_surface_mark_dirty(s);
        if (e.pub.surface) cairo_surface_destroy(e.pub.surface);
        e.pub.surface = s;
        changed_pic = true;
      }
    }
    std::free(img);
    return changed_pic;
  }

  void grab_all() {
    if (!c) return;
    bool any = false;
    for (auto& e : entries) any |= grab_one(*e, false);
    if (any) changed();
  }

  void pump() {
    while (xcb_generic_event_t* ev = xcb_poll_for_event(c)) {
      const uint8_t type = ev->response_type & 0x7f;
      if (type == XCB_CLIENT_MESSAGE) {
        auto* m = reinterpret_cast<xcb_client_message_event_t*>(ev);
        if (m->type == a_opcode && m->data.data32[1] == kSysTrayRequestDock) dock(m->data.data32[2]);
      } else if (type == XCB_DESTROY_NOTIFY) {
        remove(reinterpret_cast<xcb_destroy_notify_event_t*>(ev)->window);
      } else if (type == XCB_REPARENT_NOTIFY) {
        auto* r = reinterpret_cast<xcb_reparent_notify_event_t*>(ev);
        auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e->win == r->window; });
        if (it != entries.end() && r->parent != (*it)->box) remove(r->window);
      } else if (type == XCB_SELECTION_CLEAR) {
        std::free(ev);
        disconnect();
        return;
      }
      std::free(ev);
    }
    if (xcb_connection_has_error(c)) {
      disconnect();  // Xwayland went away; the scan timer connects again when it is back
      changed();
    }
  }

  void set_px(int p) {
    px = p;
  }

  void click(size_t index, uint32_t button, int x, int y) {
    if (!c || index >= entries.size()) return;
    const int xb = x_button_for(button);
    if (!xb) return;
    const Entry& e = *entries[index];
    for (const uint8_t type : {static_cast<uint8_t>(XCB_BUTTON_PRESS), static_cast<uint8_t>(XCB_BUTTON_RELEASE)}) {
      xcb_button_press_event_t ev{};
      ev.response_type = type;
      ev.detail = static_cast<xcb_button_t>(xb);
      ev.time = XCB_CURRENT_TIME;
      ev.root = screen->root;
      ev.event = e.win;
      ev.root_x = static_cast<int16_t>(x);
      ev.root_y = static_cast<int16_t>(y);
      ev.event_x = static_cast<int16_t>(px / 2);
      ev.event_y = static_cast<int16_t>(px / 2);
      ev.state = type == XCB_BUTTON_RELEASE ? static_cast<uint16_t>(XCB_BUTTON_MASK_1 << (xb - 1)) : 0;
      ev.same_screen = 1;
      xcb_send_event(c, 1, e.win, type == XCB_BUTTON_PRESS ? XCB_EVENT_MASK_BUTTON_PRESS : XCB_EVENT_MASK_BUTTON_RELEASE,
                     reinterpret_cast<const char*>(&ev));
    }
    xcb_flush(c);
  }
};

XembedTray::XembedTray(kit::App& app, std::function<void()> on_change) : impl_(std::make_unique<Impl>(app, std::move(on_change))) {}
XembedTray::~XembedTray() = default;
void XembedTray::start() { impl_->start(); }
void XembedTray::set_icon_px(int px) { impl_->set_px(px); }
const std::vector<XembedTray::Icon>& XembedTray::icons() const { return impl_->view; }
void XembedTray::click(size_t index, uint32_t button, int x, int y) { impl_->click(index, button, x, y); }

}  // namespace fleetwm::bar
