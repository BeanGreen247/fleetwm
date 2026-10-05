#include "fleetkit.hpp"

#include <poll.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace fleetwm::kit {

// ---------------------------------------------------------------- colors ---

Color parse_color(const std::string& hex, Color fb) {
  if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') return fb;
  char* end = nullptr;
  const unsigned long v = std::strtoul(hex.c_str() + 1, &end, 16);
  if (!end || *end != '\0') return fb;
  Color c;
  if (hex.size() == 7) {
    c.r = ((v >> 16) & 0xff) / 255.0;
    c.g = ((v >> 8) & 0xff) / 255.0;
    c.b = (v & 0xff) / 255.0;
  } else {
    c.r = ((v >> 24) & 0xff) / 255.0;
    c.g = ((v >> 16) & 0xff) / 255.0;
    c.b = ((v >> 8) & 0xff) / 255.0;
    c.a = (v & 0xff) / 255.0;
  }
  return c;
}

void set_source(cairo_t* cr, const Color& c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  r = std::min(r, std::min(w, h) / 2.0);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

Palette load_palette(const ThemeConfig& theme) {
  Palette p;
  p.rounded = theme.corner_style == CornerStyle::Rounded;
  p.radius = theme.window_corner_radius > 0 ? theme.window_corner_radius : 10;
  std::ifstream in(themes_dir() + "/" + theme_css_filename(theme.theme));
  std::string line;
  while (std::getline(in, line)) {
    // "@define-color <name> #rrggbb;"
    std::istringstream ls(line);
    std::string kw, name, val;
    if (!(ls >> kw >> name >> val) || kw != "@define-color") continue;
    if (!val.empty() && val.back() == ';') val.pop_back();
    const Color c = parse_color(val, Color{-1, 0, 0, 1});
    if (c.r < 0) continue;
    if (name == "bg_primary") p.bg_primary = c;
    else if (name == "bg_secondary") p.bg_secondary = c;
    else if (name == "fg_primary") p.fg_primary = c;
    else if (name == "fg_secondary") p.fg_secondary = c;
    else if (name == "accent_color") p.accent = c;
  }
  if (!theme.accent.auto_extract) p.accent = parse_color(theme.accent.hex, p.accent);
  return p;
}

// ------------------------------------------------------------------ text ---

namespace {
void apply_font(cairo_t* cr, double px, bool bold) {
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, px);
}
}  // namespace

TextExtents measure_text(cairo_t* cr, const std::string& s, double px, bool bold) {
  apply_font(cr, px, bold);
  cairo_text_extents_t te;
  cairo_font_extents_t fe;
  cairo_text_extents(cr, s.c_str(), &te);
  cairo_font_extents(cr, &fe);
  return {te.x_advance, fe.ascent + fe.descent, fe.ascent};
}

double draw_text(cairo_t* cr, const std::string& s, double x, double y, double px, const Color& c,
                 bool bold) {
  apply_font(cr, px, bold);
  set_source(cr, c);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, s.c_str());
  cairo_new_path(cr);  // show_text leaves a current point that would join the next arc/line
  cairo_text_extents_t te;
  cairo_text_extents(cr, s.c_str(), &te);
  return te.x_advance;
}

// --------------------------------------------------------------- Surface ---

