#pragma once

// Minimal freedesktop icon theme lookup (index.theme directories + Inherits,
// PNG/SVG), GTK-free. Used for tray icons given by name.

#include <cairo.h>

#include <string>
#include <vector>

namespace fleetwm::lean {

// Returns a new premultiplied ARGB32 cairo image surface (caller owns, free
// with cairo_surface_destroy) whose larger side is about `size` px, or
// nullptr if the icon is not found. `extra_dirs` are searched first for
// "<dir>/<name>.{png,svg}" (the StatusNotifierItem IconThemePath property).
// A name that is an absolute path is loaded directly.
cairo_surface_t* load_icon(const std::string& name, int size,
                           const std::vector<std::string>& extra_dirs = {});

// Converts non-premultiplied RGBA to a premultiplied ARGB32 surface.
cairo_surface_t* surface_from_rgba(const unsigned char* rgba, int w, int h);

}  // namespace fleetwm::lean
