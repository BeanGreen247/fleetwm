#include "view.hpp"

#include "server.hpp"
#include "workspace.hpp"

namespace fleetwm {

namespace {

// Pinned and pinned+focused colors/thickness are still hardcoded --
// theming those isn't asked for yet (only the plain focus border is, via
// ThemeConfig::focus_border_color + focus_border_thickness_px, read live
// off server->theme_config()). focus_border_color is intentionally
// separate from ThemeConfig::accent (used for UI chrome everywhere
// else -- bar, launcher, settings) so the focused-window border reads as
// a distinct signal instead of blending into whatever else on screen
// already uses the accent color. One border-rect set renders whichever
// of these applies, picked by priority in resize_border() --
// pinned+focused gets its own distinct color/thickness so it doesn't
// read as merely "pinned" or merely "focused".
constexpr int kPinnedBorderThicknessPx = 3;
constexpr float kPinnedBorderColor[4] = {0.2f, 0.6f, 1.0f, 1.0f};  // blue, RGBA

constexpr int kPinnedFocusedBorderThicknessPx = 3;
constexpr float kPinnedFocusedBorderColor[4] = {0.6f, 0.9f, 0.4f, 1.0f};  // green

constexpr float kNoBorderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // fully transparent
// Fallback if theme_config().focus_border_color is somehow unparseable
// -- should never actually be hit since ThemeConfig::focus_border_color's
// own default is a valid "#e6e6f2", but parse_hex_color() leaves its
// output untouched on failure and this is what that untouched buffer
// starts as.
constexpr float kFocusBorderColorFallback[4] = {0.9f, 0.9f, 0.95f, 1.0f};  // near-white

}  // namespace

int View::border_thickness() const {
  if (pinned && focused) {
    return kPinnedFocusedBorderThicknessPx;
  }
  if (pinned) {
    return kPinnedBorderThicknessPx;
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

void View::resize_border() {
  int thickness = border_thickness();
  const float* color;
  float focus_color[4] = {kFocusBorderColorFallback[0], kFocusBorderColorFallback[1],
                           kFocusBorderColorFallback[2], kFocusBorderColorFallback[3]};
  if (pinned && focused) {
    color = kPinnedFocusedBorderColor;
  } else if (pinned) {
    color = kPinnedBorderColor;
  } else if (focused) {
    parse_hex_color(server->theme_config().focus_border_color, focus_color);
    color = focus_color;
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

  wlr_scene_node_set_position(&scene_tree->node, thickness, thickness);

  wlr_scene_rect_set_size(border_top, width + 2 * thickness, thickness);
  wlr_scene_node_set_position(&border_top->node, 0, 0);

  wlr_scene_rect_set_size(border_bottom, width + 2 * thickness, thickness);
  wlr_scene_node_set_position(&border_bottom->node, 0, thickness + height);

  wlr_scene_rect_set_size(border_left, thickness, height);
  wlr_scene_node_set_position(&border_left->node, 0, thickness);

  wlr_scene_rect_set_size(border_right, thickness, height);
  wlr_scene_node_set_position(&border_right->node, thickness + width, thickness);
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