Surface::Surface(App& app, const Config& cfg) : app_(app), cfg_(cfg) {
  width_ = cfg.width;
  height_ = cfg.height;
  surface_ = wl_compositor_create_surface(app.compositor());
  static const wl_surface_listener sl = {
      [](void* d, wl_surface*, wl_output* o) {
        auto* s = static_cast<Surface*>(d);
        s->entered_.push_back(o);
        const int sc = s->app_.output_scale(o);
        if (sc > s->scale_) {
          s->scale_ = sc;
          s->dirty_ = true;
        }
      },
      [](void* d, wl_surface*, wl_output* o) {
        auto* s = static_cast<Surface*>(d);
        s->entered_.erase(std::remove(s->entered_.begin(), s->entered_.end(), o),
                          s->entered_.end());
        int sc = 1;
        for (auto* e : s->entered_) sc = std::max(sc, s->app_.output_scale(e));
        if (sc != s->scale_) {
          s->scale_ = sc;
          s->dirty_ = true;
        }
      },
      [](void*, wl_surface*, int32_t) {}, [](void*, wl_surface*, uint32_t) {}};
  wl_surface_add_listener(surface_, &sl, this);
  app.register_surface(surface_, this);

  if (cfg.toplevel) {
    xs_ = xdg_wm_base_get_xdg_surface(app.wm_base(), surface_);
    static const xdg_surface_listener xsl = {
        [](void* d, xdg_surface* xs, uint32_t serial) {
          auto* s = static_cast<Surface*>(d);
          xdg_surface_ack_configure(xs, serial);
          const int nw = s->pending_w_ > 0 ? s->pending_w_ : s->width_;
          const int nh = s->pending_h_ > 0 ? s->pending_h_ : s->height_;
          const bool changed = !s->configured_ || nw != s->width_ || nh != s->height_;
          s->width_ = nw;
          s->height_ = nh;
          s->configured_ = true;
          if (changed) {
            s->dirty_ = true;
            if (s->on_configure) s->on_configure(s->width_, s->height_);
          }
        }};
    xdg_surface_add_listener(xs_, &xsl, this);
    xt_ = xdg_surface_get_toplevel(xs_);
    static const xdg_toplevel_listener xtl = {
        [](void* d, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
          auto* s = static_cast<Surface*>(d);
          s->pending_w_ = w;
          s->pending_h_ = h;
        },
        [](void* d, xdg_toplevel*) {
          auto* s = static_cast<Surface*>(d);
          if (s->on_closed) s->on_closed();
        },
        [](void*, xdg_toplevel*, int32_t, int32_t) {},
        [](void*, xdg_toplevel*, wl_array*) {}};
    xdg_toplevel_add_listener(xt_, &xtl, this);
    if (!cfg.app_id.empty()) xdg_toplevel_set_app_id(xt_, cfg.app_id.c_str());
    if (!cfg.title.empty()) xdg_toplevel_set_title(xt_, cfg.title.c_str());
    if (cfg.min_width > 0 || cfg.min_height > 0) xdg_toplevel_set_min_size(xt_, cfg.min_width, cfg.min_height);
    wl_surface_commit(surface_);
    return;
  }

  ls_ = zwlr_layer_shell_v1_get_layer_surface(app.layer_shell(), surface_, cfg.output, cfg.layer,
                                              cfg.name.c_str());
  static const zwlr_layer_surface_v1_listener lsl = {
      [](void* d, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
        auto* s = static_cast<Surface*>(d);
        zwlr_layer_surface_v1_ack_configure(ls, serial);
        // Only redraw when something changed: this compositor re-sends
        // configure on every commit, so redrawing unconditionally here
        // would loop forever at full CPU (found as ~6% idle CPU).
        const int nw = w ? static_cast<int>(w) : s->width_;
        const int nh = h ? static_cast<int>(h) : s->height_;
        const bool changed = !s->configured_ || nw != s->width_ || nh != s->height_;
        s->width_ = nw;
        s->height_ = nh;
        s->configured_ = true;
        if (changed) {
          s->dirty_ = true;
          if (s->on_configure) s->on_configure(s->width_, s->height_);
        }
      },
      [](void* d, zwlr_layer_surface_v1*) {
        auto* s = static_cast<Surface*>(d);
        if (s->on_closed) s->on_closed();
      }};
  zwlr_layer_surface_v1_add_listener(ls_, &lsl, this);
  zwlr_layer_surface_v1_set_anchor(ls_, cfg.anchor);
  zwlr_layer_surface_v1_set_size(ls_, static_cast<uint32_t>(cfg.width),
                                 static_cast<uint32_t>(cfg.height));
  zwlr_layer_surface_v1_set_exclusive_zone(ls_, cfg.exclusive_zone);
  zwlr_layer_surface_v1_set_margin(ls_, cfg.margin_top, cfg.margin_right, cfg.margin_bottom,
                                   cfg.margin_left);
  zwlr_layer_surface_v1_set_keyboard_interactivity(ls_, cfg.keyboard_mode);
  wl_surface_commit(surface_);
}

Surface::~Surface() {
  app_.unregister_surface(surface_);
  if (frame_cb_) wl_callback_destroy(frame_cb_);
  for (auto& b : bufs_) free_buf(b);
  if (xt_) xdg_toplevel_destroy(xt_);
  if (xs_) xdg_surface_destroy(xs_);
  if (ls_) zwlr_layer_surface_v1_destroy(ls_);
  if (surface_) wl_surface_destroy(surface_);
}

