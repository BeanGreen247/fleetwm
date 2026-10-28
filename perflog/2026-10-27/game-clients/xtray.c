// A fake Wine-style tray program: waits for the X11 system tray selection, docks a window and turns it green when it gets a button press
// (what the Rockstar launcher's tray icon does, minus the menu). Like Wine it uses the tray's 32-bit ARGB visual (_NET_SYSTEM_TRAY_VISUAL)
// when there is one and then paints only the left half of the icon red, leaving the right half transparent. Prints DOCKED, ARGB and CLICKED.
//   cc xtray.c -o xtray $(pkg-config --cflags --libs xcb)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

static xcb_atom_t atom(xcb_connection_t* c, const char* n) {
  xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, strlen(n), n), 0);
  xcb_atom_t a = r->atom; free(r); return a;
}

int main(void) {
  xcb_connection_t* c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c)) return 1;
  xcb_screen_t* s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
  xcb_atom_t sel = atom(c, "_NET_SYSTEM_TRAY_S0"), op = atom(c, "_NET_SYSTEM_TRAY_OPCODE");
  xcb_window_t owner = 0;
  for (int i = 0; i < 150 && !owner; ++i) {
    xcb_get_selection_owner_reply_t* r = xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, sel), 0);
    owner = r ? r->owner : 0; free(r);
    if (!owner) usleep(100000);
  }
  if (!owner) { puts("NO-TRAY"); return 2; }
  xcb_window_t w = xcb_generate_id(c);
  uint32_t visual = 0;
  xcb_get_property_reply_t* vr = xcb_get_property_reply(c, xcb_get_property(c, 0, owner, atom(c, "_NET_SYSTEM_TRAY_VISUAL"), XCB_ATOM_VISUALID, 0, 1), 0);
  if (vr && xcb_get_property_value_length(vr) >= 4) visual = *(uint32_t*)xcb_get_property_value(vr);
  free(vr);
  xcb_gcontext_t gc = xcb_generate_id(c);
  if (visual) {
    xcb_colormap_t cm = xcb_generate_id(c);
    xcb_create_colormap(c, XCB_COLORMAP_ALLOC_NONE, cm, s->root, visual);
    uint32_t v[] = {0, 0, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, cm};
    xcb_create_window(c, 32, w, s->root, 0, 0, 24, 24, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, v);
    uint32_t red = 0xffff0000;
    xcb_create_gc(c, gc, w, XCB_GC_FOREGROUND, &red);
    puts("ARGB");
  } else {
    uint32_t v[] = {0xff0000, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(c, XCB_COPY_FROM_PARENT, w, s->root, 0, 0, 24, 24, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, v);
  }
  xcb_client_message_event_t ev; memset(&ev, 0, sizeof ev);
  ev.response_type = XCB_CLIENT_MESSAGE; ev.format = 32; ev.window = owner; ev.type = op;
  ev.data.data32[0] = XCB_CURRENT_TIME; ev.data.data32[1] = 0; ev.data.data32[2] = w;
  xcb_send_event(c, 0, owner, XCB_EVENT_MASK_NO_EVENT, (const char*)&ev);
  xcb_flush(c);
  puts("DOCKED"); fflush(stdout);
  xcb_generic_event_t* e;
  while ((e = xcb_wait_for_event(c))) {
    if ((e->response_type & 0x7f) == XCB_EXPOSE && visual) {
      xcb_rectangle_t r = {0, 0, 12, 24};
      xcb_poly_fill_rectangle(c, w, gc, 1, &r);
      xcb_flush(c);
    } else if ((e->response_type & 0x7f) == XCB_BUTTON_PRESS) {
      printf("CLICKED %d\n", ((xcb_button_press_event_t*)e)->detail); fflush(stdout);
      uint32_t g = visual ? 0xff00ff00 : 0x00ff00;
      if (visual) {
        xcb_change_gc(c, gc, XCB_GC_FOREGROUND, &g);
        xcb_rectangle_t r = {0, 0, 12, 24};
        xcb_poly_fill_rectangle(c, w, gc, 1, &r);
      } else {
        xcb_change_window_attributes(c, w, XCB_CW_BACK_PIXEL, &g);
        xcb_clear_area(c, 1, w, 0, 0, 0, 0);
      }
      xcb_flush(c);
    }
    free(e);
  }
  return 0;
}
