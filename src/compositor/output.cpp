#include "output.hpp"
#include "window_geometry.hpp"

extern "C" {
#include <wlr/render/gles2.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_tearing_control_v1.h>
}

#include <sys/resource.h>

#include <algorithm>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "debug_overlay.hpp"
#include "layer_surface.hpp"
#include "server.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

// The View currently in View::fullscreen state on `output`, or nullptr.
// Fullscreen apps/games always get uncapped, tearing-eligible treatment
// (see output_frame() below) regardless of the selected RenderMode --
// this is what makes that exemption possible without any per-app
// tracking of its own, per the adaptive-render-throttling design.
View* fullscreen_view_on(Output* output) {
  for (const std::unique_ptr<View>& view : output->server->views) {
    if (view->fullscreen && view->output == output) {
      return view.get();
    }
  }
  return nullptr;
}

// One-shot timer callback (RenderMode::Custom): re-requests a frame once
// the configured FPS interval has actually elapsed, since output_frame()
// deliberately withheld it below.
int fps_cap_timer_fire(void* data) {
  auto* output = static_cast<Output*>(data);
  wlr_output_schedule_frame(output->wlr_output_ptr);
  return 0;
}

void output_frame(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, frame);
  Server* server = output->server;

  View* fullscreen = fullscreen_view_on(output);

  // Interactive drags use a fixed low-power 24 FPS cap on every hardware
  // class. This keeps pointer motion responsive while leaving most GPU time
  // available to the applications being moved. Custom FPS still applies
  // when no drag is active. Neither cap applies to fullscreen content.
  // Custom FPS cap: only throttles ordinary desktop content, never a
  // fullscreen app/game (see fullscreen_view_on() above). Withholding
  // frame_done from clients below is what actually throttles them --
  // their next frame is gated on receiving it -- so a throttled tick
  // skips the commit/frame_done pair entirely and re-arms itself via a
  // timer for whenever the interval actually elapses.
  const bool drag_cap = fullscreen == nullptr && server->grab_active();
  const bool custom_cap = fullscreen == nullptr && server->theme_config().render_mode == RenderMode::Custom;
  if (drag_cap || custom_cap) {
    const int fps = drag_cap ? 24 : std::clamp(server->theme_config().custom_fps_lock, 24, 5000);
    int interval_ms = std::max(1, 1000 / fps);

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (output->fps_cap_has_last_commit) {
      double elapsed_ms = (now.tv_sec - output->fps_cap_last_commit.tv_sec) * 1000.0 +
                           (now.tv_nsec - output->fps_cap_last_commit.tv_nsec) / 1e6;
      if (elapsed_ms < interval_ms) {
        // Only keep the render loop alive while there is actually something to
        // present. Without this check the timer's forced frame request committed
        // an (empty) frame, whose page flip produced the next frame event, which
        // re-armed the timer: an idle desktop in Custom mode committed ~150
        // frames a second forever. With no pending damage the output goes idle
        // and wakes again by itself when something changes.
        wlr_scene_output* pending = wlr_scene_get_scene_output(server->scene(), output->wlr_output_ptr);
        if (pending == nullptr || !pixman_region32_not_empty(&pending->pending_commit_damage)) {
          return;
        }
        int remaining_ms = std::max(1, static_cast<int>(interval_ms - elapsed_ms));
        if (output->fps_cap_timer == nullptr) {
          output->fps_cap_timer = wl_event_loop_add_timer(
              wl_display_get_event_loop(server->display()), fps_cap_timer_fire, output);
        }
        wl_event_source_timer_update(output->fps_cap_timer, remaining_ms);
        return;
      }
    }
    output->fps_cap_last_commit = now;
    output->fps_cap_has_last_commit = true;
  }

  wlr_scene* scene = server->scene();
  wlr_scene_output* scene_output =
      wlr_scene_get_scene_output(scene, output->wlr_output_ptr);

  // Real DRM tearing, automatic and unconditional for a fullscreen
  // surface that has actually hinted it wants async presentation (games/
  // players via SDL/GLFW etc.) -- never a user-selectable mode, see
  // RenderMode's doc comment in theme.hpp. Everything else (ordinary
  // desktop content, or a fullscreen surface with no tearing hint) takes
  // the plain wlr_scene_output_commit() path, which already early-
  // returns for free on a genuinely idle output -- that early return is
  // replicated by hand below only for the tearing branch, since bypassing
  // the convenience call to set tearing_page_flip loses it otherwise.
  wlr_surface* fullscreen_surface = fullscreen != nullptr ? fullscreen->surface() : nullptr;
  bool want_tearing =
      fullscreen_surface != nullptr &&
      wlr_tearing_control_manager_v1_surface_hint_from_surface(server->tearing_manager(),
                                                                 fullscreen_surface) ==
          WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;

  // The overlay repaints inside this frame's own commit, so it never causes a frame by itself.
  timespec commit_start{};
  clock_gettime(CLOCK_MONOTONIC, &commit_start);
  output->debug_frame_begin(commit_start);

  if (want_tearing) {
    if (output->wlr_output_ptr->needs_frame ||
        pixman_region32_not_empty(&scene_output->pending_commit_damage)) {
      wlr_output_state state;
      wlr_output_state_init(&state);
      if (wlr_scene_output_build_state(scene_output, &state, nullptr)) {
        state.tearing_page_flip = true;
        wlr_output_commit_state(output->wlr_output_ptr, &state);
      }
      wlr_output_state_finish(&state);
    }
  } else {
    const bool committed = wlr_scene_output_commit(scene_output, nullptr);
    if (!committed) {
      ++output->commit_failures;
      if (output->has_fallback && output->commit_failures >= 3) {
        server->revert_output_mode(output);
        return;
      }
    } else {
      output->commit_failures = 0;
      if (output->has_fallback && ++output->confirm_frames >= 10) {
        output->has_fallback = false;  // the new mode is proven to work
      }
    }
  }

  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);

  output->debug_frame_end((now.tv_sec - commit_start.tv_sec) * 1000.0 + (now.tv_nsec - commit_start.tv_nsec) / 1e6);
}