void Surface::queue_draw() { dirty_ = true; }

void Surface::set_input_passthrough() {
  wl_region* empty = wl_compositor_create_region(app_.compositor());
  wl_surface_set_input_region(surface_, empty);
  wl_region_destroy(empty);
  wl_surface_commit(surface_);
}

void Surface::set_title(const std::string& title) {
  if (xt_) xdg_toplevel_set_title(xt_, title.c_str());
}

void Surface::set_size(int w, int h) {
  cfg_.width = w;
  cfg_.height = h;
  if (!ls_) return;  // toplevels are resized by the compositor
  zwlr_layer_surface_v1_set_size(ls_, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
  wl_surface_commit(surface_);
}
void Surface::set_anchor(uint32_t anchor) {
  cfg_.anchor = anchor;
  if (!ls_) return;
  zwlr_layer_surface_v1_set_anchor(ls_, anchor);
  wl_surface_commit(surface_);
}
void Surface::set_exclusive_zone(int z) {
  if (!ls_) return;
  zwlr_layer_surface_v1_set_exclusive_zone(ls_, z);
  wl_surface_commit(surface_);
}
void Surface::set_margins(int t, int r, int b, int l) {
  if (!ls_) return;
  zwlr_layer_surface_v1_set_margin(ls_, t, r, b, l);
  wl_surface_commit(surface_);
}
void Surface::set_keyboard_mode(uint32_t mode) {
  if (!ls_) return;
  zwlr_layer_surface_v1_set_keyboard_interactivity(ls_, mode);
  wl_surface_commit(surface_);
}

bool Surface::alloc(Buffer& b, int w, int h) {
  free_buf(b);
  const size_t stride = static_cast<size_t>(w) * 4, size = stride * h;
  const int fd = memfd_create("fleetwm-kit", MFD_CLOEXEC);
  if (fd < 0) return false;
  if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
    close(fd);
    return false;
  }
  void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    close(fd);
    return false;
  }
  wl_shm_pool* pool = wl_shm_create_pool(app_.shm(), fd, static_cast<int32_t>(size));
  b.buf = wl_shm_pool_create_buffer(pool, 0, w, h, static_cast<int32_t>(stride),
                                    WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);
  b.data = map;
  b.size = size;
  b.w = w;
  b.h = h;
  b.busy = false;
  static const wl_buffer_listener bl = {[](void* d, wl_buffer*) {
    static_cast<Buffer*>(d)->busy = false;
  }};
  wl_buffer_add_listener(b.buf, &bl, &b);
  return true;
}

void Surface::free_buf(Buffer& b) {
  if (b.buf) wl_buffer_destroy(b.buf);
  if (b.data) munmap(b.data, b.size);
  b = Buffer{};
}

void Surface::render() {
  dirty_ = false;
  if (width_ <= 0 || height_ <= 0) return;
  const int bw = width_ * scale_, bh = height_ * scale_;
  Buffer* b = nullptr;
  for (auto& cand : bufs_)
    if (!cand.busy && cand.w == bw && cand.h == bh && cand.buf) {
      b = &cand;
      break;
    }
  if (!b)
    for (auto& cand : bufs_)
      if (!cand.busy) {
        if (!alloc(cand, bw, bh)) return;
        b = &cand;
        break;
      }
  if (!b) {  // both still held by the compositor; retry when one is released
    dirty_ = true;
    return;
  }

  cairo_surface_t* cs = cairo_image_surface_create_for_data(
      static_cast<unsigned char*>(b->data), CAIRO_FORMAT_ARGB32, bw, bh, bw * 4);
  cairo_surface_set_device_scale(cs, scale_, scale_);
  cairo_t* cr = cairo_create(cs);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (on_draw) on_draw(cr, width_, height_);
  cairo_destroy(cr);
  cairo_surface_flush(cs);
  cairo_surface_destroy(cs);

  wl_surface_set_buffer_scale(surface_, scale_);
  wl_surface_attach(surface_, b->buf, 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, bw, bh);
  b->busy = true;
  frame_pending_ = true;
  frame_cb_ = wl_surface_frame(surface_);
  static const wl_callback_listener fl = {[](void* d, wl_callback* cb, uint32_t) {
    auto* s = static_cast<Surface*>(d);
    wl_callback_destroy(cb);
    s->frame_cb_ = nullptr;
    s->frame_pending_ = false;
  }};
  wl_callback_add_listener(frame_cb_, &fl, this);
  wl_surface_commit(surface_);
}

