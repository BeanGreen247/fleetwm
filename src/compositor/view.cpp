#include "view.hpp"

#include <algorithm>

#include "output.hpp"
#include "server.hpp"
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

#if FLEETWM_SCENEFX
void set_buffer_radius(wlr_scene_buffer* buffer, int, int, void* data) {
  wlr_scene_buffer_set_corner_radius(buffer, *static_cast<int*>(data), CORNER_LOCATION_ALL);
}
#endif

}  // namespace

int View::border_thickness() const {
  if (fullscreen) {
    return 0;  // a border would break the "exact output match" scanout
               // precondition (see the fullscreen field's doc comment,
               // view.hpp), and no one wants a focus ring around a game.
  }
  if (pinned) {
    return server->theme_config().pinned_border_thickness_px;
  }
  if (focused) {
    return server->theme_config().focus_border_thickness_px;
  }
  return 0;
}

View::View(Server* server_, Kind kind_) : server(server_), kind(kind_) {}

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
    output->relayout();
    return;
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
  wlr_scene_node_set_position(&scene_tree->node, thickness, thickness);

  int top_h = thickness + grow_top;
  int bottom_h = thickness + grow_bottom;
  int left_w = thickness + grow_left;
  int right_w = thickness + grow_right;

#if FLEETWM_SCENEFX
  if (server->fx_enabled()) {
    // Rounded windows: the content buffers are clipped to a rounded rect, and
    // the border becomes ONE rounded rect behind them (ring = outer radius
    // content radius + thickness), so the four edge rects are switched off.
    // Fullscreen windows stay square (exact-output scanout).
    int radius = fullscreen ? 0 : server->theme_config().window_corner_radius;
    wlr_scene_node_for_each_buffer(&scene_tree->node, set_buffer_radius, &radius);
    const bool rounded = radius > 0;
    for (wlr_scene_rect* r : {border_bottom, border_left, border_right})
      wlr_scene_node_set_enabled(&r->node, !rounded);
    if (rounded) {
      const int outer_w = width + left_w + right_w, outer_h = height + top_h + bottom_h;
      const int outer_r = radius + thickness;
      wlr_scene_rect_set_size(border_top, outer_w, outer_h);
      wlr_scene_node_set_position(&border_top->node, -grow_left, -grow_top);
      wlr_scene_rect_set_corner_radius(border_top, outer_r, CORNER_LOCATION_ALL);
      wlr_scene_node_lower_to_bottom(&border_top->node);  // behind the content
      update_shadow(outer_w, outer_h, outer_r);
      return;
    }
    wlr_scene_rect_set_corner_radius(border_top, 0, CORNER_LOCATION_ALL);
    update_shadow(0, 0, 0);
  }
#endif

  wlr_scene_rect_set_size(border_top, width + left_w + right_w, top_h);
  wlr_scene_node_set_position(&border_top->node, -grow_left, -grow_top);

  wlr_scene_rect_set_size(border_bottom, width + left_w + right_w, bottom_h);
  wlr_scene_node_set_position(&border_bottom->node, -grow_left, thickness + height);

  wlr_scene_rect_set_size(border_left, left_w, height + top_h + bottom_h);
  wlr_scene_node_set_position(&border_left->node, -grow_left, -grow_top);

  wlr_scene_rect_set_size(border_right, right_w, height + top_h + bottom_h);
  wlr_scene_node_set_position(&border_right->node, thickness + width, -grow_top);
}

#if FLEETWM_SCENEFX
void View::update_shadow(int outer_w, int outer_h, int outer_radius) {
  const bool want = outer_w > 0 && server->theme_config().window_shadows && !fullscreen;
  if (!want) {
    if (shadow) wlr_scene_node_set_enabled(&shadow->node, false);
    return;
  }
  constexpr int kBlur = 12, kMargin = 28, kDy = 6;
  constexpr float kColor[4] = {0.0f, 0.0f, 0.0f, 0.42f};
  const int sw = outer_w + 2 * kMargin, sh = outer_h + 2 * kMargin;
  if (!shadow) {
    shadow = wlr_scene_shadow_create(container_tree, sw, sh, outer_radius, kBlur, kColor);
    wlr_scene_node_lower_to_bottom(&shadow->node);
  } else {
    wlr_scene_node_set_enabled(&shadow->node, true);
    wlr_scene_shadow_set_size(shadow, sw, sh);
    wlr_scene_shadow_set_corner_radius(shadow, outer_radius);
  }
  wlr_scene_node_lower_to_bottom(&shadow->node);  // below the border ring
  wlr_scene_node_set_position(&shadow->node, -grow_left - kMargin, -grow_top - kMargin + kDy);
  // Do not draw the shadow under the window itself (matters for translucent
  // clients such as terminals).
  clipped_region hole = clipped_region_get_default();
  hole.area = {kMargin, kMargin - kDy, outer_w, outer_h};
  hole.corner_radius = outer_radius;
  hole.corners = CORNER_LOCATION_ALL;
  wlr_scene_shadow_set_clipped_region(shadow, hole);
}
#endif

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
