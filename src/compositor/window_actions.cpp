// Keyboard-driven window management: Alt+Tab cycling, Windows-style snap keys,
// workspaces, and sending a window to another screen.

#include <algorithm>
#include <list>
#include <memory>

#include "ipc_server.hpp"
#include "output.hpp"
#include "server.hpp"
#include "view.hpp"
#include "window_geometry.hpp"
#include "workspace.hpp"

namespace fleetwm {

View* Server::focused_view_for_actions() const {
  wlr_surface* focused = seat_->keyboard_state.focused_surface;
  if (!focused) return nullptr;
  for (const std::unique_ptr<View>& view : views)
    if (view->surface() == focused) return view.get();
  return nullptr;
}

// ---- Alt+Tab -------------------------------------------------------------------

void Server::cycle_windows(bool backward, unsigned hold_mask) {
  auto still_open = [this](View* v) {
    return std::any_of(views.begin(), views.end(), [v](const std::unique_ptr<View>& p) {
      return p.get() == v && p->workspace &&
             (p->pinned || !p->output || p->workspace == &p->output->active_workspace());
    });
  };
  if (cycle_order_.empty()) {
    // `views` is most-recently-used first, which is the order Alt+Tab should walk.
    for (const std::unique_ptr<View>& view : views) {
      if (!view->workspace || !view->is_window() || view->is_child_window()) {
        continue;
      }
      // Only the windows on the workspace being looked at (pinned ones are on all of them);
      // other workspaces are reached with the workspace keys or the taskbar buttons.
      const bool here = view->pinned || !view->output || view->workspace == &view->output->active_workspace();
      if (!here) continue;
      cycle_order_.push_back(view.get());
    }
    if (cycle_order_.size() < 2) {
      cycle_order_.clear();
      return;
    }
    cycle_index_ = 0;
    cycle_hold_mask_ = hold_mask;
  }
  // Windows may have closed (or been sent to another workspace) since the cycle began; drop
  // them so we never touch a stale pointer.
  View* current = cycle_index_ < cycle_order_.size() ? cycle_order_[cycle_index_] : nullptr;
  cycle_order_.erase(std::remove_if(cycle_order_.begin(), cycle_order_.end(),
                                    [&](View* v) { return !still_open(v); }),
                     cycle_order_.end());
  if (cycle_order_.size() < 2) {
    end_window_cycle();
    return;
  }
  auto it = std::find(cycle_order_.begin(), cycle_order_.end(), current);
  cycle_index_ = it == cycle_order_.end() ? 0 : static_cast<size_t>(it - cycle_order_.begin());
  const size_t n = cycle_order_.size();
  cycle_index_ = (cycle_index_ + (backward ? n - 1 : 1)) % n;
  activate_view(cycle_order_[cycle_index_]);
  switcher_.show(cycle_order_, cycle_index_);
}

// ---- Super+arrows (Windows-style snapping) -----------------------------------

void Server::snap_step_focused(geom::Direction dir) {
  View* view = focused_view_for_actions();
  if (!view || !view->output || view->fullscreen) return;
  const geom::SnapZone current = view->maximized ? geom::SnapZone::Maximize : view->snap_zone;
  const geom::SnapStep step = geom::snap_step(current, dir);
  using K = geom::SnapStep::Kind;
  switch (step.kind) {
    case K::Zone: view->snap_to(step.zone); break;
    case K::Restore: view->restore_from_snap(); break;
    case K::Minimize: view->set_minimized(true); break;
    case K::MovePrevScreen:
    case K::MoveNextScreen:
      if (move_view_to_screen(view, step.kind == K::MovePrevScreen ? -1 : 1)) view->snap_to(step.zone);
      break;
    case K::Nothing: break;
  }
}

// ---- whole-desktop actions (Super+D, Super+M) ----------------------------------

namespace {
// Windows on the workspace being looked at, ignoring always-on-top ones (Settings, dialogs).
std::vector<View*> windows_here(const std::list<std::unique_ptr<View>>& all) {
  std::vector<View*> out;
  for (const std::unique_ptr<View>& view : all) {
    if (!view->workspace || !view->output || view->kind != View::Kind::XdgToplevel || view->always_on_top) continue;
    if (view->workspace != &view->output->active_workspace()) continue;
    out.push_back(view.get());
  }
  return out;
}
}  // namespace

void Server::minimize_all() {
  for (View* view : windows_here(views))
    if (!view->minimized) view->set_minimized(true);
}

void Server::restore_all() {
  // Most recently used last, so the one you were on ends up focused.
  std::vector<View*> here = windows_here(views);
  for (auto it = here.rbegin(); it != here.rend(); ++it)
    if ((*it)->minimized) (*it)->set_minimized(false);
}

void Server::show_desktop_toggle() {
  // Drop anything that closed in the meantime.
  hidden_by_show_desktop_.erase(
      std::remove_if(hidden_by_show_desktop_.begin(), hidden_by_show_desktop_.end(),
                     [this](View* v) {
                       return !std::any_of(views.begin(), views.end(),
                                           [v](const std::unique_ptr<View>& p) { return p.get() == v; });
                     }),
      hidden_by_show_desktop_.end());
  if (!hidden_by_show_desktop_.empty()) {
    for (auto it = hidden_by_show_desktop_.rbegin(); it != hidden_by_show_desktop_.rend(); ++it)
      if ((*it)->minimized) (*it)->set_minimized(false);
    hidden_by_show_desktop_.clear();
    return;
  }
  for (View* view : windows_here(views)) {
    if (view->minimized) continue;
    hidden_by_show_desktop_.push_back(view);
    view->set_minimized(true);
  }
}

// ---- workspaces --------------------------------------------------------------

void Server::switch_workspace(int index) {
  if (outputs.empty() || index < 0 || index >= kWorkspaceCount) return;
  View* focused = focused_view_for_actions();
  Output* out = focused && focused->output ? focused->output : focused_output();
  if (out->active_workspace_index == index) return;
  switch_workspace_everywhere(index);
  if (ipc_server) ipc_server->broadcast_workspace_changed(index);
  // The window that was focused is now hidden; hand focus to something that is visible.
  focus_next_after(nullptr);
  schedule_windows_broadcast();
}

void Server::switch_workspace_relative(int delta) {
  if (outputs.empty()) return;
  View* focused = focused_view_for_actions();
  Output* out = focused && focused->output ? focused->output : focused_output();
  const int next = ((out->active_workspace_index + delta) % kWorkspaceCount + kWorkspaceCount) % kWorkspaceCount;
  switch_workspace(next);
}

void Server::move_view_to_workspace(View* view, int index) {
  if (!view || !view->output || !view->workspace || index < 0 || index >= kWorkspaceCount) return;
  Output* out = view->output;
  Workspace& target = out->workspaces[static_cast<size_t>(index)];
  if (view->workspace == &target) return;

  view->workspace->remove_view(view);
  target.add_view(view);
  view->workspace = &target;

  const bool visible = index == out->active_workspace_index;
  wlr_scene_node_set_enabled(&view->container_tree->node, (visible || view->pinned) && !view->minimized);
  if (!visible && !view->pinned && seat_->keyboard_state.focused_surface == view->surface()) {
    focus_next_after(view);
  }
  out->relayout();
  schedule_windows_broadcast();
}

// ---- screens -----------------------------------------------------------------

bool Server::move_view_to_screen(View* view, int delta) {
  if (!view || !view->output || !view->workspace || outputs.size() < 2) return false;
  // Order the screens left to right (then top to bottom) as they sit in the layout.
  std::vector<Output*> order;
  for (const std::unique_ptr<Output>& o : outputs) order.push_back(o.get());
  auto position = [this](Output* o) {
    wlr_box box{};
    wlr_output_layout_get_box(output_layout_, o->wlr_output_ptr, &box);
    return std::make_pair(box.x, box.y);
  };
  std::sort(order.begin(), order.end(), [&](Output* a, Output* b) { return position(a) < position(b); });
  const auto it = std::find(order.begin(), order.end(), view->output);
  if (it == order.end()) return false;
  const long target_index = (it - order.begin()) + delta;
  if (target_index < 0 || target_index >= static_cast<long>(order.size())) return false;
  Output* to = order[static_cast<size_t>(target_index)];

  transfer_view_to_output(view, to, false);
  focus_view(view);
  return true;
}


void Server::transfer_view_to_output(View* view, Output* to, bool keep_position) {
  if (!view || !to || !view->output || !view->workspace || view->output == to) return;
  Output* from = view->output;
  wlr_box from_box{}, to_box{};
  wlr_output_layout_get_box(output_layout_, from->wlr_output_ptr, &from_box);
  wlr_output_layout_get_box(output_layout_, to->wlr_output_ptr, &to_box);
  const int rel_x = view->container_tree->node.x - from_box.x;
  const int rel_y = view->container_tree->node.y - from_box.y;

  // A maximized or snapped window is re-applied on the new screen by the caller (or fit below).
  if (view->maximized) view->set_maximized(false);
  view->snap_zone = geom::SnapZone::None;
  view->has_placed = false;

  const int index = view->workspace->index();
  view->workspace->remove_view(view);
  Workspace& target = to->workspaces[static_cast<size_t>(index)];
  target.add_view(view);
  view->workspace = &target;
  view->output = to;
  if (!keep_position)
    wlr_scene_node_set_position(&view->container_tree->node, to_box.x + std::min(rel_x, std::max(0, to_box.width - 80)),
                                to_box.y + std::min(rel_y, std::max(0, to_box.height - 40)));
  const bool visible = index == to->active_workspace_index;
  wlr_scene_node_set_enabled(&view->container_tree->node, (visible || view->pinned) && !view->minimized);
  from->relayout();
  to->relayout();
  to->fit_floating_views();
  view->resize_border();
  schedule_windows_broadcast();
}

void Server::evacuate_output(Output* from) {
  Output* to = nullptr;
  for (const std::unique_ptr<Output>& o : outputs)
    if (o.get() != from) {
      to = o.get();
      break;
    }
  if (!to) return;
  std::vector<View*> moving;
  for (const std::unique_ptr<View>& view : views)
    if (view->output == from && view->workspace) moving.push_back(view.get());
  for (View* view : moving) {
    view->last_output_name = from->wlr_output_ptr->name ? from->wlr_output_ptr->name : "";
    transfer_view_to_output(view, to, false);
  }
}

void Server::restore_output_windows(Output* back) {
  const char* name = back->wlr_output_ptr->name;
  if (!name) return;
  std::vector<View*> returning;
  for (const std::unique_ptr<View>& view : views)
    if (view->last_output_name == name && view->output && view->output != back && view->workspace) returning.push_back(view.get());
  for (View* view : returning) {
    view->last_output_name.clear();
    transfer_view_to_output(view, back, false);
  }
}

void Server::adopt_output_under(View* view) {
  if (!view || !view->output || outputs.size() < 2) return;
  const wlr_box geo = view->content_geometry();
  const double cx = view->container_tree->node.x + geo.width / 2.0, cy = view->container_tree->node.y + geo.height / 2.0;
  wlr_output* at = wlr_output_layout_output_at(output_layout_, cx, cy);
  Output* target = at ? output_for(at) : nullptr;
  if (target && target != view->output) {
    view->last_output_name.clear();
    transfer_view_to_output(view, target, true);
  }
}

}  // namespace fleetwm