// ------------------------------------------------------------------- App ---

App::App() = default;

App::~App() {
  for (auto& w : watches_)
    if (w.owned && w.fd >= 0) close(w.fd);
  if (post_fd_ >= 0) close(post_fd_);
  if (xkb_state_) xkb_state_unref(xkb_state_);
  if (xkb_map_) xkb_keymap_unref(xkb_map_);
  if (xkb_ctx_) xkb_context_unref(xkb_ctx_);
  if (display_) wl_display_disconnect(display_);
}

int App::output_scale(wl_output* o) const {
  for (const auto& i : outputs_)
    if (i.output == o) return i.scale;
  return 1;
}

void App::unregister_surface(wl_surface* s) {
  if (find(s) == kb_focus_) kb_focus_ = nullptr;
  if (find(s) == ptr_focus_) ptr_focus_ = nullptr;
  surfaces_.erase(s);
}

Surface* App::find(wl_surface* s) const {
  auto it = surfaces_.find(s);
  return it == surfaces_.end() ? nullptr : it->second;
}

bool App::connect() {
  display_ = wl_display_connect(nullptr);
  if (!display_) {
    std::fprintf(stderr, "fleetwm: cannot connect to the Wayland display\n");
    return false;
  }
  registry_ = wl_display_get_registry(display_);
  static const wl_registry_listener rl = {
      [](void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
        auto* a = static_cast<App*>(d);
        if (!std::strcmp(iface, wl_compositor_interface.name)) {
          a->compositor_ = static_cast<wl_compositor*>(
              wl_registry_bind(r, name, &wl_compositor_interface, std::min(ver, 4u)));
        } else if (!std::strcmp(iface, wl_shm_interface.name)) {
          a->shm_ = static_cast<wl_shm*>(wl_registry_bind(r, name, &wl_shm_interface, 1));
        } else if (!std::strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
          a->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
              wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, std::min(ver, 4u)));
        } else if (!std::strcmp(iface, xdg_wm_base_interface.name)) {
          a->wm_base_ = static_cast<xdg_wm_base*>(
              wl_registry_bind(r, name, &xdg_wm_base_interface, std::min(ver, 2u)));
          static const xdg_wm_base_listener wl = {
              [](void*, xdg_wm_base* b, uint32_t serial) { xdg_wm_base_pong(b, serial); }};
          xdg_wm_base_add_listener(a->wm_base_, &wl, a);
        } else if (!std::strcmp(iface, wl_seat_interface.name)) {
          a->seat_ = static_cast<wl_seat*>(
              wl_registry_bind(r, name, &wl_seat_interface, std::min(ver, 5u)));
          // Listener must be attached before the next dispatch, or the seat's
          // capabilities event is discarded and no keyboard/pointer is made.
          a->setup_seat();
        } else if (!std::strcmp(iface, wl_data_device_manager_interface.name)) {
          a->data_manager_ = static_cast<wl_data_device_manager*>(
              wl_registry_bind(r, name, &wl_data_device_manager_interface, std::min(ver, 3u)));
        } else if (!std::strcmp(iface, wl_output_interface.name) && ver >= 2) {
          OutputInfo oi;
          oi.name = name;
          oi.output = static_cast<wl_output*>(
              wl_registry_bind(r, name, &wl_output_interface, std::min(ver, 3u)));
          a->outputs_.push_back(oi);
          static const wl_output_listener ol = {
              [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*,
                 const char*, int32_t) {},
              [](void* d, wl_output* o, uint32_t flags, int32_t w, int32_t h, int32_t) {
                if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
                auto* app = static_cast<App*>(d);
                for (auto& i : app->outputs_)
                  if (i.output == o) {
                    i.width = w;
                    i.height = h;
                  }
              },
              [](void* d, wl_output*) {
                auto* app = static_cast<App*>(d);
                if (app->on_outputs_changed) app->on_outputs_changed();
              },
              [](void* d, wl_output* o, int32_t f) {
                auto* app = static_cast<App*>(d);
                for (auto& i : app->outputs_)
                  if (i.output == o) i.scale = f > 0 ? f : 1;
              },
              [](void*, wl_output*, const char*) {},
              [](void*, wl_output*, const char*) {}};
          wl_output_add_listener(oi.output, &ol, a);
        }
      },
      [](void* d, wl_registry*, uint32_t name) {
        auto* a = static_cast<App*>(d);
        for (size_t i = 0; i < a->outputs_.size(); ++i)
          if (a->outputs_[i].name == name) {
            wl_output_destroy(a->outputs_[i].output);
            a->outputs_.erase(a->outputs_.begin() + static_cast<long>(i));
            if (a->on_outputs_changed) a->on_outputs_changed();
            return;
          }
      }};
  wl_registry_add_listener(registry_, &rl, this);
  wl_display_roundtrip(display_);
  wl_display_roundtrip(display_);  // output events
  if (!compositor_ || !shm_ || (!layer_shell_ && !wm_base_)) {
    std::fprintf(stderr, "fleetwm: compositor lacks wl_compositor/wl_shm/wlr-layer-shell\n");
    return false;
  }
  wl_display_roundtrip(display_);  // keymap / repeat info
  if (data_manager_ && seat_) {
    data_device_ = wl_data_device_manager_get_data_device(data_manager_, seat_);
    static const wl_data_offer_listener ol = {
        [](void* d, wl_data_offer* offer, const char* mime) {
          static_cast<App*>(d)->offer_mimes_[offer].push_back(mime);
        },
        [](void*, wl_data_offer*, uint32_t) {},
        [](void*, wl_data_offer*, uint32_t) {}};
    static const wl_data_device_listener dl = {
        [](void* d, wl_data_device*, wl_data_offer* offer) {
          auto* app = static_cast<App*>(d);
          app->offer_mimes_[offer];  // created empty; mimes follow
          wl_data_offer_add_listener(offer, &ol, app);
        },
        [](void*, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*) {},
        [](void*, wl_data_device*) {},
        [](void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {},
        [](void*, wl_data_device*) {},
        [](void* d, wl_data_device*, wl_data_offer* offer) {
          auto* app = static_cast<App*>(d);
          if (app->selection_) {
            app->offer_mimes_.erase(app->selection_);
            wl_data_offer_destroy(app->selection_);
          }
          app->selection_ = offer;  // null when the clipboard was cleared
        }};
    wl_data_device_add_listener(data_device_, &dl, this);
    wl_display_roundtrip(display_);
  }
  post_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (post_fd_ >= 0) {
    watch_fd(post_fd_, [this] {
      uint64_t n;
      if (read(post_fd_, &n, sizeof n) < 0) {
      }
      std::vector<std::function<void()>> q;
      {
        std::lock_guard<std::mutex> lk(post_mu_);
        q.swap(post_q_);
      }
      for (auto& f : q) f();
    });
  }
  return true;
}

