#pragma once

// The scene-graph API: SceneFX's drop-in replacement for wlroots' wlr_scene
// when the build has it (rounded corners, shadows), plain wlroots otherwise.
// Never include <wlr/types/wlr_scene.h> directly in the compositor.

#include "config.h"

extern "C" {
#if FLEETWM_SCENEFX
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>
#else
#include <wlr/types/wlr_scene.h>
#endif
}
