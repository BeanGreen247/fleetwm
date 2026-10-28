#pragma once

// The compositor draws its own Windows 7 style pointer (src/fleetkit/cursor_draw.cpp) for the windows that ask for a cursor by name, but a program that
// loads its own cursor image (GTK 3, Chromium and Electron, Steam, Wine, anything under Xwayland) showed the cursor theme of the desktop it was written for.
// So the same pictures are also written as an xcursor theme, "fleetwm-aero" (glass) or "fleetwm-flat", and XCURSOR_THEME points every program at it:
// one pointer whatever window it is over.

#include <cstdint>
#include <string>
#include <vector>

namespace fleetwm {

struct XcursorFrame {
  uint32_t nominal_size = 32;       // the size this picture is for (XCURSOR_SIZE picks the nearest)
  uint32_t width = 0, height = 0, xhot = 0, yhot = 0;
  uint32_t delay_ms = 0;            // 0 for a still picture; frames of one animation share nominal_size and carry the delay
  std::vector<uint32_t> argb;       // premultiplied ARGB32, width * height
};

// The bytes of an Xcursor file holding these frames (little endian: "Xcur", the table of contents, one image chunk per frame).
std::vector<unsigned char> xcursor_file(const std::vector<XcursorFrame>& frames);

// Writes the theme into $XDG_DATA_HOME/icons (default ~/.local/share/icons), unless an up-to-date copy is there, and returns its name; "" when it could not be written.
// `inherits` is the theme a name this one does not draw falls back to. `glass` picks the Aero or the flat look.
std::string install_xcursor_theme(bool glass, const std::string& inherits);

}  // namespace fleetwm
