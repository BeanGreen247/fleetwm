#include "view.hpp"

#include <algorithm>

#include <cstring>
#include <vector>

#include "output.hpp"
#include "server.hpp"
#include "titlebar.hpp"
#include "workspace.hpp"

namespace fleetwm {

namespace {

// Pinned/pinned+focused colors and thickness are themeable
// (ThemeConfig::pinned_border_color/pinned_focused_border_color/
// pinned_border_thickness_px, same as the plain focus border below),
// read live off server->theme_config(). focus_border_color is
// intentionally separate from ThemeConfig::accent (used for UI chrome
// everywhere else -- bar, launcher, settings) so the focused-window
// border reads as a distinct signal instead of blending into whatever
// else on screen already uses the accent color. One border-rect set
// renders whichever of these applies, picked by priority in
// resize_border() -- pinned+focused gets its own distinct color so it
// doesn't read as merely "pinned" or merely "focused".
constexpr float kNoBorderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // fully transparent

}  // namespace

int View::border_thickness() const {
  if (fullscreen) {
    return 0;  // a border would break the "exact output match" scanout
               // precondition (see the fullscreen field's doc comment,
               // view.hpp), and no one wants a focus ring around a game.
  }
  if (desktop_mode()) {
    // Desktop layout: no focus/pin highlight ring (the titlebar shows both); a window with a titlebar
    // gets the glass or flat frame instead.
    return geom::frame_thickness(server->theme_config().titlebar.frame_px, true, wants_titlebar(), fullscreen, maximized);
  }
  if (pinned) {
    return server->theme_config().pinned_border_thickness_px;
  }
  if (focused) {
    return server->theme_config().focus_border_thickness_px;
  }
  return 0;
}

// The top edge of the border: none in the Desktop layout (the titlebar stands on its own, the frame is the sides and the
// bottom), the same as the sides in the Tiling layout's focus ring.
int View::top_border() const { return desktop_mode() ? 0 : border_thickness(); }

View::View(Server* server_, Kind kind_) : server(server_), kind(kind_) { tag.view = this; }

// Listener cleanup lives in xdg_toplevel_destroy() (server.cpp), called
// explicitly BEFORE this View is erased -- not here in the destructor.
// See that function's comment for why ordering matters (matches
// wlroots' own tinywl.c reference pattern: remove every listener first,
// only then destroy the scene tree / free the object).
View::~View() = default;

void View::set_pinned(bool pinned_) {
  if (pinned == pinned_) {
    return;
  }
  pinned = pinned_;
  resize_border();

  update_stacking_layer();
}

void View::update_size_policy() {
  if (kind != Kind::XdgToplevel || !xdg_toplevel || !xdg_toplevel->base->initialized) {
    return;
  }
  const bool client_sized = floating || always_on_top || xdg_toplevel->parent != nullptr || fullscreen;
  wlr_xdg_toplevel_set_tiled(xdg_toplevel, client_sized ? WLR_EDGE_NONE : WLR_EDGE_LEFT | WLR_EDGE_RIGHT |
                                                                             WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
}

void View::update_stacking_layer() {
  wlr_scene_tree* parent = fullscreen        ? server->layer_fullscreen()
                           : always_on_top   ? server->layer_topmost()
                           : pinned          ? server->layer_pinned()
                                             : server->layer_toplevels();
  wlr_scene_node_reparent(&container_tree->node, parent);
  if (fullscreen || always_on_top || pinned) {
    wlr_scene_node_raise_to_top(&container_tree->node);
  }
}

void View::set_floating(bool floating_) {
  if (floating == floating_) {
    return;
  }
  floating = floating_;
  update_size_policy();
}

void View::set_fullscreen(bool fullscreen_) {
  if (fullscreen == fullscreen_) {
    return;
  }
  fullscreen = fullscreen_;
  update_size_policy();

  if (output == nullptr) {
    // Client requested fullscreen before ever mapping -- confirmed via
    // real testing with `foot --fullscreen`, which sends
    // xdg_toplevel.set_fullscreen() immediately at startup, well before
    // its first real (non-initial) commit. `fullscreen` above is already
    // updated, so this isn't lost: xdg_toplevel_map (server.cpp) checks
    // it and re-invokes this method once `output` is actually set,
    // before that same call's relayout() runs (so relayout() already
    // sees fullscreen==true and skips tiling this view -- no
    // tiled-then-corrected flash).
    return;
  }

  if (kind == Kind::XdgToplevel && xdg_toplevel) {
    wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, fullscreen);
  }
#if FLEETWM_XWAYLAND
  if (kind == Kind::XWayland && xwayland_surface) {
    wlr_xwayland_surface_set_fullscreen(xwayland_surface, fullscreen);
  }
#endif

  if (!fullscreen) {
    // Reparent back under normal toplevels and let the usual tiling
    // machinery pick the view back up -- resize_border() will restore
    // real border dimensions once the client's shrink-back-down commit
    // lands (xdg_toplevel_surface_commit, server.cpp), same as any other
    // resize.
    update_stacking_layer();
    if (desktop_mode() && is_window()) {
      // Free-floating windows go back to where (and how big) they were.
      wlr_scene_node_set_position(&container_tree->node, pre_fullscreen_box.x, pre_fullscreen_box.y);
      if (pre_fullscreen_box.width > 0 && pre_fullscreen_box.height > 0) {
        request_size(pre_fullscreen_box.width, pre_fullscreen_box.height);
        last_requested_content_w = pre_fullscreen_box.width;
        last_requested_content_h = pre_fullscreen_box.height;
      }
      resize_border();
      return;
    }
    output->relayout();
    return;
  }

  if (desktop_mode() && is_window()) {
    const wlr_box geo = content_geometry();
    pre_fullscreen_box = {container_tree->node.x, container_tree->node.y, geo.width, geo.height};
  }

  wlr_box output_box{};
  wlr_output_layout_get_box(server->output_layout(), output->wlr_output_ptr, &output_box);

  wlr_scene_node_reparent(&container_tree->node, server->layer_fullscreen());
  wlr_scene_node_raise_to_top(&container_tree->node);
  wlr_scene_node_set_position(&container_tree->node, output_box.x, output_box.y);

  if (is_window()) {
    int w = std::max(1, output_box.width);
    int h = std::max(1, output_box.height);
    if (w != last_requested_content_w || h != last_requested_content_h) {
      request_size(w, h);
      last_requested_content_w = w;
      last_requested_content_h = h;
    }
  }
  resize_border();  // thickness is 0 while fullscreen; zeroes the rects now
                     // rather than waiting on the client's resize commit
}

void View::resize_border() {
  int thickness = border_thickness();
  const Server::BorderColors& colors = server->border_colors();
  const float* color = desktop_mode()      ? kNoBorderColor  // the frame is drawn by update_frame()
                       : pinned && focused ? colors.pinned_focused
                       : pinned          ? colors.pinned
                       : focused         ? colors.focus
                                         : kNoBorderColor;
  wlr_scene_rect_set_color(border_top, color);
  wlr_scene_rect_set_color(border_bottom, color);
  wlr_scene_rect_set_color(border_left, color);
  wlr_scene_rect_set_color(border_right, color);

  wlr_box geo{};
  if (is_window()) {
    geo = content_geometry();
  }
  int width = geo.width > 0 ? geo.width : 1;
  int height = geo.height > 0 ? geo.height : 1;

  // Content position/size never depends on grow_* -- always the plain
  // thickness offset, regardless of whether this view is currently
  // "stepped forward" (Output::relayout()). Only the border rects below
  // bleed outward into that extra space.
  content_w = width;
  const int th = titlebar_height();
  const int top = top_border();  // 0 in the Desktop layout: the titlebar is not framed, only the sides and the bottom are
  wlr_scene_node_set_position(&scene_tree->node, thickness, top + th);

  int top_h = top + grow_top;
  int bottom_h = thickness + grow_bottom;
  int left_w = thickness + grow_left;
  int right_w = thickness + grow_right;

  wlr_scene_rect_set_size(border_top, width + left_w + right_w, top_h);
  wlr_scene_node_set_position(&border_top->node, -grow_left, -grow_top);

  wlr_scene_rect_set_size(border_bottom, width + left_w + right_w, bottom_h);
  wlr_scene_node_set_position(&border_bottom->node, -grow_left, top + th + height);

  wlr_scene_rect_set_size(border_left, left_w, height + th + top_h + bottom_h);
  wlr_scene_node_set_position(&border_left->node, -grow_left, -grow_top);

  wlr_scene_rect_set_size(border_right, right_w, height + th + top_h + bottom_h);
  wlr_scene_node_set_position(&border_right->node, thickness + width, -grow_top);

  // Backdrop for windows that own a fixed slot (see fill_rect).
  if (fill_rect) {
    bool on = false;
    geom::Box slot{};
    if (desktop_mode() && !fullscreen && output) {
      const wlr_box a = output->usable_area;
      if (maximized) {
        slot = {0, 0, a.width, a.height};
        on = true;
      } else if (snap_zone != geom::SnapZone::None) {
        const geom::Box b = geom::snap_box(snap_zone, {a.x, a.y, a.width, a.height});
        slot = {0, 0, b.w, b.h};
        on = true;
      } else if (has_placed) {
        const std::vector<geom::Box> boxes =
            geom::tile_boxes({a.x, a.y, a.width, a.height}, placed_count);
        if (placed_index < boxes.size()) {
          slot = {0, 0, boxes[placed_index].w, boxes[placed_index].h};
          on = true;
        }
      }
    }
    wlr_scene_node_set_enabled(&fill_rect->node, on);
    if (on) {
      float color[4];
      titlebar_backdrop_color(color);
      wlr_scene_rect_set_color(fill_rect, color);
      wlr_scene_rect_set_size(fill_rect, slot.w, slot.h);
      wlr_scene_node_set_position(&fill_rect->node, 0, 0);
    }
  }

  // Invisible resize ring around the whole window (Desktop layout only).
  if (grab_rect) {
    constexpr int kRing = 6;
    const bool on = desktop_mode() && !fullscreen;
    wlr_scene_node_set_enabled(&grab_rect->node, on);
    if (on) {
      wlr_scene_rect_set_size(grab_rect, width + 2 * thickness + 2 * kRing,
                              height + th + top + thickness + 2 * kRing);
      wlr_scene_node_set_position(&grab_rect->node, -kRing, -kRing);
    }
  }
  update_titlebar();
  update_frame(height);
}

bool View::desktop_mode() const {
  return server->theme_config().window_layout == WindowLayout::Desktop;
}

bool View::wants_titlebar() const {
  if (!desktop_mode() || fullscreen || !is_window()) {
    return false;
  }
#if FLEETWM_XWAYLAND
  if (kind == Kind::XWayland) {
    // X11 programs expect the window manager to decorate them, unless they ask for none.
    return !(xwayland_surface->decorations & WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE);
  }
#endif
  if (has_decoration) {
    return true;
  }
  // fleetwm's own clients (fleetkit) draw no decorations and do not use
  // xdg-decoration, so they are recognised by app id instead.
  return xdg_toplevel->app_id && std::strncmp(xdg_toplevel->app_id, "dev.fleetwm.", 12) == 0;
}

int View::titlebar_height() const {
  return wants_titlebar() ? std::max(16, server->theme_config().titlebar.height) : 0;
}

void View::update_titlebar() {
  if (!wants_titlebar() || content_w <= 0) {
    if (titlebar) {
      wlr_scene_node_set_enabled(&titlebar->node, false);
    }
    return;
  }
  const int thickness = border_thickness();
  if (!titlebar) {
    titlebar = wlr_scene_buffer_create(container_tree, nullptr);
    titlebar->node.data = &tag;
  }
  wlr_scene_node_set_enabled(&titlebar->node, true);
  wlr_scene_node_set_position(&titlebar->node, 0, 0);  // the bar spans the whole window width; the frame starts below it

  // resize_border() runs on every client commit, so compare without
  // allocating: only build a std::string when something actually changed.
  const char* title = window_title() ? window_title() : window_app_id();
  if (!title) title = "";
  const int height = titlebar_height();
  const int bar_w = content_w + 2 * thickness;  // as wide as the window, over the frame's sides
  if (bar_w == titlebar_w_ && height == titlebar_h_ && focused == rendered_.focused &&
      maximized == rendered_.maximized && pinned == rendered_.pinned &&
      hover_button == rendered_.hover_button && pressed_button == rendered_.pressed_button && rendered_.title == title &&
      server->theme_config().glass == rendered_.glass && round_corners() == rendered_.round_corners) {
    return;
  }
  titlebar_w_ = bar_w;
  titlebar_h_ = height;
  rendered_ = {title, focused, maximized, pinned, hover_button, pressed_button, server->theme_config().glass, round_corners()};
  if (wlr_buffer* buffer = render_titlebar(bar_w, rendered_, server->theme_config().titlebar)) {
    wlr_scene_buffer_set_buffer(titlebar, buffer);
    wlr_buffer_drop(buffer);
  }
}

void View::update_frame(int content_h) {
  const int bt = border_thickness();
  const bool on = bt > 0 && wants_titlebar() && content_w > 0 && content_h > 0;
  wlr_scene_buffer* strips[3] = {frame_left_, frame_right_, frame_bottom_};
  if (!on) {
    for (wlr_scene_buffer* s : strips)
      if (s) wlr_scene_node_set_enabled(&s->node, false);
    frame_key_ = {};
    return;
  }
  wlr_scene_buffer** slots[3] = {&frame_left_, &frame_right_, &frame_bottom_};
  for (wlr_scene_buffer** slot : slots) {
    if (!*slot) {
      *slot = wlr_scene_buffer_create(container_tree, nullptr);
      (*slot)->node.data = &tag;  // part of the window's decoration: clicks on it resize
    }
    wlr_scene_node_set_enabled(&(*slot)->node, true);
  }
  const int th = titlebar_height();
  const int y = top_border() + th;  // the sides and the bottom start under the titlebar
  wlr_scene_node_set_position(&frame_left_->node, 0, y);
  wlr_scene_node_set_position(&frame_right_->node, bt + content_w, y);
  wlr_scene_node_set_position(&frame_bottom_->node, 0, y + content_h);

  const bool glass = server->theme_config().glass;
  const bool round = round_corners();
  const FrameKey key{content_w, content_h, th, bt, focused, glass, round};
  if (key == frame_key_) {
    return;
  }
  frame_key_ = key;
  const int w = content_w + 2 * bt;
  const int dims[3][2] = {{bt, content_h}, {bt, content_h}, {w, bt}};
  for (int i = 0; i < 3; ++i) {
    if (wlr_buffer* buffer = render_frame_strip(dims[i][0], dims[i][1], i, focused, glass, round)) {
      wlr_scene_buffer_set_buffer(*slots[i], buffer);
      wlr_buffer_drop(buffer);
    }
  }
}

bool View::round_corners() const {
  return server->theme_config().corner_style == CornerStyle::Rounded && !maximized && snap_zone == geom::SnapZone::None;
}

void View::set_pressed_button(int button) {
  if (pressed_button == button) {
    return;
  }
  pressed_button = button;
  update_titlebar();
}

void View::set_hover_button(int button) {
  if (hover_button == button) {
    return;
  }
  hover_button = button;
  update_titlebar();
}

void View::snap_to(geom::SnapZone zone) {
  if (zone == geom::SnapZone::None || !output || !is_window()) {
    return;
  }
  if (zone == geom::SnapZone::Maximize) {
    set_maximized(true);
    return;
  }
  if (maximized) {
    set_maximized(false);
  }
  if (snap_zone == geom::SnapZone::None) {
    const wlr_box geo = content_geometry();
    restore_box = {container_tree->node.x, container_tree->node.y, geo.width, geo.height};
  }
  snap_zone = zone;
  refit_snapped();
}

void View::place_tile(size_t index, size_t count) {
  if (!output || !is_window() || count == 0 || index >= count) {
    return;
  }
  snap_zone = geom::SnapZone::None;  // an explicit placement replaces any earlier snap
  placed_index = index;
  placed_count = count;
  has_placed = true;
  refit_placed();
}

void View::refit_placed() {
  if (!has_placed || !output || !is_window()) {
    return;
  }
  const wlr_box a = output->usable_area;
  const std::vector<geom::Box> boxes = geom::tile_boxes({a.x, a.y, a.width, a.height}, placed_count);
  if (placed_index >= boxes.size()) {
    return;
  }
  const geom::Box& outer = boxes[placed_index];
  const int bt = border_thickness();
  wlr_scene_node_set_position(&container_tree->node, outer.x, outer.y);
  const int w = std::max(1, outer.w - 2 * bt), h = std::max(1, outer.h - titlebar_height() - bt - top_border());
  request_size(w, h);
  last_requested_content_w = w;
  last_requested_content_h = h;
  resize_border();
}

void View::refit_snapped() {
  if (snap_zone == geom::SnapZone::None || !output || !is_window()) {
    return;
  }
  const wlr_box a = output->usable_area;
  const geom::Box outer = geom::snap_box(snap_zone, {a.x, a.y, a.width, a.height});
  const int bt = border_thickness();
  wlr_scene_node_set_position(&container_tree->node, outer.x, outer.y);
  const int w = std::max(1, outer.w - 2 * bt), h = std::max(1, outer.h - titlebar_height() - bt - top_border());
  request_size(w, h);
  last_requested_content_w = w;
  last_requested_content_h = h;
  resize_border();
}

void View::restore_from_snap() {
  // A window maximized out of a half still remembers that half; restoring must go all the way
  // back to the floating size, not just to the half.
  if (maximized) set_maximized(false);
  if (snap_zone == geom::SnapZone::None || !is_window()) {
    return;
  }
  snap_zone = geom::SnapZone::None;
  wlr_scene_node_set_position(&container_tree->node, restore_box.x, restore_box.y);
  const int w = std::max(1, restore_box.width), h = std::max(1, restore_box.height);
  request_size(w, h);
  last_requested_content_w = w;
  last_requested_content_h = h;
  resize_border();
}

void View::refit_maximized() {
  if (!maximized || !output || !is_window()) {
    return;
  }
  const int bt = border_thickness();
  const wlr_box area = output->usable_area;
  wlr_scene_node_set_position(&container_tree->node, area.x, area.y);
  request_size(std::max(1, area.width - 2 * bt), std::max(1, area.height - titlebar_height() - bt - top_border()));
}

void View::set_minimized(bool want) {
  if (minimized == want || !workspace) {
    return;
  }
  minimized = want;
  if (want) {
    wlr_scene_node_set_enabled(&container_tree->node, false);
    if (server->grab_view() == this) {
      server->end_grab();
    }
    if (server->seat()->keyboard_state.focused_surface == surface()) {
      server->focus_next_after(this);
    }
  } else {
    wlr_scene_node_set_enabled(&container_tree->node, true);
    if (output) {
      output->relayout();
    }
    server->focus_view(this);
  }
  server->schedule_windows_broadcast();
}

void View::set_maximized(bool want) {
  if (maximized == want || !output || !is_window()) {
    return;
  }
  const int th = titlebar_height();
  if (want) {
    // Out of a half, restore_box already holds the floating size; keep it.
    if (snap_zone == geom::SnapZone::None) {
      const wlr_box geo = content_geometry();
      restore_box = {container_tree->node.x, container_tree->node.y, geo.width, geo.height};
    }
    maximized = true;
    const int bt = border_thickness();  // 0 once maximized: the window fills the work area
    const wlr_box area = output->usable_area;
    wlr_scene_node_set_position(&container_tree->node, area.x, area.y);
    request_size(std::max(1, area.width - 2 * bt), std::max(1, area.height - th - bt - top_border()));
  } else {
    maximized = false;
    wlr_scene_node_set_position(&container_tree->node, restore_box.x, restore_box.y);
    request_size(std::max(1, restore_box.width), std::max(1, restore_box.height));
  }
  if (kind == Kind::XdgToplevel) {
    wlr_xdg_toplevel_set_maximized(xdg_toplevel, maximized);
  }
#if FLEETWM_XWAYLAND
  if (kind == Kind::XWayland) {
    wlr_xwayland_surface_set_maximized(xwayland_surface, maximized);
  }
#endif
  resize_border();
}

wlr_surface* View::surface() const {
  if (kind == Kind::XdgToplevel) {
    return xdg_toplevel ? xdg_toplevel->base->surface : nullptr;
  }
#if FLEETWM_XWAYLAND
  return xwayland_surface ? xwayland_surface->surface : nullptr;
#else
  return nullptr;
#endif
}

void View::focus() {
  server->focus_view(this);
}

void View::close() {
  if (kind == Kind::XdgToplevel && xdg_toplevel) {
    wlr_xdg_toplevel_send_close(xdg_toplevel);
    return;
  }
#if FLEETWM_XWAYLAND
  if (kind == Kind::XWayland && xwayland_surface) {
    wlr_xwayland_surface_close(xwayland_surface);
  }
#endif
}

// ---- toolkit-independent window access -------------------------------------

bool View::is_window() const {
  if (kind == Kind::XdgToplevel) return xdg_toplevel != nullptr;
#if FLEETWM_XWAYLAND
  return xwayland_surface != nullptr;
#else
  return false;
#endif
}

bool View::is_child_window() const {
  if (kind == Kind::XdgToplevel) return xdg_toplevel && xdg_toplevel->parent != nullptr;
#if FLEETWM_XWAYLAND
  return xwayland_surface && xwayland_surface->parent != nullptr;
#else
  return false;
#endif
}

wlr_box View::content_geometry() const {
  wlr_box geo{};
  if (kind == Kind::XdgToplevel) {
    if (xdg_toplevel) wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geo);
    return geo;
  }
#if FLEETWM_XWAYLAND
  if (xwayland_surface) {
    wlr_surface* s = xwayland_surface->surface;
    geo.width = s && s->current.width > 0 ? s->current.width : xwayland_surface->width;
    geo.height = s && s->current.height > 0 ? s->current.height : xwayland_surface->height;
  }
#endif
  return geo;
}

