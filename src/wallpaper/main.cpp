// fleetwm-wallpaper: GTK-free layer-shell client. Paints the
// configured wallpaper image (or a solid color) on the BACKGROUND layer
// into a wl_shm buffer, once per configure/config change, then sleeps in
// poll() -- zero CPU while idle and none of GTK/GLib/Pango/Cairo mapped.

#include <poll.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <wayland-client.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "quit_signals.hpp"
#include "image.hpp"
#include "malloc_tuning.hpp"
#include "theme.hpp"
#include "wallpaper_config.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace {

using namespace fleetwm;

struct Output {
  wl_output* output = nullptr;
  uint32_t name = 0;
  int32_t scale = 1;
};

struct State {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  zwlr_layer_shell_v1* layer_shell = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer_surface = nullptr;
  std::vector<Output*> outputs;
  std::vector<wl_output*> entered;  // outputs the surface is currently on
  int width = 0, height = 0;        // logical size from configure
  int scale = 1;
  bool configured = false;
  bool running = true;
  ThemeConfig theme;
  WallpaperConfig config;
};

State g;

uint32_t parse_color(const std::string& hex, uint32_t fallback) {
  if (hex.size() == 7 && hex[0] == '#') {
    char* end = nullptr;
    const unsigned long v = std::strtoul(hex.c_str() + 1, &end, 16);
    if (end && *end == '\0') return static_cast<uint32_t>(v);
  }
  return fallback;
}

// bg_primary of each built-in theme (themes/*.css), the backdrop when no
// image is set.
uint32_t theme_bg_primary(ThemeName t) {
  switch (t) {
    case ThemeName::Dracula: return 0x282a36;
    case ThemeName::OledBlack: return 0x000000;
    case ThemeName::Light: return 0xf5f5f5;
    case ThemeName::Dark:
    case ThemeName::Catppuccin:
    default: return 0x1e1e2e;
  }
}

int output_scale_for_surface() {
  int s = 1;
  for (auto* wo : g.entered)
    for (auto* o : g.outputs)
      if (o->output == wo) s = std::max(s, o->scale);
  return s;
}

void paint() {
  if (!g.configured || g.width <= 0 || g.height <= 0) return;
  g.scale = output_scale_for_surface();
  const int bw = g.width * g.scale, bh = g.height * g.scale;
  const size_t stride = static_cast<size_t>(bw) * 4, size = stride * bh;

  const int fd = memfd_create("fleetwm-wallpaper", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) < 0) {
    std::perror("fleetwm-wallpaper: shm");
    if (fd >= 0) close(fd);
    return;
  }
  void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    close(fd);
    return;
  }
  auto* px = static_cast<uint8_t*>(map);

  bool drawn = false;
  if (!g.config.use_solid_color && !g.config.path.empty()) {
    kit::Image img = kit::load_image(g.config.path);
    if (img.ok()) {
      kit::render_cover(img, bw, bh, px);
      drawn = true;
    } else {
      std::fprintf(stderr, "fleetwm-wallpaper: cannot load '%s'\n", g.config.path.c_str());
    }
  }
  if (!drawn) {
    const uint32_t rgb = g.config.use_solid_color
                             ? parse_color(g.config.solid_color, theme_bg_primary(g.theme.theme))
                             : theme_bg_primary(g.theme.theme);
    const uint32_t argb = 0xff000000u | rgb;
    auto* p32 = reinterpret_cast<uint32_t*>(px);
    for (size_t i = 0, n = static_cast<size_t>(bw) * bh; i < n; ++i) p32[i] = argb;
  }
  munmap(map, size);

  wl_shm_pool* pool = wl_shm_create_pool(g.shm, fd, static_cast<int32_t>(size));
  wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, bw, bh, static_cast<int32_t>(stride),
                                             WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  wl_surface_set_buffer_scale(g.surface, g.scale);
  wl_surface_attach(g.surface, buf, 0, 0);
  wl_surface_damage_buffer(g.surface, 0, 0, bw, bh);
  wl_surface_commit(g.surface);
  // The compositor copies shm content on commit; the wl_buffer proxy is no
  // longer needed (destroy is legal right after commit per the protocol).
  wl_buffer_destroy(buf);
}

// ---- wl_output ----
void out_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*,
                  const char*, int32_t) {}
void out_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void out_done(void*, wl_output*) { paint(); }
void out_scale(void* data, wl_output*, int32_t factor) {
  static_cast<Output*>(data)->scale = factor > 0 ? factor : 1;
}
const wl_output_listener kOutputListener = {out_geometry, out_mode, out_done, out_scale, nullptr,
                                            nullptr};

// ---- wl_surface ----
void surf_enter(void*, wl_surface*, wl_output* o) {
  g.entered.push_back(o);
  paint();
}
void surf_leave(void*, wl_surface*, wl_output* o) {
  for (size_t i = 0; i < g.entered.size(); ++i)
    if (g.entered[i] == o) {
      g.entered.erase(g.entered.begin() + static_cast<long>(i));
      break;
    }
}
const wl_surface_listener kSurfaceListener = {surf_enter, surf_leave, nullptr, nullptr};