void App::setup_seat() {
  xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  static const wl_seat_listener sl = {
      [](void* d, wl_seat* seat, uint32_t caps) {
        auto* a = static_cast<App*>(d);
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !a->keyboard_) {
          a->keyboard_ = wl_seat_get_keyboard(seat);
          static const wl_keyboard_listener kl = {
              // keymap
              [](void* d2, wl_keyboard*, uint32_t fmt, int32_t fd, uint32_t size) {
                auto* app = static_cast<App*>(d2);
                if (fmt == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
                  void* m = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
                  if (m != MAP_FAILED) {
                    xkb_keymap* km = xkb_keymap_new_from_string(
                        app->xkb_ctx_, static_cast<const char*>(m), XKB_KEYMAP_FORMAT_TEXT_V1,
                        XKB_KEYMAP_COMPILE_NO_FLAGS);
                    munmap(m, size);
                    if (km) {
                      if (app->xkb_state_) xkb_state_unref(app->xkb_state_);
                      if (app->xkb_map_) xkb_keymap_unref(app->xkb_map_);
                      app->xkb_map_ = km;
                      app->xkb_state_ = xkb_state_new(km);
                    }
                  }
                }
                close(fd);
              },
              // enter
              [](void* d2, wl_keyboard*, uint32_t, wl_surface* s, wl_array*) {
                auto* app = static_cast<App*>(d2);
                app->kb_focus_ = app->find(s);
              },
              // leave
              [](void* d2, wl_keyboard*, uint32_t, wl_surface*) {
                auto* app = static_cast<App*>(d2);
                app->kb_focus_ = nullptr;
                if (app->repeat_timer_) {
                  app->unwatch(app->repeat_timer_);
                  app->repeat_timer_ = 0;
                }
                app->repeat_key_ = 0;
              },
              // key
              [](void* d2, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
                auto* app = static_cast<App*>(d2);
                if (!app->xkb_state_ || !app->kb_focus_) return;
                const xkb_keycode_t kc = key + 8;
                const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
                KeyEvent ev;
                ev.pressed = pressed;
                ev.sym = xkb_state_key_get_one_sym(app->xkb_state_, kc);
                char buf[16];
                const int n = xkb_state_key_get_utf8(app->xkb_state_, kc, buf, sizeof buf);
                if (n > 0 && pressed) ev.utf8.assign(buf, static_cast<size_t>(n));
                auto active = [&](const char* name) {
                  return xkb_state_mod_name_is_active(app->xkb_state_, name,
                                                      XKB_STATE_MODS_EFFECTIVE) > 0;
                };
                if (active(XKB_MOD_NAME_SHIFT)) ev.mods |= kShift;
                if (active(XKB_MOD_NAME_CTRL)) ev.mods |= kCtrl;
                if (active(XKB_MOD_NAME_ALT)) ev.mods |= kAlt;
                if (active(XKB_MOD_NAME_LOGO)) ev.mods |= kSuper;
                if (app->repeat_timer_ && (!pressed ? app->repeat_key_ == key : true)) {
                  app->unwatch(app->repeat_timer_);
                  app->repeat_timer_ = 0;
                  app->repeat_key_ = 0;
                }
                Surface* focus = app->kb_focus_;
                if (focus->on_key) focus->on_key(ev);
                if (pressed && app->repeat_rate_ > 0 &&
                    xkb_keymap_key_repeats(app->xkb_map_, kc) && app->kb_focus_ == focus) {
                  app->repeat_key_ = key;
                  app->repeat_ev_ = ev;
                  app->repeat_ev_.repeat = true;
                  app->repeat_timer_ = app->add_oneshot(app->repeat_delay_, [app] {
                    app->repeat_timer_ = 0;
                    app->key_repeat_tick();
                    if (app->repeat_key_)
                      app->repeat_timer_ =
                          app->add_timer(std::max(1, 1000 / app->repeat_rate_),
                                         [app] { app->key_repeat_tick(); });
                  });
                }
              },
              // modifiers
              [](void* d2, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock,
                 uint32_t grp) {
                auto* app = static_cast<App*>(d2);
                if (app->xkb_state_) xkb_state_update_mask(app->xkb_state_, dep, lat, lock, 0, 0, grp);
              },
              // repeat info
              [](void* d2, wl_keyboard*, int32_t rate, int32_t delay) {
                auto* app = static_cast<App*>(d2);
                app->repeat_rate_ = rate;
                app->repeat_delay_ = delay;
              }};
          wl_keyboard_add_listener(a->keyboard_, &kl, a);
        }
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !a->pointer_) {
          a->pointer_ = wl_seat_get_pointer(seat);
          static const wl_pointer_listener pl = {
              [](void* d2, wl_pointer*, uint32_t, wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
                auto* app = static_cast<App*>(d2);
                app->ptr_focus_ = app->find(s);
                app->ptr_x_ = wl_fixed_to_double(x);
                app->ptr_y_ = wl_fixed_to_double(y);
                if (app->ptr_focus_ && app->ptr_focus_->on_motion)
                  app->ptr_focus_->on_motion(app->ptr_x_, app->ptr_y_);
              },
              [](void* d2, wl_pointer*, uint32_t, wl_surface*) {
                auto* app = static_cast<App*>(d2);
                Surface* f = app->ptr_focus_;
                app->ptr_focus_ = nullptr;
                if (f && f->on_leave) f->on_leave();
              },
              [](void* d2, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                auto* app = static_cast<App*>(d2);
                app->ptr_x_ = wl_fixed_to_double(x);
                app->ptr_y_ = wl_fixed_to_double(y);
                if (app->ptr_focus_ && app->ptr_focus_->on_motion)
                  app->ptr_focus_->on_motion(app->ptr_x_, app->ptr_y_);
              },
              [](void* d2, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
                auto* app = static_cast<App*>(d2);
                if (app->ptr_focus_ && app->ptr_focus_->on_button)
                  app->ptr_focus_->on_button(app->ptr_x_, app->ptr_y_, button,
                                             state == WL_POINTER_BUTTON_STATE_PRESSED);
              },
              [](void* d2, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t v) {
                auto* app = static_cast<App*>(d2);
                if (app->ptr_focus_ && app->ptr_focus_->on_scroll) {
                  const double d3 = wl_fixed_to_double(v);
                  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                    app->ptr_focus_->on_scroll(0, d3);
                  else
                    app->ptr_focus_->on_scroll(d3, 0);
                }
              },
              // frame, axis_source, axis_stop, axis_discrete, axis_value120,
              // axis_relative_direction: libwayland aborts the process when an
              // event arrives with a NULL listener, so every slot is filled.
              [](void*, wl_pointer*) {},
              [](void*, wl_pointer*, uint32_t) {},
              [](void*, wl_pointer*, uint32_t, uint32_t) {},
              [](void*, wl_pointer*, uint32_t, int32_t) {},
              [](void*, wl_pointer*, uint32_t, int32_t) {},
              [](void*, wl_pointer*, uint32_t, uint32_t) {}};
          wl_pointer_add_listener(a->pointer_, &pl, a);
        }
      },
      [](void*, wl_seat*, const char*) {}};
  wl_seat_add_listener(seat_, &sl, this);
}

