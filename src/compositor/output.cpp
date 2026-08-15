#include "output.hpp"

#include <algorithm>
#include <ctime>
#include <memory>

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

  for (View* view : workspaces[active_workspace_index].views()) {
    wlr_scene_node_set_enabled(&view->scene_tree->node, false);
  }

  active_workspace_index = index;

  for (View* view : workspaces[active_workspace_index].views()) {
    wlr_scene_node_set_enabled(&view->scene_tree->node, true);
  }
}

}  // namespace fleetwm
