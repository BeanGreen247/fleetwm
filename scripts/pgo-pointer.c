/*
 * A tiny virtual pointer for the PGO training run (scripts/pgo-train-session.sh): moves, presses, drags and scrolls
 * through the compositor's wlr-virtual-pointer protocol, which `wlrctl` cannot do (it only moves and clicks). That
 * is what lets the training drag windows by their titlebar, resize them by their frame, snap them to the screen
 * edges and click the caption buttons. Built on the fly by the training script with wayland-scanner and cc; if that
 * fails the training simply skips the pointer part.
 *
 *   pgo-pointer [command ...]      commands (x, y in output pixels, PGO_W x PGO_H is the output size):
 *     abs X Y          move to X Y            down | up          press / release the left button
 *     click X Y        move, press, release   rclick X Y         the same with the right button
 *     dclick X Y       double click           drag X0 Y0 X1 Y1 N press at 0, move in N steps, release at 1
 *     scroll V         wheel (positive = down) wait MS           pause
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1* manager;
static struct wl_seat* seat;
static struct zwlr_virtual_pointer_v1* ptr;
static uint32_t out_w = 1280, out_h = 720;

static void global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
  (void)d;
  if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
    manager = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, version < 2 ? version : 2);
  else if (!strcmp(iface, "wl_seat") && !seat)
    seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
}
static void global_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg = {global, global_remove};

static uint32_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static void settle(struct wl_display* d, int ms) {
  wl_display_flush(d);
  if (ms > 0) usleep((useconds_t)ms * 1000);
}

static void move(struct wl_display* d, int x, int y) {
  zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(), (uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y), out_w, out_h);
  zwlr_virtual_pointer_v1_frame(ptr);
  settle(d, 6);
}
static void button(struct wl_display* d, uint32_t code, int down) {
  zwlr_virtual_pointer_v1_button(ptr, now_ms(), code, down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
  zwlr_virtual_pointer_v1_frame(ptr);
  settle(d, 14);
}

int main(int argc, char** argv) {
  if (getenv("PGO_W")) out_w = (uint32_t)atoi(getenv("PGO_W"));
  if (getenv("PGO_H")) out_h = (uint32_t)atoi(getenv("PGO_H"));
  struct wl_display* d = wl_display_connect(NULL);
  if (!d) return 2;
  struct wl_registry* r = wl_display_get_registry(d);
  wl_registry_add_listener(r, &reg, NULL);
  wl_display_roundtrip(d);
  if (!manager) return 3;
  ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);
  wl_display_roundtrip(d);
  const uint32_t BTN_LEFT = 0x110, BTN_RIGHT = 0x111;
  for (int i = 1; i < argc; ++i) {
    const char* c = argv[i];
    if (!strcmp(c, "abs") && i + 2 < argc) { move(d, atoi(argv[i + 1]), atoi(argv[i + 2])); i += 2; }
    else if (!strcmp(c, "down")) button(d, BTN_LEFT, 1);
    else if (!strcmp(c, "up")) button(d, BTN_LEFT, 0);
    else if ((!strcmp(c, "click") || !strcmp(c, "rclick") || !strcmp(c, "dclick")) && i + 2 < argc) {
      const uint32_t b = c[0] == 'r' ? BTN_RIGHT : BTN_LEFT;
      move(d, atoi(argv[i + 1]), atoi(argv[i + 2]));
      button(d, b, 1);
      button(d, b, 0);
      if (c[0] == 'd') { button(d, b, 1); button(d, b, 0); }
      i += 2;
    } else if (!strcmp(c, "drag") && i + 5 < argc) {
      const int x0 = atoi(argv[i + 1]), y0 = atoi(argv[i + 2]), x1 = atoi(argv[i + 3]), y1 = atoi(argv[i + 4]);
      int n = atoi(argv[i + 5]);
      if (n < 1) n = 1;
      move(d, x0, y0);
      button(d, BTN_LEFT, 1);
      for (int s = 1; s <= n; ++s) move(d, x0 + (x1 - x0) * s / n, y0 + (y1 - y0) * s / n);
      button(d, BTN_LEFT, 0);
      i += 5;
    } else if (!strcmp(c, "scroll") && i + 1 < argc) {
      zwlr_virtual_pointer_v1_axis_source(ptr, WL_POINTER_AXIS_SOURCE_WHEEL);
      zwlr_virtual_pointer_v1_axis(ptr, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(atoi(argv[i + 1])));
      zwlr_virtual_pointer_v1_frame(ptr);
      settle(d, 20);
      i += 1;
    } else if (!strcmp(c, "wait") && i + 1 < argc) { settle(d, atoi(argv[i + 1])); i += 1; }
    else { fprintf(stderr, "pgo-pointer: bad command '%s'\n", c); return 4; }
  }
  wl_display_roundtrip(d);
  return 0;
}