void App::key_repeat_tick() {
  if (!repeat_key_ || !kb_focus_) return;
  if (kb_focus_->on_key) kb_focus_->on_key(repeat_ev_);
}

void App::paste_text(std::function<void(const std::string&)> cb) {
  if (!selection_) {
    cb("");
    return;
  }
  const auto& mimes = offer_mimes_[selection_];
  const char* chosen = nullptr;
  for (const char* want : {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING"}) {
    if (std::find(mimes.begin(), mimes.end(), want) != mimes.end()) {
      chosen = want;
      break;
    }
  }
  if (!chosen) {
    cb("");
    return;
  }
  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
    cb("");
    return;
  }
  wl_data_offer_receive(selection_, chosen, fds[1]);
  close(fds[1]);
  wl_display_flush(display_);
  struct Reader {
    std::string data;
    int id = 0;
  };
  auto reader = std::make_shared<Reader>();
  const int rfd = fds[0];
  reader->id = watch_fd(rfd, [this, reader, rfd, cb = std::move(cb)]() mutable {
    char buf[4096];
    for (;;) {
      const ssize_t n = read(rfd, buf, sizeof buf);
      if (n > 0) {
        if (reader->data.size() < 65536) reader->data.append(buf, static_cast<size_t>(n));
        continue;
      }
      if (n < 0 && errno == EAGAIN) return;  // wait for more
      break;  // EOF or error: the source closed its end
    }
    close(rfd);
    const int id = reader->id;
    std::string text = std::move(reader->data);
    unwatch(id);
    cb(text);
  });
  // Safety net: give up after 2 s so a dead clipboard owner cannot leak the fd.
  add_oneshot(2000, [this, reader, rfd] {
    for (auto& w : watches_)
      if (w.id == reader->id) {
        close(rfd);
        unwatch(reader->id);
        return;
      }
  });
}

