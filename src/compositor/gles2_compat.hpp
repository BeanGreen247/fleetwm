#pragma once

#include <wlr/render/wlr_renderer.h>

// A wlroots built with only the pixman renderer (no Mesa, no EGL, no GBM: the low-memory build, docs/ALPINE.md) has no
// <wlr/render/gles2.h>; the compositor then simply never meets a GLES2 renderer.
#if __has_include(<wlr/render/gles2.h>)
#include <wlr/render/gles2.h>
inline bool renderer_is_gles2(wlr_renderer* r) { return wlr_renderer_is_gles2(r); }
#else
inline bool renderer_is_gles2(wlr_renderer*) { return false; }
#endif
