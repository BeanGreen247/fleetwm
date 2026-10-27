// Test client: a fullscreen xdg toplevel that ignores the configured size and shows a fixed 640x480 picture, like an old game.
#define _GNU_SOURCE
#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "xdg-shell-client-protocol.h"
static struct wl_compositor* comp; static struct wl_shm* shm; static struct xdg_wm_base* wm;
static void reg(void* d, struct wl_registry* r, uint32_t id, const char* i, uint32_t v) {
  if (!strcmp(i, "wl_compositor")) comp = wl_registry_bind(r, id, &wl_compositor_interface, 4);
  else if (!strcmp(i, "wl_shm")) shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
  else if (!strcmp(i, "xdg_wm_base")) wm = wl_registry_bind(r, id, &xdg_wm_base_interface, 1);
}
static void rem(void* d, struct wl_registry* r, uint32_t id) {}
static const struct wl_registry_listener rl = {reg, rem};
static void ping(void* d, struct xdg_wm_base* b, uint32_t s) { xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wl = {ping};
static struct wl_surface* surf; static struct wl_buffer* buf; static int configured;
static void xs_conf(void* d, struct xdg_surface* x, uint32_t s) {
  xdg_surface_ack_configure(x, s);
  if (!configured) { configured = 1; wl_surface_attach(surf, buf, 0, 0); wl_surface_damage(surf, 0, 0, 640, 480); wl_surface_commit(surf); }
}
static const struct xdg_surface_listener xsl = {xs_conf};
static void tl_conf(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* s) {}
static void tl_close(void* d, struct xdg_toplevel* t) { exit(0); }
static const struct xdg_toplevel_listener tll = {tl_conf, tl_close};
int main(void) {
  struct wl_display* dpy = wl_display_connect(NULL); if (!dpy) return 1;
  struct wl_registry* r = wl_display_get_registry(dpy); wl_registry_add_listener(r, &rl, NULL); wl_display_roundtrip(dpy);
  xdg_wm_base_add_listener(wm, &wl, NULL);
  int fd = memfd_create("fit", 0); ftruncate(fd, 640 * 480 * 4);
  uint32_t* px = mmap(NULL, 640 * 480 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) {
    int edge = x < 8 || y < 8 || x >= 632 || y >= 472, grid = (x / 32 + y / 32) & 1;
    px[y * 640 + x] = edge ? 0xffff3030 : grid ? 0xff2a6fdb : 0xff9ad0ff;
  }
  struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, 640 * 480 * 4);
  buf = wl_shm_pool_create_buffer(pool, 0, 640, 480, 640 * 4, WL_SHM_FORMAT_XRGB8888);
  surf = wl_compositor_create_surface(comp);
  struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm, surf); xdg_surface_add_listener(xs, &xsl, NULL);
  struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs); xdg_toplevel_add_listener(tl, &tll, NULL);
  xdg_toplevel_set_title(tl, "fit-test"); xdg_toplevel_set_app_id(tl, getenv("FIT_APP_ID") ? getenv("FIT_APP_ID") : "fit-test"); xdg_toplevel_set_fullscreen(tl, NULL);
  wl_surface_commit(surf);
  while (wl_display_dispatch(dpy) != -1) {}
  return 0;
}
