#include "view.hpp"

#include <algorithm>

#include <cstring>

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
// Fallback if a theme_config() color string is somehow unparseable --
// should never actually be hit since every ThemeConfig color field's own
// default is a valid hex string, but parse_hex_color() leaves its output
// untouched on failure and this is what that untouched buffer starts as.
constexpr float kBorderColorFallback[4] = {0.9f, 0.9f, 0.95f, 1.0f};  // near-white

}  // namespace

int View::border_thickness() const {
  if (fullscreen) {
    return 0;  // a border would break the "exact output match" scanout
               // precondition (see the fullscreen field's doc comment,
               // view.hpp), and no one wants a focus ring around a game.
  }
  if (desktop_mode()) {
    return 0;  // Desktop layout: no focus/pin highlight ring (the titlebar shows both)
  }
  if (pinned) {
    return server->theme_config().pinned_border_thickness_px;
  }
  if (focused) {
    return server->theme_config().focus_border_thickness_px;
  }
  return 0;
}

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

  wlr_scene_node_reparent(&container_tree->node,
                           pinned ? server->layer_pinned() : server->layer_toplevels());
  if (pinned) {
    wlr_scene_node_raise_to_top(&container_tree->node);
  }
}

void View::set_floating(bool floating_) {
  if (floating == floating_) {
    return;
  }
  floating = floating_;
}

void View::set_fullscreen(bool fullscreen_) {
  if (fullscreen == fullscreen_) {
    return;
  }
  fullscreen = fullscreen_;

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

  if (!fullscreen) {
    // Reparent back under normal toplevels and let the usual tiling
    // machinery pick the view back up -- resize_border() will restore
    // real border dimensions once the client's shrink-back-down commit
    // lands (xdg_toplevel_surface_commit, server.cpp), same as any other
    // resize.
    wlr_scene_node_reparent(&container_tree->node, server->layer_toplevels());
    if (desktop_mode() && kind == Kind::XdgToplevel && xdg_toplevel) {
      // Free-floating windows go back to where (and how big) they were.
      wlr_scene_node_set_position(&container_tree->node, pre_fullscreen_box.x, pre_fullscreen_box.y);
      if (pre_fullscreen_box.width > 0 && pre_fullscreen_box.height > 0) {
        wlr_xdg_toplevel_set_size(xdg_toplevel, pre_fullscreen_box.width, pre_fullscreen_box.height);
        last_requested_content_w = pre_fullscreen_box.width;
        last_requested_content_h = pre_fullscreen_box.height;
      }
      resize_border();
      return;
    }
    output->relayout();
    return;
  }

  if (desktop_mode() && kind == Kind::XdgToplevel && xdg_toplevel) {
    wlr_box geo{};
    wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geo);
    pre_fullscreen_box = {container_tree->node.x, container_tree->node.y, geo.width, geo.height};
  }

  wlr_box output_box{};
  wlr_output_layout_get_box(server->output_layout(), output->wlr_output_ptr, &output_box);

  wlr_scene_node_reparent(&container_tree->node, server->layer_fullscreen());
  wlr_scene_node_raise_to_top(&container_tree->node);
  wlr_scene_node_set_position(&container_tree->node, output_box.x, output_box.y);

  if (kind == Kind::XdgToplevel && xdg_toplevel) {
    int w = std::max(1, output_box.width);
    int h = std::max(1, output_box.height);
    if (w != last_requested_content_w || h != last_requested_content_h) {
      wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
      last_requested_content_w = w;
      last_requested_content_h = h;
    }
  }
  resize_border();  // thickness is 0 while fullscreen; zeroes the rects now
                     // rather than waiting on the client's resize commit
}

void View::resize_border() {
  int thickness = border_thickness();
  const float* color;
  float themed_color[4] = {kBorderColorFallback[0], kBorderColorFallback[1],
                            kBorderColorFallback[2], kBorderColorFallback[3]};
  if (pinned && focused) {
    parse_hex_color(server->theme_config().pinned_focused_border_color, themed_color);
    color = themed_color;
  } else if (pinned) {
    parse_hex_color(server->theme_config().pinned_border_color, themed_color);
    color = themed_color;
  } else if (focused) {
    parse_hex_color(server->theme_config().focus_border_color, themed_color);
    color = themed_color;
  } else {
    color = kNoBorderColor;
  }
  wlr_scene_rect_set_color(border_top, color);
  wlr_scene_rect_set_color(border_bottom, color);
  wlr_scene_rect_set_color(border_left, color);
  wlr_scene_rect_set_color(border_right, color);

  wlr_box geo{};
  if (kind == Kind::XdgToplevel && xdg_toplevel) {
    wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geo);
  }
  int width = geo.width > 0 ? geo.width : 1;
  int height = geo.height > 0 ? geo.height : 1;

  // Content position/size never depends on grow_* -- always the plain
  // thickness offset, regardless of whether this view is currently
  // "stepped forward" (Output::relayout()). Only the border rects below
  // bleed outward into that extra space.
  content_w = width;
  const int th = titlebar_height();
  wlr_scene_node_set_position(&scene_tree->node, thickness, thickness + th);

  int top_h = thickness + grow_top;
  int bottom_h = thickness + grow_bottom;
  int left_w = thickness + grow_left;
  int right_w = thickness + grow_right;

  wlr_scene_rect_set_size(border_top, width + left_w + right_w, top_h);
  wlr_scene_node_set_position(&border_top->node, -grow_left, -grow_top);

  wlr_scene_rect_set_size(border_bottom, width + left_w + right_w, bottom_h);
  wlr_scene_node_set_position(&border_bottom->node, -grow_left, thickness + th + height);

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
        slot = {0, 0, placed_outer.w, placed_outer.h};
        on = true;
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
                              height + th + 2 * thickness + 2 * kRing);
      wlr_scene_node_set_position(&grab_rect->node, -kRing, -kRing);
    }
  }
  update_titlebar();
}