void output_request_state(wl_listener* listener, void* data) {
  Output* output = wl_container_of(listener, output, request_state);
  auto* event = static_cast<const wlr_output_event_request_state*>(data);
  wlr_output_commit_state(output->wlr_output_ptr, event->state);
}

void output_destroy(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, destroy);
  output->server->stop_output_helpers(output->wlr_output_ptr->name);
  output->server->evacuate_output(output);
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

  if (fps_cap_timer != nullptr) {
    wl_event_source_remove(fps_cap_timer);
  }
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
    if (!view->pinned && !view->minimized) {
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
  if (view->is_window() &&
      (content_w != view->last_requested_content_w ||
       content_h != view->last_requested_content_h)) {
    view->request_size(content_w, content_h);
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
View* output_focused_view(Server* server) {
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

    // Per the wlr-layer-shell spec an exclusive zone reserves space on the one
    // edge a surface is anchored to: anchored to that edge alone (the centered
    // "island" bar), or to it plus both perpendicular edges (a full-width bar).
    // The floating (capsule/island) bar of the tiling layout keeps a small gap
    // between it and the windows; the Desktop layout's taskbar is flush with the
    // screen edge, so windows meet it directly.
    const int gap_px = server->desktop_layout() ? 0 : server->theme_config().bar_gap_px;
    const int reserve = static_cast<int>(exclusive_zone) + gap_px;
    switch (geom::exclusive_edge(anchored_left, anchored_right, anchored_top, anchored_bottom)) {
      case geom::BarEdge::Top:
        box.y += reserve;
        box.height -= reserve;
        break;
      case geom::BarEdge::Bottom: box.height -= reserve; break;
      case geom::BarEdge::Left:
        box.x += reserve;
        box.width -= reserve;
        break;
      case geom::BarEdge::Right: box.width -= reserve; break;
      case geom::BarEdge::None: break;
    }
  }

  usable_area = box;
  fit_floating_views();
  relayout();
}

void Output::snap_tiled_windows() {
  for (Workspace& workspace : workspaces) {
    std::vector<View*> tiled;
    for (View* view : workspace.views()) {
      // Same selection as relayout(), except visibility: other workspaces count too.
      if (view->pinned || view->floating || view->fullscreen || view->minimized ||
          view->kind != View::Kind::XdgToplevel) {
        continue;
      }
      tiled.push_back(view);
    }
    const std::vector<geom::SnapZone> zones = geom::tile_zones(tiled.size());
    for (size_t i = 0; i < tiled.size(); ++i) {
      // Forget any earlier snap/maximize so the tiled slot wins.
      if (tiled[i]->maximized) tiled[i]->set_maximized(false);
      tiled[i]->snap_zone = geom::SnapZone::None;
      if (zones[i] != geom::SnapZone::None) tiled[i]->snap_to(zones[i]);
      else tiled[i]->place_tile(i, tiled.size());
    }
  }
}

void Output::fit_floating_views() {
  if (!server->desktop_layout()) {
    return;
  }
  for (const std::unique_ptr<View>& view : server->views) {
    if (view->output != this || view->fullscreen || view->pinned) {
      continue;
    }
    if (view->maximized) {
      view->refit_maximized();
      continue;
    }
    if (view->snap_zone != geom::SnapZone::None) {
      view->refit_snapped();
      continue;
    }
    if (view->has_placed) {
      view->refit_placed();
      continue;
    }
    // Keep the titlebar reachable: not under a taskbar, not off-screen.
    constexpr int kKeepVisible = 80;
    const int outer_w = view->content_w + 2 * view->border_thickness();
    int x = view->container_tree->node.x, y = view->container_tree->node.y;
    x = std::clamp(x, usable_area.x - outer_w + kKeepVisible, usable_area.x + usable_area.width - kKeepVisible);
    y = std::clamp(y, usable_area.y, std::max(usable_area.y, usable_area.y + usable_area.height - 32));
    wlr_scene_node_set_position(&view->container_tree->node, x, y);
  }
}

void Output::relayout() {
  // Desktop layout: windows are free-floating, nothing to tile.
  if (server->desktop_layout()) {
    return;
  }
  std::vector<View*> tiled;
  for (View* view : active_workspace().views()) {
    if (view->pinned || view->floating || view->fullscreen ||
        !view->container_tree->node.enabled) {
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
  const int gap = std::max(0, server->theme_config().gap_px);              // between windows
  const int edge = std::max(0, server->theme_config().outer_gap_px);       // around the tiled area
  int outer_w = std::max(1, box.width - 2 * edge);
  int outer_h = std::max(1, box.height - 2 * edge);
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
  View* focused = output_focused_view(server);
  int grow = std::min(6, std::max(0, edge - 1));

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

void Output::debug_frame_begin(const timespec& now) {
  if (!server->debug_overlay_enabled()) {
    debug_overlay_.reset();  // nothing of it stays allocated while it is off
    return;
  }
  if (!debug_overlay_) debug_overlay_ = std::make_unique<DebugOverlay>(server, this);
  debug_overlay_->frame_begin(now);
}

void Output::debug_frame_end(double cost_ms) {
  if (debug_overlay_) debug_overlay_->frame_end(cost_ms);
}

}  // namespace fleetwm