void View::request_size(int w, int h) {
  if (kind == Kind::XdgToplevel) {
    if (xdg_toplevel) wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
    return;
  }
#if FLEETWM_XWAYLAND
  if (xwayland_surface) {
    int cx = 0, cy = 0;
    wlr_scene_node_coords(&container_tree->node, &cx, &cy);
    const int bt = border_thickness();
    x_sent_x = cx + bt;
    x_sent_y = cy + top_border() + titlebar_height();
    x_sent_w = std::max(1, w);
    x_sent_h = std::max(1, h);
    wlr_xwayland_surface_configure(xwayland_surface, static_cast<int16_t>(x_sent_x), static_cast<int16_t>(x_sent_y),
                                   static_cast<uint16_t>(x_sent_w), static_cast<uint16_t>(x_sent_h));
  }
#else
  (void)w;
  (void)h;
#endif
}

void View::sync_x11_position() {
#if FLEETWM_XWAYLAND
  if (kind != Kind::XWayland || !xwayland_surface || !workspace) return;
  int cx = 0, cy = 0;
  wlr_scene_node_coords(&container_tree->node, &cx, &cy);
  const int bt = border_thickness();
  const int x = cx + bt, y = cy + top_border() + titlebar_height();
  if (x == x_sent_x && y == x_sent_y) return;
  const wlr_box geo = content_geometry();
  x_sent_x = x;
  x_sent_y = y;
  wlr_xwayland_surface_configure(xwayland_surface, static_cast<int16_t>(x), static_cast<int16_t>(y),
                                 static_cast<uint16_t>(std::max(1, x_sent_w > 0 ? x_sent_w : geo.width)),
                                 static_cast<uint16_t>(std::max(1, x_sent_h > 0 ? x_sent_h : geo.height)));
#endif
}

