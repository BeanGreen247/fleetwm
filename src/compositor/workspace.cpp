#include "workspace.hpp"

#include <utility>

namespace fleetwm {

namespace {

template <std::size_t... I>
WorkspaceArray make_workspaces_impl(std::index_sequence<I...>) {
  return {Workspace(static_cast<int>(I))...};
}

}  // namespace

WorkspaceArray make_workspaces() {
  return make_workspaces_impl(std::make_index_sequence<kWorkspaceCount>{});
}

}  // namespace fleetwm