void App::post(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lk(post_mu_);
    post_q_.push_back(std::move(fn));
  }
  const uint64_t one = 1;
  if (post_fd_ >= 0 && write(post_fd_, &one, sizeof one) < 0) {
  }
}

int App::watch_fd(int fd, std::function<void()> cb) {
  watches_.push_back({next_id_, fd, false, false, std::move(cb)});
  return next_id_++;
}

int App::add_timer(int ms, std::function<void()> cb, bool fire_now) {
  const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  itimerspec its{};
  its.it_interval.tv_sec = ms / 1000;
  its.it_interval.tv_nsec = (ms % 1000) * 1000000L;
  its.it_value = its.it_interval;
  if (fire_now) its.it_value = {0, 1};
  timerfd_settime(fd, 0, &its, nullptr);
  watches_.push_back({next_id_, fd, true, false, std::move(cb)});
  return next_id_++;
}

int App::add_oneshot(int ms, std::function<void()> cb) {
  const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  itimerspec its{};
  its.it_value.tv_sec = ms / 1000;
  its.it_value.tv_nsec = (ms % 1000) * 1000000L;
  if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) its.it_value.tv_nsec = 1;
  timerfd_settime(fd, 0, &its, nullptr);
  watches_.push_back({next_id_, fd, true, true, std::move(cb)});
  return next_id_++;
}