bool View::desktop_mode() const {
  return server->theme_config().window_layout == WindowLayout::Desktop;
}

bool View::wants_titlebar() const {
  if (!desktop_mode() || fullscreen || kind != Kind::XdgToplevel || !xdg_toplevel) {
    return false;
  }
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
  wlr_scene_node_set_position(&titlebar->node, thickness, thickness);

  // resize_border() runs on every client commit, so compare without
  // allocating: only build a std::string when something actually changed.
  const char* title = xdg_toplevel->title ? xdg_toplevel->title : xdg_toplevel->app_id;
  if (!title) title = "";
  const int height = titlebar_height();
  if (content_w == titlebar_w_ && height == titlebar_h_ && focused == rendered_.focused &&
      maximized == rendered_.maximized && pinned == rendered_.pinned &&
      hover_button == rendered_.hover_button && rendered_.title == title) {
    return;
  }
  titlebar_w_ = content_w;
  titlebar_h_ = height;
  rendered_ = {title, focused, maximized, pinned, hover_button};
  if (wlr_buffer* buffer = render_titlebar(content_w, rendered_, server->theme_config().titlebar)) {
    wlr_scene_buffer_set_buffer(titlebar, buffer);
    wlr_buffer_drop(buffer);
  }
}

void View::set_hover_button(int button) {
  if (hover_button == button) {
    return;
  }
  hover_button = button;
  update_titlebar();
}

void View::snap_to(geom::SnapZone zone) {
  if (zone == geom::SnapZone::None || !output || kind != Kind::XdgToplevel || !xdg_toplevel) {
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
    wlr_box geo{};
    wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geo);
    restore_box = {container_tree->node.x, container_tree->node.y, geo.width, geo.height};
  }
  snap_zone = zone;
  refit_snapped();
}

void View::place_outer(const geom::Box& outer) {
  if (!output || !xdg_toplevel) {
    return;
  }
  snap_zone = geom::SnapZone::None;  // an explicit placement replaces any earlier snap
  placed_outer = outer;
  has_placed = true;
  const int bt = border_thickness();
  wlr_scene_node_set_position(&container_tree->node, outer.x, outer.y);
  const int w = std::max(1, outer.w - 2 * bt), h = std::max(1, outer.h - titlebar_height() - 2 * bt);
  wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
  last_requested_content_w = w;
  last_requested_content_h = h;
  resize_border();
}

void View::refit_snapped() {
  if (snap_zone == geom::SnapZone::None || !output || !xdg_toplevel) {
    return;
  }
  const wlr_box a = output->usable_area;
  const geom::Box outer = geom::snap_box(snap_zone, {a.x, a.y, a.width, a.height});
  const int bt = border_thickness();
  wlr_scene_node_set_position(&container_tree->node, outer.x, outer.y);
  const int w = std::max(1, outer.w - 2 * bt), h = std::max(1, outer.h - titlebar_height() - 2 * bt);
  wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
  last_requested_content_w = w;
  last_requested_content_h = h;
  resize_border();
}

void View::restore_from_snap() {
  if (maximized) {
    set_maximized(false);
    return;
  }
  if (snap_zone == geom::SnapZone::None || !xdg_toplevel) {
    return;
  }
  snap_zone = geom::SnapZone::None;
  wlr_scene_node_set_position(&container_tree->node, restore_box.x, restore_box.y);
  const int w = std::max(1, restore_box.width), h = std::max(1, restore_box.height);
  wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
  last_requested_content_w = w;
  last_requested_content_h = h;
  resize_border();
}

void View::refit_maximized() {
  if (!maximized || !output || !xdg_toplevel) {
    return;
  }
  const int bt = border_thickness();
  const wlr_box area = output->usable_area;
  wlr_scene_node_set_position(&container_tree->node, area.x, area.y);
  wlr_xdg_toplevel_set_size(xdg_toplevel, std::max(1, area.width - 2 * bt),
                            std::max(1, area.height - titlebar_height() - 2 * bt));
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
  if (maximized == want || !output || kind != Kind::XdgToplevel || !xdg_toplevel) {
    return;
  }
  const int th = titlebar_height();
  const int bt = border_thickness();
  if (want) {
    wlr_box geo{};
    wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geo);
    restore_box = {container_tree->node.x, container_tree->node.y, geo.width, geo.height};
    maximized = true;
    const wlr_box area = output->usable_area;
    wlr_scene_node_set_position(&container_tree->node, area.x, area.y);
    wlr_xdg_toplevel_set_size(xdg_toplevel, std::max(1, area.width - 2 * bt),
                              std::max(1, area.height - th - 2 * bt));
  } else {
    maximized = false;
    wlr_scene_node_set_position(&container_tree->node, restore_box.x, restore_box.y);
    wlr_xdg_toplevel_set_size(xdg_toplevel, std::max(1, restore_box.width),
                              std::max(1, restore_box.height));
  }
  wlr_xdg_toplevel_set_maximized(xdg_toplevel, maximized);
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

}  // namespace fleetwm
