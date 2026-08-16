#include "output.hpp"

extern "C" {
#include <wlr/types/wlr_layer_shell_v1.h>
}

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <vector>

#include "layer_surface.hpp"
#include "server.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

void output_frame(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, frame);

  wlr_scene* scene = output->server->scene();
  wlr_scene_output* scene_output =
      wlr_scene_get_scene_output(scene, output->wlr_output_ptr);

  wlr_scene_output_commit(scene_output, nullptr);

  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

void output_request_state(wl_listener* listener, void* data) {
  Output* output = wl_container_of(listener, output, request_state);
  auto* event = static_cast<const wlr_output_event_request_state*>(data);
  wlr_output_commit_state(output->wlr_output_ptr, event->state);
}

void output_destroy(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, destroy);
  auto& outputs = output->server->outputs;
  outputs.erase(
      std::remove_if(outputs.begin(), outputs.end(),
                      [output](const std::unique_ptr<Output>& o) { return o.get() == output; }),
      outputs.end());
}

}  // namespace

Output::Output(Server* server_, wlr_output* wlr_output_ptr_)
    : server(server_), wlr_output_ptr(wlr_output_ptr_) {
  frame.notify = output_frame;
  wl_signal_add(&wlr_output_ptr->events.frame, &frame);

  request_state.notify = output_request_state;
  wl_signal_add(&wlr_output_ptr->events.request_state, &request_state);

  destroy.notify = output_destroy;
  wl_signal_add(&wlr_output_ptr->events.destroy, &destroy);
}

Output::~Output() {
  wl_list_remove(&frame.link);
  wl_list_remove(&request_state.link);
  wl_list_remove(&destroy.link);
}

void Output::switch_workspace(int index) {
  if (index < 0 || index >= kWorkspaceCount || index == active_workspace_index) {
    return;
  }

  // Pinned views skip this enable/disable entirely -- they live in
  // Server's layer_pinned_ tree (always enabled) and are meant to stay
  // visible across every workspace switch, not just their own (see
  // View::set_pinned). They're still tracked in workspaces[] like any
  // other view for bookkeeping (add_view/remove_view), just not toggled
  // here.
  for (View* view : workspaces[active_workspace_index].views()) {
    if (!view->pinned) {
      wlr_scene_node_set_enabled(&view->container_tree->node, false);
    }
  }

  active_workspace_index = index;

  for (View* view : workspaces[active_workspace_index].views()) {
    if (!view->pinned) {
      wlr_scene_node_set_enabled(&view->container_tree->node, true);
    }
  }

  relayout();
}

namespace {

// Positions container_tree at (x, y) and asks the client to resize its
// surface to (w, h) minus the view's current border inset -- container_tree
// is the outer box (border + content), but wlr_xdg_toplevel_set_size sets
// the client's content size, not the outer box, so the border would
// otherwise eat into the requested tile size instead of framing it.
void tile_view(View* view, int x, int y, int w, int h) {
  wlr_scene_node_set_position(&view->container_tree->node, x, y);
  int thickness = view->border_thickness();
  int content_w = std::max(1, w - 2 * thickness);
  int content_h = std::max(1, h - 2 * thickness);
  // Skip the request entirely when nothing actually changed -- see the
  // last_requested_content_w/h comment in view.hpp for why this matters:
  // relayout() (and therefore tile_view()) runs far more often than the
  // tiled layout actually changes.
  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel &&
      (content_w != view->last_requested_content_w ||
       content_h != view->last_requested_content_h)) {
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, content_w, content_h);
    view->last_requested_content_w = content_w;
    view->last_requested_content_h = content_h;
  }
  // NOT calling view->resize_border() here: wlr_xdg_toplevel_set_size()
  // is async, so the client hasn't actually resized yet -- border rects
  // must stay in sync with the client's real committed geometry, which
  // xdg_toplevel_surface_commit (server.cpp) updates once the resize
  // actually lands. Calling it here too would draw borders against the
  // stale pre-resize size for one frame (see server.cpp's commit handler
  // comment for the full story).
}

// The View currently holding keyboard focus, or nullptr -- local copy of
// input.cpp's own focused_view() (kept file-static there), since that
// one lives in an anonymous namespace and isn't shared across
// translation units. Same "seat only tracks a wlr_surface*, scan views
// for the owner" approach.
View* focused_view(Server* server) {
  wlr_surface* focused_surface = server->seat()->keyboard_state.focused_surface;
  if (!focused_surface) {
    return nullptr;
  }
  for (const std::unique_ptr<View>& view : server->views) {
    if (view->surface() == focused_surface) {
      return view.get();
    }
  }
  return nullptr;
}

// Sets View::grow_left/top/right/bottom (view.hpp) to `grow` on whichever
// edges of `tile` already sit at the outer boundary of the workable area
// (`outer`) -- i.e. edges facing the bar/screen edge, never an edge
// shared with a neighboring tiled window, so the resulting border bleed
// (View::resize_border()) can never overlap another tiled view. This is
// what makes the focused window "step forward": its border eats into its
// own share of the outer gap rather than shrinking anything else. `grow`
// is 0 for every non-focused view, clearing any bleed left over from a
// previous focus.
void apply_edge_grow(View* view, const wlr_box& tile, const wlr_box& outer, int grow) {
  view->grow_left = (grow > 0 && tile.x <= outer.x) ? grow : 0;
  view->grow_top = (grow > 0 && tile.y <= outer.y) ? grow : 0;
  view->grow_right = (grow > 0 && tile.x + tile.width >= outer.x + outer.width) ? grow : 0;
  view->grow_bottom = (grow > 0 && tile.y + tile.height >= outer.y + outer.height) ? grow : 0;
  view->resize_border();
}

