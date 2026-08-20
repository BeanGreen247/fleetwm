#pragma once

#include <filesystem>

namespace fleetwm::config_internal {

// $XDG_CONFIG_HOME, or ~/.config if unset/empty -- the shared base every
// *_config.cpp/theme.cpp module resolves its own user config path
// against. Was five byte-identical copies (one per module, each in its
// own anonymous namespace) until unity builds (meson -Dunity=on)
// exposed the duplication as a hard symbol collision -- consolidated
// here rather than renamed apart, since it's genuinely the same
// function, not five unrelated ones that happen to look alike.
//
// Throws std::runtime_error if neither XDG_CONFIG_HOME nor HOME is set.
std::filesystem::path config_home();

}  // namespace fleetwm::config_internal
