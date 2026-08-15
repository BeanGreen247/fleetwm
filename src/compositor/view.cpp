#include "view.hpp"

#include "server.hpp"
#include "workspace.hpp"

namespace fleetwm {

namespace {

// Hardcoded for now -- Phase 6's fleetwm-settings will make these
// user-configurable (color + thickness) per the pin-border feature
// request; until that lands, a single fixed accent-ish blue is enough
// to make "this window is pinned" visually obvious.
constexpr int kPinnedBorderThicknessPx = 3;
constexpr float kPinnedBorderColor[4] = {0.2f, 0.6f, 1.0f, 1.0f};  // RGBA
constexpr float kNoBorderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};      // fully transparent

}  // namespace

View::View(Server* server_, Kind kind_) : server(server_), kind(kind_) {}

View::~View() = default;

void View::set_pinned(bool pinned_) {
  if (pinned == pinned_) {
    return;
  }
  pinned = pinned_;

  const float* color = pinned ? kPinnedBorderColor : kNoBorderColor;
  wlr_scene_rect_set_color(border_top, color);
  wlr_scene_rect_set_color(border_bottom, color);
  wlr_scene_rect_set_color(border_left, color);
  wlr_scene_rect_set_color(border_right, color);
  resize_border();

  wlr_scene_node_reparent(&container_tree->node,
                           pinned ? server->layer_pinned() : server->layer_toplevels());
  if (pinned) {
    wlr_scene_node_raise_to_top(&container_tree->node);
  }
}

void View::resize_border() {
  int thickness = pinned ? kPinnedBorderThicknessPx : 0;

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