// Extra breathing room between a tiled window's edge and any adjacent
// exclusive-zone layer surface (e.g. fleetwm-bar) -- purely cosmetic, on
// top of the zone's own reserved space, so a window's border doesn't sit
// flush against the bar's own border with zero visual gap between them.
// Only applied to edges that actually have a mapped exclusive-zone
// surface reserving space there; an output with no bar at all still
// tiles edge-to-edge, unaffected.
constexpr int kExclusiveZoneGapPx = 6;

}  // namespace

void Output::update_usable_area() {
  wlr_box box{};
  wlr_output_layout_get_box(server->output_layout(), wlr_output_ptr, &box);

  for (const std::unique_ptr<LayerSurface>& ls : server->layer_surfaces) {
    if (ls->layer_surface->output != wlr_output_ptr || !ls->surface()->mapped) {
      continue;
    }
    uint32_t exclusive_zone = ls->layer_surface->current.exclusive_zone > 0
                                   ? static_cast<uint32_t>(ls->layer_surface->current.exclusive_zone)
                                   : 0;
    if (exclusive_zone == 0) {
      continue;
    }
    uint32_t anchor = ls->layer_surface->current.anchor;
    bool anchored_left = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    bool anchored_right = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    bool anchored_top = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    bool anchored_bottom = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    // Exclusive zone only reserves space for a surface anchored to
    // exactly one edge (spanning the perpendicular axis) -- matches the
    // wlr-layer-shell-v1 spec's own definition of exclusive_zone.
    if (anchored_top && !anchored_bottom && anchored_left && anchored_right) {
      int reserve = static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
      box.y += reserve;
      box.height -= reserve;
    } else if (anchored_bottom && !anchored_top && anchored_left && anchored_right) {
      box.height -= static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
    } else if (anchored_left && !anchored_right && anchored_top && anchored_bottom) {
      int reserve = static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
      box.x += reserve;
      box.width -= reserve;
    } else if (anchored_right && !anchored_left && anchored_top && anchored_bottom) {
      box.width -= static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
    }
  }

  usable_area = box;
  relayout();
}

void Output::relayout() {
  std::vector<View*> tiled;
  for (View* view : active_workspace().views()) {
    if (view->pinned || view->floating || !view->container_tree->node.enabled) {
      continue;
    }
    tiled.push_back(view);
  }
  if (tiled.empty()) {
    return;
  }

  wlr_box box = usable_area;

  // Outer gap: reserved on every edge of the usable area, whether
  // there's a single tiled window or several -- previously only the
  // *inner* gap between master/stack windows was implemented below, so a
  // lone window (or the outermost edge of a multi-window layout) sat
  // flush against the bar/screen edges with no gap at all, which is what
  // made gap_px look broken with only one window open. Clamped so a
  // large gap_px on a small output can't invert width/height negative.
  int gap = std::max(0, server->theme_config().gap_px);
  int outer_w = std::max(1, box.width - 2 * gap);
  int outer_h = std::max(1, box.height - 2 * gap);
  box.x += (box.width - outer_w) / 2;
  box.y += (box.height - outer_h) / 2;
  box.width = outer_w;
  box.height = outer_h;

  // The focused window "steps forward" a few px into its own share of
  // the outer gap -- explicit user request to make the existing
  // raise-to-top-on-focus behavior more visually pronounced. This is
  // border-only bleed (apply_edge_grow() above, View::resize_border()),
  // deliberately never folded into the box below: every tile_view() call
  // here always uses the same plain, focus-independent box, so a focus
  // change never asks the client to resize (see the grow_* fields'
  // comment in view.hpp for why that used to cause a visible flicker).
  // Capped well below the full gap so there's always some residual gap
  // left even around a grown, focused window (and gap_px == 0 means
  // grow == 0: nothing to step into).
  View* focused = focused_view(server);
  int grow = std::min(6, std::max(0, gap - 1));

  if (tiled.size() == 1) {
    tile_view(tiled[0], box.x, box.y, box.width, box.height);
    apply_edge_grow(tiled[0], box, usable_area, tiled[0] == focused ? grow : 0);
    return;
  }

  int master_width = (box.width - gap) / 2;
  wlr_box master_box{box.x, box.y, master_width, box.height};
  tile_view(tiled[0], master_box.x, master_box.y, master_box.width, master_box.height);
  apply_edge_grow(tiled[0], master_box, usable_area, tiled[0] == focused ? grow : 0);

  int stack_count = static_cast<int>(tiled.size()) - 1;
  int stack_x = box.x + master_width + gap;
  int stack_width = box.width - master_width - gap;
  int stack_height = (box.height - (stack_count - 1) * gap) / stack_count;
  for (int i = 0; i < stack_count; ++i) {
    int y = box.y + i * (stack_height + gap);
    // Last stripe absorbs any remainder from integer division so the
    // stack always exactly fills the output height.
    int h = (i == stack_count - 1) ? (box.y + box.height - y) : stack_height;
    wlr_box stack_box{stack_x, y, stack_width, h};
    tile_view(tiled[i + 1], stack_box.x, stack_box.y, stack_box.width, stack_box.height);
    apply_edge_grow(tiled[i + 1], stack_box, usable_area, tiled[i + 1] == focused ? grow : 0);
  }
}

}  // namespace fleetwm
