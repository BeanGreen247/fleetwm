#pragma once

#include <vector>

struct wlr_scene_tree;

namespace fleetwm {

class Server;
class View;

// The Alt+Tab window switcher overlay, in the style of Windows 7: a glass-like panel in the
// middle of the screen with a live thumbnail of every window in the cycle, the one you are about
// to switch to framed, and its title on top. Shown while the cycle runs, gone when you let go.
class WindowSwitcher {
 public:
  explicit WindowSwitcher(Server* server) : server_(server) {}
  ~WindowSwitcher() = default;  // the scene is torn down with the server
  WindowSwitcher(const WindowSwitcher&) = delete;
  WindowSwitcher& operator=(const WindowSwitcher&) = delete;

  // Shows (or updates) the panel for `order`, with `selected` framed.
  void show(const std::vector<View*>& order, size_t selected);
  void hide();
  bool visible() const { return tree_ != nullptr; }

 private:
  Server* server_;
  wlr_scene_tree* tree_ = nullptr;
};

}  // namespace fleetwm