// ---- layer surface ----
void ls_configure(void*, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  const bool changed = !g.configured || static_cast<int>(w) != g.width ||
                       static_cast<int>(h) != g.height;
  g.width = static_cast<int>(w);
  g.height = static_cast<int>(h);
  g.configured = true;
  if (changed) paint();
}
void ls_closed(void*, zwlr_layer_surface_v1*) { g.running = false; }
const zwlr_layer_surface_v1_listener kLayerListener = {ls_configure, ls_closed};

// ---- registry ----
void reg_global(void*, wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
  if (!std::strcmp(iface, wl_compositor_interface.name)) {
    g.compositor = static_cast<wl_compositor*>(
        wl_registry_bind(r, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (!std::strcmp(iface, wl_shm_interface.name)) {
    g.shm = static_cast<wl_shm*>(wl_registry_bind(r, name, &wl_shm_interface, 1));
  } else if (!std::strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
    g.layer_shell = static_cast<zwlr_layer_shell_v1*>(
        wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
  } else if (!std::strcmp(iface, wl_output_interface.name) && version >= 2) {
    auto* o = new Output;
    o->name = name;
    o->output = static_cast<wl_output*>(
        wl_registry_bind(r, name, &wl_output_interface, std::min(version, 3u)));
    wl_output_add_listener(o->output, &kOutputListener, o);
    g.outputs.push_back(o);
  }
}
void reg_remove(void*, wl_registry*, uint32_t name) {
  for (size_t i = 0; i < g.outputs.size(); ++i)
    if (g.outputs[i]->name == name) {
      for (size_t j = 0; j < g.entered.size(); ++j)
        if (g.entered[j] == g.outputs[i]->output) {
          g.entered.erase(g.entered.begin() + static_cast<long>(j));
          break;
        }
      wl_output_destroy(g.outputs[i]->output);
      delete g.outputs[i];
      g.outputs.erase(g.outputs.begin() + static_cast<long>(i));
      return;
    }
}
const wl_registry_listener kRegistryListener = {reg_global, reg_remove};

void reload_config() {
  g.theme = load_theme_config();
  g.config = load_wallpaper_config();
}

int open_config_watch() {
  const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (fd < 0) return -1;
  // Watch the directories (editors and save_*_config() replace files via
  // rename, which a watch on the file itself would lose track of).
  std::filesystem::path dirs[2] = {
      std::filesystem::path(wallpaper_user_config_path()).parent_path(),
      std::filesystem::path(user_config_path()).parent_path()};
  for (const auto& d : dirs) {
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    inotify_add_watch(fd, d.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
  }
  return fd;
}

}  // namespace

int main() {
  fleetwm::block_quit_signals();  // before any thread exists, see quit_signals.hpp
  fleetwm::tune_malloc_for_low_rss();

  reload_config();

  g.display = wl_display_connect(nullptr);
  if (!g.display) {
    std::fprintf(stderr, "fleetwm-wallpaper: cannot connect to the Wayland display\n");
    return 1;
  }
  g.registry = wl_display_get_registry(g.display);
  wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
  wl_display_roundtrip(g.display);
  if (!g.compositor || !g.shm || !g.layer_shell) {
    std::fprintf(stderr, "fleetwm-wallpaper: compositor lacks wl_compositor/wl_shm/wlr-layer-shell\n");
    return 1;
  }

  g.surface = wl_compositor_create_surface(g.compositor);
  wl_surface_add_listener(g.surface, &kSurfaceListener, nullptr);
  g.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      g.layer_shell, g.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
      "fleetwm-wallpaper");
  zwlr_layer_surface_v1_add_listener(g.layer_surface, &kLayerListener, nullptr);
  zwlr_layer_surface_v1_set_anchor(g.layer_surface,
                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_size(g.layer_surface, 0, 0);
  // A wallpaper never reserves space; tiled windows render over it.
  zwlr_layer_surface_v1_set_exclusive_zone(g.layer_surface, 0);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      g.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  wl_surface_commit(g.surface);

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  const int ifd = open_config_watch();

  while (g.running) {
    wl_display_dispatch_pending(g.display);
    if (wl_display_flush(g.display) < 0 && errno != EAGAIN) break;

    pollfd fds[3] = {{wl_display_get_fd(g.display), POLLIN, 0}, {sfd, POLLIN, 0}, {ifd, POLLIN, 0}};
    if (wl_display_prepare_read(g.display) != 0) continue;
    if (poll(fds, 3, -1) < 0 && errno != EINTR) {
      wl_display_cancel_read(g.display);
      break;
    }
    if (fds[0].revents & POLLIN) {
      if (wl_display_read_events(g.display) < 0) break;
    } else {
      wl_display_cancel_read(g.display);
    }
    if (fds[0].revents & (POLLERR | POLLHUP)) break;
    if (fds[1].revents & POLLIN) g.running = false;
    if (fds[2].revents & POLLIN) {
      char buf[4096];
      while (read(ifd, buf, sizeof buf) > 0) {
      }
      reload_config();
      paint();
    }
  }

  if (g.layer_surface) zwlr_layer_surface_v1_destroy(g.layer_surface);
  if (g.surface) wl_surface_destroy(g.surface);
  wl_display_disconnect(g.display);
  return 0;
}
