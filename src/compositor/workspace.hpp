#pragma once

#include <array>
#include <list>

namespace fleetwm {

class View;

// Per-output workspace model (see docs/adr/0002-per-output-workspaces.md):
// every Output owns its own set of 10 Workspaces (keys 1-9, 0 -> index 9).
// A Workspace just tracks which Views belong to it and which one is
// currently active; Output::relayout() is what actually positions/hides
// views based on which Workspace is active, so switching workspaces is
// cheap (no view destruction/recreation, just a visibility+layout pass).
class Workspace {
 public:
  explicit Workspace(int index) : index_(index) {}

  int index() const { return index_; }

  std::list<View*>& views() { return views_; }
  const std::list<View*>& views() const { return views_; }

  void add_view(View* view) { views_.push_front(view); }
  void remove_view(View* view) { views_.remove(view); }

 private:
  int index_;
  std::list<View*> views_;
};

constexpr int kWorkspaceCount = 10;
using WorkspaceArray = std::array<Workspace, kWorkspaceCount>;

WorkspaceArray make_workspaces();

}  // namespace fleetwm
