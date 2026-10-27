// Test client: shows a 400x300 window, locks the pointer when it enters it and prints the relative motion it receives.
#define _GNU_SOURCE
#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "xdg-shell-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
static struct wl_compositor* comp; static struct wl_shm* shm; static struct xdg_wm_base* wm; static struct wl_seat* seat;
static struct zwp_pointer_constraints_v1* pc; static struct zwp_relative_pointer_manager_v1* rpm;
static struct wl_surface* surf; static struct wl_buffer* buf; static struct wl_pointer* ptr; static int locked_once;
static void reg(void* d, struct wl_registry* r, uint32_t id, const char* i, uint32_t v) {
  if (!strcmp(i, "wl_compositor")) comp = wl_registry_bind(r, id, &wl_compositor_interface, 4);
  else if (!strcmp(i, "wl_shm")) shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
  else if (!strcmp(i, "xdg_wm_base")) wm = wl_registry_bind(r, id, &xdg_wm_base_interface, 1);
  else if (!strcmp(i, "wl_seat") && !seat) seat = wl_registry_bind(r, id, &wl_seat_interface, 1);
  else if (!strcmp(i, "zwp_pointer_constraints_v1")) pc = wl_registry_bind(r, id, &zwp_pointer_constraints_v1_interface, 1);
  else if (!strcmp(i, "zwp_relative_pointer_manager_v1")) rpm = wl_registry_bind(r, id, &zwp_relative_pointer_manager_v1_interface, 1);
}
static void rem(void* d, struct wl_registry* r, uint32_t id) {}
static const struct wl_registry_listener rl = {reg, rem};
static void ping(void* d, struct xdg_wm_base* b, uint32_t s) { xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wml = {ping};
static void xs_conf(void* d, struct xdg_surface* x, uint32_t s) { xdg_surface_ack_configure(x, s); wl_surface_attach(surf, buf, 0, 0); wl_surface_commit(surf); }
static const struct xdg_surface_listener xsl = {xs_conf};
static void tl_conf(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* s) {}
static void tl_close(void* d, struct xdg_toplevel* t) { exit(0); }
static const struct xdg_toplevel_listener tll = {tl_conf, tl_close};
static void c_locked(void* d, struct zwp_locked_pointer_v1* l) { printf("LOCKED\n"); fflush(stdout); }
static void c_unlocked(void* d, struct zwp_locked_pointer_v1* l) { printf("UNLOCKED\n"); fflush(stdout); }
static const struct zwp_locked_pointer_v1_listener ll = {c_locked, c_unlocked};
static void r_motion(void* d, struct zwp_relative_pointer_v1* r, uint32_t hi, uint32_t lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t ux, wl_fixed_t uy) {
  printf("REL %d %d\n", wl_fixed_to_int(dx), wl_fixed_to_int(dy)); fflush(stdout);
}
static const struct zwp_relative_pointer_v1_listener rpl = {r_motion};
static void p_enter(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* sf, wl_fixed_t x, wl_fixed_t y) {
  printf("ENTER\n"); fflush(stdout);
  if (!locked_once) { locked_once = 1; struct zwp_locked_pointer_v1* l = zwp_pointer_constraints_v1_lock_pointer(pc, surf, p, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT); zwp_locked_pointer_v1_add_listener(l, &ll, NULL); }
}
static void p_leave(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* sf) { printf("LEAVE\n"); fflush(stdout); }
static void p_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) { printf("ABS %d %d\n", wl_fixed_to_int(x), wl_fixed_to_int(y)); fflush(stdout); }
static void p_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {}
static void p_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static const struct wl_pointer_listener pl = {p_enter, p_leave, p_motion, p_button, p_axis};
int main(void) {
  struct wl_display* dpy = wl_display_connect(NULL); if (!dpy) return 1;
  struct wl_registry* r = wl_display_get_registry(dpy); wl_registry_add_listener(r, &rl, NULL); wl_display_roundtrip(dpy);
  if (!pc || !rpm) { printf("NO PROTOCOLS pc=%p rpm=%p\n", (void*)pc, (void*)rpm); return 2; }
  xdg_wm_base_add_listener(wm, &wml, NULL);
  int fd = memfd_create("lock", 0); ftruncate(fd, 400 * 300 * 4);
  uint32_t* px = mmap(NULL, 400 * 300 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  for (int i = 0; i < 400 * 300; ++i) px[i] = 0xff208040;
  struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, 400 * 300 * 4);
  buf = wl_shm_pool_create_buffer(pool, 0, 400, 300, 400 * 4, WL_SHM_FORMAT_XRGB8888);
  surf = wl_compositor_create_surface(comp);
  struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm, surf); xdg_surface_add_listener(xs, &xsl, NULL);
  struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs); xdg_toplevel_add_listener(tl, &tll, NULL); xdg_toplevel_set_title(tl, "lock-test");
  ptr = wl_seat_get_pointer(seat); wl_pointer_add_listener(ptr, &pl, NULL);
  struct zwp_relative_pointer_v1* rp = zwp_relative_pointer_manager_v1_get_relative_pointer(rpm, ptr); zwp_relative_pointer_v1_add_listener(rp, &rpl, NULL);
  wl_surface_commit(surf);
  while (wl_display_dispatch(dpy) != -1) {}
  return 0;
}
