#include "output.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <vector>

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
  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel) {
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, content_w, content_h);
  }
  // NOT calling view->resize_border() here: wlr_xdg_toplevel_set_size()
  // is async, so the client hasn't actually resized yet -- border rects
  // must stay in sync with the client's real committed geometry, which
  // xdg_toplevel_surface_commit (server.cpp) updates once the resize
  // actually lands. Calling it here too would draw borders against the
  // stale pre-resize size for one frame (see server.cpp's commit handler
  // comment for the full story).
}

}  // namespace

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

  wlr_box box{};
  wlr_output_layout_get_box(server->output_layout(), wlr_output_ptr, &box);

  if (tiled.size() == 1) {
    tile_view(tiled[0], box.x, box.y, box.width, box.height);
    return;
  }

  int master_width = box.width / 2;
  tile_view(tiled[0], box.x, box.y, master_width, box.height);

  int stack_count = static_cast<int>(tiled.size()) - 1;
  int stack_x = box.x + master_width;
  int stack_width = box.width - master_width;
  int stack_height = box.height / stack_count;
  for (int i = 0; i < stack_count; ++i) {
    int y = box.y + i * stack_height;
    // Last stripe absorbs any remainder from integer division so the
    // stack always exactly fills the output height.
    int h = (i == stack_count - 1) ? (box.y + box.height - y) : stack_height;
    tile_view(tiled[i + 1], stack_x, y, stack_width, h);
  }
}

}  // namespace fleetwm