void View::set_activated(bool activated) {
  if (kind == Kind::XdgToplevel) {
    if (xdg_toplevel) wlr_xdg_toplevel_set_activated(xdg_toplevel, activated);
    return;
  }
#if FLEETWM_XWAYLAND
  if (xwayland_surface) {
    wlr_xwayland_surface_activate(xwayland_surface, activated);
    if (activated) wlr_xwayland_surface_restack(xwayland_surface, nullptr, XCB_STACK_MODE_ABOVE);
  }
#else
  (void)activated;
#endif
}

const char* View::window_title() const {
  if (kind == Kind::XdgToplevel) return xdg_toplevel ? xdg_toplevel->title : nullptr;
#if FLEETWM_XWAYLAND
  // X11 programs often set an empty title; treat that as none so the class shows instead.
  return xwayland_surface && xwayland_surface->title && *xwayland_surface->title ? xwayland_surface->title : nullptr;
#else
  return nullptr;
#endif
}

const char* View::window_app_id() const {
  if (kind == Kind::XdgToplevel) return xdg_toplevel ? xdg_toplevel->app_id : nullptr;
#if FLEETWM_XWAYLAND
  return xwayland_surface ? xwayland_surface->class_ ? xwayland_surface->class_ : nullptr : nullptr;
#else
  return nullptr;
#endif
}

void create_view_rects(View* view) {
  constexpr float kTransparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  view->border_top = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->border_bottom = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->border_left = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->border_right = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);

  // Invisible, tagged ring around the window that acts as the resize handles
  // in the Desktop layout; enabled/sized by View::resize_border().
  // The border rects are part of the resize handle too (they sit between the
  // content and the ring), so they carry the same tag.
  for (wlr_scene_rect* border : {view->border_top, view->border_bottom, view->border_left, view->border_right}) {
    border->node.data = &view->tag;
  }
  view->fill_rect = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->fill_rect->node.data = &view->tag;
  wlr_scene_node_lower_to_bottom(&view->fill_rect->node);
  wlr_scene_node_set_enabled(&view->fill_rect->node, false);
  view->grab_rect = wlr_scene_rect_create(view->container_tree, 0, 0, kTransparent);
  view->grab_rect->node.data = &view->tag;
  wlr_scene_node_lower_to_bottom(&view->grab_rect->node);
  wlr_scene_node_set_enabled(&view->grab_rect->node, false);
}

}  // namespace fleetwm