void App::unwatch(int id) {
  for (size_t i = 0; i < watches_.size(); ++i)
    if (watches_[i].id == id) {
      if (watches_[i].owned) close(watches_[i].fd);
      watches_.erase(watches_.begin() + static_cast<long>(i));
      return;
    }
}

void App::run() {
  while (running_) {
    // Render everything that is dirty and idle before sleeping.
    for (auto& [ws, s] : std::map<wl_surface*, Surface*>(surfaces_))
      if (surfaces_.count(ws) && s->wants_render()) s->render();
    wl_display_dispatch_pending(display_);
    if (wl_display_flush(display_) < 0 && errno != EAGAIN) break;
    if (wl_display_prepare_read(display_) != 0) continue;

    std::vector<pollfd> fds;
    std::vector<int> ids;
    fds.push_back({wl_display_get_fd(display_), POLLIN, 0});
    ids.push_back(0);
    for (const auto& w : watches_) {
      fds.push_back({w.fd, POLLIN, 0});
      ids.push_back(w.id);
    }
    if (poll(fds.data(), fds.size(), -1) < 0 && errno != EINTR) {
      wl_display_cancel_read(display_);
      break;
    }
    if (fds[0].revents & POLLIN) {
      if (wl_display_read_events(display_) < 0) break;
    } else {
      wl_display_cancel_read(display_);
    }
    if (fds[0].revents & (POLLERR | POLLHUP)) break;
    wl_display_dispatch_pending(display_);

    for (size_t i = 1; i < fds.size(); ++i) {
      if (!(fds[i].revents & POLLIN)) continue;
      // The watch may have been removed by an earlier callback this round.
      auto it = std::find_if(watches_.begin(), watches_.end(),
                             [&](const Watch& w) { return w.id == ids[i]; });
      if (it == watches_.end()) continue;
      const bool owned = it->owned, oneshot = it->oneshot;
      std::function<void()> cb = it->cb;
      if (owned) {  // timerfd: consume the expiration count
        uint64_t n;
        if (read(it->fd, &n, sizeof n) < 0) {
        }
      }
      if (oneshot) unwatch(ids[i]);
      if (cb) cb();
    }
  }
}

int watch_dirs(App& app, const std::vector<std::string>& dirs, std::function<void()> cb) {
  const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (fd < 0) return 0;
  for (const auto& d : dirs) {
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    inotify_add_watch(fd, d.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
  }
  // The App owns only timerfds for closing; this fd lives for the process.
  return app.watch_fd(fd, [fd, cb = std::move(cb)] {
    char buf[4096];
    while (read(fd, buf, sizeof buf) > 0) {
    }
    cb();
  });
}

Tooltip::Tooltip(App& app, const Palette& pal, const std::string& text, int x, int y) {
  // Measure with a scratch context to size the surface.
  cairo_surface_t* cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(cs);
  const TextExtents te = measure_text(cr, text, 13);
  cairo_destroy(cr);
  cairo_surface_destroy(cs);
  const int w = static_cast<int>(std::ceil(te.width)) + 16, h = static_cast<int>(std::ceil(te.height)) + 10;

  Surface::Config cfg;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  cfg.width = w;
  cfg.height = h;
  cfg.exclusive_zone = -1;
  cfg.margin_top = y;
  cfg.margin_left = std::max(0, x - w / 2);
  cfg.keyboard_mode = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
  cfg.name = "fleetwm-tooltip";
  surface_ = std::make_unique<Surface>(app, cfg);
  surface_->set_input_passthrough();
  const Palette p = pal;
  surface_->on_draw = [p, text, te](cairo_t* c, int sw, int sh) {
    rounded_rect(c, 0.5, 0.5, sw - 1, sh - 1, p.rounded ? 6 : 0);
    set_source(c, p.bg_secondary);
    cairo_fill_preserve(c);
    set_source(c, p.fg_secondary);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    draw_text(c, text, 8, (sh - te.height) / 2.0 + te.ascent, 13, p.fg_primary);
  };
}

}  // namespace fleetwm::kit
