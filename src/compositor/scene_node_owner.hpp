#pragma once

namespace fleetwm {

// Tags what kind of object a wlr_scene_tree's node.data points to, so
// hit-testing code (view_at()-equivalent in server.cpp) can safely
// disambiguate before reinterpreting the pointer. Must be the first
// member of View and LayerSurface so a scene node's node.data, cast to
// SceneNodeOwner*, reads the correct tag regardless of which type it
// actually points to.
enum class SceneNodeOwner { View, LayerSurface };

}  // namespace fleetwm
