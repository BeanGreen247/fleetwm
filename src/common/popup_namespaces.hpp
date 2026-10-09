#pragma once

// Layer-shell popups that close when the user presses anywhere outside them. The compositor does it (a
// popup that only covers its own card cannot see clicks elsewhere): it closes the popup on a press that
// lands outside its surface and swallows that press, so clicking the bar's volume readout or the Start
// button a second time simply closes the popup instead of opening another.

#include <cstring>

namespace fleetwm {

inline constexpr const char* kStartMenuNamespace = "fleetwm-start-menu";
inline constexpr const char* kAudioMixerNamespace = "fleetwm-audiomixer";
inline constexpr const char* kCtxMenuNamespace = "fleetwm-ctxmenu";

inline bool dismisses_on_outside_click(const char* layer_namespace) {
  if (layer_namespace == nullptr) return false;
  return std::strcmp(layer_namespace, kStartMenuNamespace) == 0 || std::strcmp(layer_namespace, kAudioMixerNamespace) == 0 ||
         std::strcmp(layer_namespace, kCtxMenuNamespace) == 0;
}

}  // namespace fleetwm
