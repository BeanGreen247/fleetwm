#include "output.hpp"

extern "C" {
#include <wlr/render/gles2.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_tearing_control_v1.h>
}

#include <sys/resource.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "layer_surface.hpp"
#include "server.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

constexpr int kDebugBarWidth = 3;
constexpr int kDebugBarGap = 1;
constexpr int kDebugBarMaxHeight = 60;
constexpr int kDebugBarMargin = 8;
// A frame at exactly the 60Hz budget (16.6ms) draws at 1/3 of the max
// bar height, not full height -- leaves headroom to actually see how
// far over budget a bad frame is, rather than every frame over ~16ms
// clipping to the same full-height bar.
constexpr float kDebugBarMsForFullHeight = 50.0f;
constexpr float kDebugGoodBudgetMs = 16.6f;
constexpr float kDebugWarnBudgetMs = 33.3f;
constexpr float kDebugTransparent[4] = {0, 0, 0, 0};
constexpr float kDebugGreen[4] = {0.2f, 0.85f, 0.2f, 0.9f};
constexpr float kDebugYellow[4] = {0.9f, 0.85f, 0.1f, 0.9f};
constexpr float kDebugRed[4] = {0.9f, 0.15f, 0.15f, 0.9f};
constexpr float kDebugTextColor[4] = {0.85f, 0.85f, 0.9f, 0.9f};

constexpr int kDebugTextIntervalMs = 500;
constexpr int kDebugGlyphWidth = 3;
constexpr int kDebugGlyphHeight = 5;
constexpr int kDebugGlyphPixelSize = 3;
constexpr int kDebugGlyphGapPx = 3;
constexpr int kDebugTextRowGapPx = 4;

// 3x5 bitmap font, deliberately covering only the characters the debug
// overlay actually needs (digits, '.', and the unit letters in
// "FPS"/"MB"/"MHz") -- see DebugTextRow's own doc comment (output.hpp)
// for why this exists instead of a real font library. Each row is a
// 3-bit value, bit 2 = leftmost column. Unlisted characters (including
// ' ') render as blank.
const std::array<uint8_t, kDebugGlyphHeight>& debug_glyph(char c) {
  static const std::array<uint8_t, kDebugGlyphHeight> kBlank = {0, 0, 0, 0, 0};
  static const std::array<uint8_t, kDebugGlyphHeight> k0 = {0b111, 0b101, 0b101, 0b101, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k1 = {0b010, 0b110, 0b010, 0b010, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k2 = {0b111, 0b001, 0b111, 0b100, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k3 = {0b111, 0b001, 0b111, 0b001, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k4 = {0b101, 0b101, 0b111, 0b001, 0b001};
  static const std::array<uint8_t, kDebugGlyphHeight> k5 = {0b111, 0b100, 0b111, 0b001, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k6 = {0b111, 0b100, 0b111, 0b101, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k7 = {0b111, 0b001, 0b001, 0b001, 0b001};
  static const std::array<uint8_t, kDebugGlyphHeight> k8 = {0b111, 0b101, 0b111, 0b101, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> k9 = {0b111, 0b101, 0b111, 0b001, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> kDot = {0b000, 0b000, 0b000, 0b000, 0b010};
  static const std::array<uint8_t, kDebugGlyphHeight> kM = {0b101, 0b111, 0b111, 0b101, 0b101};
  static const std::array<uint8_t, kDebugGlyphHeight> kB = {0b110, 0b101, 0b110, 0b101, 0b110};
  static const std::array<uint8_t, kDebugGlyphHeight> kH = {0b101, 0b101, 0b111, 0b101, 0b101};
  static const std::array<uint8_t, kDebugGlyphHeight> kZ = {0b111, 0b001, 0b010, 0b100, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> kF = {0b111, 0b100, 0b111, 0b100, 0b100};
  static const std::array<uint8_t, kDebugGlyphHeight> kP = {0b111, 0b101, 0b111, 0b100, 0b100};
  // Renderer-name letters (GLES2/PIXMAN) -- added alongside those two
  // labels specifically, not as a general-purpose alphabet.
  static const std::array<uint8_t, kDebugGlyphHeight> kA = {0b010, 0b101, 0b111, 0b101, 0b101};
  static const std::array<uint8_t, kDebugGlyphHeight> kE = {0b111, 0b100, 0b111, 0b100, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> kG = {0b111, 0b100, 0b101, 0b101, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> kI = {0b111, 0b010, 0b010, 0b010, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> kL = {0b100, 0b100, 0b100, 0b100, 0b111};
  static const std::array<uint8_t, kDebugGlyphHeight> kN = {0b101, 0b111, 0b111, 0b111, 0b101};
  static const std::array<uint8_t, kDebugGlyphHeight> kX = {0b101, 0b101, 0b010, 0b101, 0b101};
  // 's' reuses '5's shape -- both are the same rounded S silhouette on
  // a 3x5 grid, and this overlay never shows both cases of a letter
  // where the distinction would matter.
  switch (c) {
    case '0': return k0;
    case '1': return k1;
    case '2': return k2;
    case '3': return k3;
    case '4': return k4;
    case '5': return k5;
    case '6': return k6;
    case '7': return k7;
    case '8': return k8;
    case '9': return k9;
    case '.': return kDot;
    case 'M': case 'm': return kM;
    case 'B': return kB;
    case 'H': return kH;
    case 'z': case 'Z': return kZ;
    case 's': case 'S': return k5;
    case 'F': case 'f': return kF;
    case 'P': case 'p': return kP;
    case 'A': case 'a': return kA;
    case 'E': case 'e': return kE;
    case 'G': case 'g': return kG;
    case 'I': case 'i': return kI;
    case 'L': case 'l': return kL;
    case 'N': case 'n': return kN;
    case 'X': case 'x': return kX;
    default: return kBlank;
  }
}

// Compositor's own resident memory, in MB. Deliberately getrusage()
// (a pure syscall, RUSAGE_SELF -> ru_maxrss, no file descriptor of any
// kind involved) rather than reading /proc/self/status's VmRSS --
// equally not real storage I/O either way (procfs is generated
// in-kernel from live process state, never touches a disk), but this
// avoids even the appearance of file I/O for a debug overlay that's
// supposed to be as close to free as possible. Note: ru_maxrss is
// *peak* RSS, monotonically non-decreasing for the process's lifetime,
// not current-instant RSS -- close enough for a debug overlay's
// purposes, and avoids /proc entirely.
int read_self_rss_mb() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return -1;
  }
  return static_cast<int>(usage.ru_maxrss / 1024);
}

// Live (not rated/base) clock speed of CPU 0, in MHz. Unlike the RSS
// figure above, there's no syscall-only way to get this on Linux --
// cpufreq's current-scaling value is only ever exposed via sysfs.
// Still not real storage I/O (sysfs is generated in-kernel, same as
// procfs -- nothing here ever touches a disk), just unavoidably a
// plain file read rather than a pure syscall. Turbo/throttle mean this
// changes constantly; that's the point of showing it live rather than
// a fixed spec number. Returns -1 on any read failure (e.g. no cpufreq
// scaling driver).
int read_cpu_mhz() {
  std::ifstream freq("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
  long khz = 0;
  if (freq >> khz) {
    return static_cast<int>(khz / 1000);
  }
  return -1;
}

// Name of the renderer backend actually in use, for the debug overlay --
// answers "is this GPU-accelerated or not" at a glance, which matters
// a lot here: wlr_renderer_autocreate() (Server::init(), server.cpp)
// only tries GLES2/Vulkan and silently falls back to the pixman
// software renderer on any GPU whose driver fails hardware EGL init
// (e.g. a Mali Midgard board with no Panfrost kernel driver -- see
// README's "Supported hardware" table), so this is the difference
// between "accelerated" and "software" that isn't otherwise visible
// anywhere in the UI. Vulkan deliberately not distinguished here (would
// need <vulkan/vulkan_core.h> as a new build dependency just for a
// debug label; fleetwm never requests it via WLR_RENDERER anyway).
const char* debug_renderer_name(wlr_renderer* renderer) {
  if (wlr_renderer_is_pixman(renderer)) {
    return "PIXMAN";
  }
  if (wlr_renderer_is_gles2(renderer)) {
    return "GLES2";
  }
  return "RENDER";
}

// Creates every cell rect for one text row up front (DebugTextRow::
// kMaxChars * kGlyphCells rects total), all initially transparent --
// render_text_row() below only ever recolors/repositions/hides these
// same rects afterward, never creates or destroys any.
void create_text_row(wlr_scene_tree* parent, DebugTextRow& row, int x, int y) {
  for (int ch = 0; ch < DebugTextRow::kMaxChars; ++ch) {
    for (int cell = 0; cell < DebugTextRow::kGlyphCells; ++cell) {
      wlr_scene_rect* rect =
          wlr_scene_rect_create(parent, kDebugGlyphPixelSize, kDebugGlyphPixelSize,
                                 kDebugTransparent);
      int col = cell % kDebugGlyphWidth;
      int glyph_row = cell / kDebugGlyphWidth;
      int char_x = x + ch * (kDebugGlyphWidth * kDebugGlyphPixelSize + kDebugGlyphGapPx);
      wlr_scene_node_set_position(&rect->node, char_x + col * kDebugGlyphPixelSize,
                                   y + glyph_row * kDebugGlyphPixelSize);
      row.cells[ch][cell] = rect;
    }
  }
  row.created = true;
}

// Re-renders `text` (silently truncated to DebugTextRow::kMaxChars)
// into an already-created row's rects -- recolors the "on" pixels of
// each character's glyph and hides the rest, including every cell
// belonging to a character slot past the end of `text`.
void render_text_row(DebugTextRow& row, const std::string& text, const float color[4]) {
  for (int ch = 0; ch < DebugTextRow::kMaxChars; ++ch) {
    const std::array<uint8_t, kDebugGlyphHeight>& glyph =
        ch < static_cast<int>(text.size()) ? debug_glyph(text[ch]) : debug_glyph(' ');
    for (int cell = 0; cell < DebugTextRow::kGlyphCells; ++cell) {
      int col = cell % kDebugGlyphWidth;
      int glyph_row = cell / kDebugGlyphWidth;
      bool on = (glyph[glyph_row] >> (kDebugGlyphWidth - 1 - col)) & 1;
      wlr_scene_rect_set_color(row.cells[ch][cell], on ? color : kDebugTransparent);
    }
  }
}

void destroy_text_row(DebugTextRow& row) {
  if (!row.created) {
    return;
  }
  for (auto& char_cells : row.cells) {
    for (wlr_scene_rect* rect : char_cells) {
      if (rect != nullptr) {
        wlr_scene_node_destroy(&rect->node);
      }
    }
  }
}

}  // namespace

namespace {

// The View currently in View::fullscreen state on `output`, or nullptr.
// Fullscreen apps/games always get uncapped, tearing-eligible treatment
// (see output_frame() below) regardless of the selected RenderMode --
// this is what makes that exemption possible without any per-app
// tracking of its own, per the adaptive-render-throttling design.
View* fullscreen_view_on(Output* output) {
  for (const std::unique_ptr<View>& view : output->server->views) {
    if (view->fullscreen && view->output == output) {
      return view.get();
    }
  }
  return nullptr;
}

// One-shot timer callback (RenderMode::Custom): re-requests a frame once
// the configured FPS interval has actually elapsed, since output_frame()
// deliberately withheld it below.
int fps_cap_timer_fire(void* data) {
  auto* output = static_cast<Output*>(data);
  wlr_output_schedule_frame(output->wlr_output_ptr);
  return 0;
}

void output_frame(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, frame);
  Server* server = output->server;

  View* fullscreen = fullscreen_view_on(output);

  // Custom FPS cap: only throttles ordinary desktop content, never a
  // fullscreen app/game (see fullscreen_view_on() above). Withholding
  // frame_done from clients below is what actually throttles them --
  // their next frame is gated on receiving it -- so a throttled tick
  // skips the commit/frame_done pair entirely and re-arms itself via a
  // timer for whenever the interval actually elapses.
  if (fullscreen == nullptr && server->theme_config().render_mode == RenderMode::Custom) {
    int fps = std::clamp(server->theme_config().custom_fps_lock, 24, 5000);
    int interval_ms = std::max(1, 1000 / fps);

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (output->fps_cap_has_last_commit) {
      double elapsed_ms = (now.tv_sec - output->fps_cap_last_commit.tv_sec) * 1000.0 +
                           (now.tv_nsec - output->fps_cap_last_commit.tv_nsec) / 1e6;
      if (elapsed_ms < interval_ms) {
        int remaining_ms = std::max(1, static_cast<int>(interval_ms - elapsed_ms));
        if (output->fps_cap_timer == nullptr) {
          output->fps_cap_timer = wl_event_loop_add_timer(
              wl_display_get_event_loop(server->display()), fps_cap_timer_fire, output);
        }
        wl_event_source_timer_update(output->fps_cap_timer, remaining_ms);
        return;
      }
    }
    output->fps_cap_last_commit = now;
    output->fps_cap_has_last_commit = true;
  }

  wlr_scene* scene = server->scene();
  wlr_scene_output* scene_output =
      wlr_scene_get_scene_output(scene, output->wlr_output_ptr);

  // Real DRM tearing, automatic and unconditional for a fullscreen
  // surface that has actually hinted it wants async presentation (games/
  // players via SDL/GLFW etc.) -- never a user-selectable mode, see
  // RenderMode's doc comment in theme.hpp. Everything else (ordinary
  // desktop content, or a fullscreen surface with no tearing hint) takes
  // the plain wlr_scene_output_commit() path, which already early-
  // returns for free on a genuinely idle output -- that early return is
  // replicated by hand below only for the tearing branch, since bypassing
  // the convenience call to set tearing_page_flip loses it otherwise.
  wlr_surface* fullscreen_surface = fullscreen != nullptr ? fullscreen->surface() : nullptr;
  bool want_tearing =
      fullscreen_surface != nullptr &&
      wlr_tearing_control_manager_v1_surface_hint_from_surface(server->tearing_manager(),
                                                                 fullscreen_surface) ==
          WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;

  if (want_tearing) {
    if (output->wlr_output_ptr->needs_frame ||
        pixman_region32_not_empty(&scene_output->pending_commit_damage)) {
      wlr_output_state state;
      wlr_output_state_init(&state);
      if (wlr_scene_output_build_state(scene_output, &state, nullptr)) {
        state.tearing_page_flip = true;
        wlr_output_commit_state(output->wlr_output_ptr, &state);
      }
      wlr_output_state_finish(&state);
    }
  } else {
    wlr_scene_output_commit(scene_output, nullptr);
  }

  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);

  output->update_debug_overlay();
}

void output_request_state(wl_listener* listener, void* data) {
  Output* output = wl_container_of(listener, output, request_state);
  auto* event = static_cast<const wlr_output_event_request_state*>(data);
  wlr_output_commit_state(output->wlr_output_ptr, event->state);
}

void output_destroy(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, destroy);
  auto& outputs = output->server->outputs;
  outputs.erase(
      std::remove_if(outputs.begin(), outputs.end(),
                      [output](const std::unique_ptr<Output>& o) { return o.get() == output; }),
      outputs.end());
}

}  // namespace

Output::Output(Server* server_, wlr_output* wlr_output_ptr_)
    : server(server_), wlr_output_ptr(wlr_output_ptr_) {
  frame.notify = output_frame;
  wl_signal_add(&wlr_output_ptr->events.frame, &frame);

  request_state.notify = output_request_state;
  wl_signal_add(&wlr_output_ptr->events.request_state, &request_state);

  destroy.notify = output_destroy;
  wl_signal_add(&wlr_output_ptr->events.destroy, &destroy);
}

Output::~Output() {
  wl_list_remove(&frame.link);
  wl_list_remove(&request_state.link);
  wl_list_remove(&destroy.link);

  if (fps_cap_timer != nullptr) {
    wl_event_source_remove(fps_cap_timer);
  }

  // debug_bars_ are parented to the server-level layer_debug_ tree, not
  // to anything owned by this Output -- if this output is being
  // destroyed (monitor unplugged) while the compositor keeps running,
  // nothing else would ever destroy these nodes, leaving stale frame-
  // time bars on screen for a monitor that no longer exists.
  for (wlr_scene_rect* bar : debug_bars_) {
    if (bar != nullptr) {
      wlr_scene_node_destroy(&bar->node);
    }
  }
  destroy_text_row(debug_frame_time_row_);
  destroy_text_row(debug_ram_row_);
  destroy_text_row(debug_cpu_row_);
  destroy_text_row(debug_renderer_row_);
}

void Output::switch_workspace(int index) {
  if (index < 0 || index >= kWorkspaceCount || index == active_workspace_index) {
    return;
  }

  // Pinned views skip this enable/disable entirely -- they live in
  // Server's layer_pinned_ tree (always enabled) and are meant to stay
  // visible across every workspace switch, not just their own (see
  // View::set_pinned). They're still tracked in workspaces[] like any
  // other view for bookkeeping (add_view/remove_view), just not toggled
  // here.
  for (View* view : workspaces[active_workspace_index].views()) {
    if (!view->pinned) {
      wlr_scene_node_set_enabled(&view->container_tree->node, false);
    }
  }

  active_workspace_index = index;

  for (View* view : workspaces[active_workspace_index].views()) {
    if (!view->pinned) {
      wlr_scene_node_set_enabled(&view->container_tree->node, true);
    }
  }

  relayout();
}

namespace {

// Positions container_tree at (x, y) and asks the client to resize its
// surface to (w, h) minus the view's current border inset -- container_tree
// is the outer box (border + content), but wlr_xdg_toplevel_set_size sets
// the client's content size, not the outer box, so the border would
// otherwise eat into the requested tile size instead of framing it.
void tile_view(View* view, int x, int y, int w, int h) {
  wlr_scene_node_set_position(&view->container_tree->node, x, y);
  int thickness = view->border_thickness();
  int content_w = std::max(1, w - 2 * thickness);
  int content_h = std::max(1, h - 2 * thickness);
  // Skip the request entirely when nothing actually changed -- see the
  // last_requested_content_w/h comment in view.hpp for why this matters:
  // relayout() (and therefore tile_view()) runs far more often than the
  // tiled layout actually changes.
  if (view->kind == View::Kind::XdgToplevel && view->xdg_toplevel &&
      (content_w != view->last_requested_content_w ||
       content_h != view->last_requested_content_h)) {
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, content_w, content_h);
    view->last_requested_content_w = content_w;
    view->last_requested_content_h = content_h;
  }
  // NOT calling view->resize_border() here: wlr_xdg_toplevel_set_size()
  // is async, so the client hasn't actually resized yet -- border rects
  // must stay in sync with the client's real committed geometry, which
  // xdg_toplevel_surface_commit (server.cpp) updates once the resize
  // actually lands. Calling it here too would draw borders against the
  // stale pre-resize size for one frame (see server.cpp's commit handler
  // comment for the full story).
}

// The View currently holding keyboard focus, or nullptr -- local copy of
// input.cpp's own focused_view() (kept file-static there), since that
// one lives in an anonymous namespace and isn't shared across
// translation units. Same "seat only tracks a wlr_surface*, scan views
// for the owner" approach.
View* focused_view(Server* server) {
  wlr_surface* focused_surface = server->seat()->keyboard_state.focused_surface;
  if (!focused_surface) {
    return nullptr;
  }
  for (const std::unique_ptr<View>& view : server->views) {
    if (view->surface() == focused_surface) {
      return view.get();
    }
  }
  return nullptr;
}

// Sets View::grow_left/top/right/bottom (view.hpp) to `grow` on whichever
// edges of `tile` already sit at the outer boundary of the workable area
// (`outer`) -- i.e. edges facing the bar/screen edge, never an edge
// shared with a neighboring tiled window, so the resulting border bleed
// (View::resize_border()) can never overlap another tiled view. This is
// what makes the focused window "step forward": its border eats into its
// own share of the outer gap rather than shrinking anything else. `grow`
// is 0 for every non-focused view, clearing any bleed left over from a
// previous focus.
void apply_edge_grow(View* view, const wlr_box& tile, const wlr_box& outer, int grow) {
  view->grow_left = (grow > 0 && tile.x <= outer.x) ? grow : 0;
  view->grow_top = (grow > 0 && tile.y <= outer.y) ? grow : 0;
  view->grow_right = (grow > 0 && tile.x + tile.width >= outer.x + outer.width) ? grow : 0;
  view->grow_bottom = (grow > 0 && tile.y + tile.height >= outer.y + outer.height) ? grow : 0;
  view->resize_border();
}

// Extra breathing room between a tiled window's edge and any adjacent
// exclusive-zone layer surface (e.g. fleetwm-bar) -- purely cosmetic, on
// top of the zone's own reserved space, so a window's border doesn't sit
// flush against the bar's own border with zero visual gap between them.
// Only applied to edges that actually have a mapped exclusive-zone
// surface reserving space there; an output with no bar at all still
// tiles edge-to-edge, unaffected.
constexpr int kExclusiveZoneGapPx = 6;

}  // namespace

void Output::update_usable_area() {
  wlr_box box{};
  wlr_output_layout_get_box(server->output_layout(), wlr_output_ptr, &box);

  for (const std::unique_ptr<LayerSurface>& ls : server->layer_surfaces) {
    if (ls->layer_surface->output != wlr_output_ptr || !ls->surface()->mapped) {
      continue;
    }
    uint32_t exclusive_zone = ls->layer_surface->current.exclusive_zone > 0
                                   ? static_cast<uint32_t>(ls->layer_surface->current.exclusive_zone)
                                   : 0;
    if (exclusive_zone == 0) {
      continue;
    }
    uint32_t anchor = ls->layer_surface->current.anchor;
    bool anchored_left = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    bool anchored_right = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    bool anchored_top = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    bool anchored_bottom = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    // Exclusive zone only reserves space for a surface anchored to
    // exactly one edge (spanning the perpendicular axis) -- matches the
    // wlr-layer-shell-v1 spec's own definition of exclusive_zone.
    if (anchored_top && !anchored_bottom && anchored_left && anchored_right) {
      int reserve = static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
      box.y += reserve;
      box.height -= reserve;
    } else if (anchored_bottom && !anchored_top && anchored_left && anchored_right) {
      box.height -= static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
    } else if (anchored_left && !anchored_right && anchored_top && anchored_bottom) {
      int reserve = static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
      box.x += reserve;
      box.width -= reserve;
    } else if (anchored_right && !anchored_left && anchored_top && anchored_bottom) {
      box.width -= static_cast<int>(exclusive_zone) + kExclusiveZoneGapPx;
    }
  }

  usable_area = box;
  relayout();
}

void Output::relayout() {
  std::vector<View*> tiled;
  for (View* view : active_workspace().views()) {
    if (view->pinned || view->floating || view->fullscreen ||
        !view->container_tree->node.enabled) {
      continue;
    }
    tiled.push_back(view);
  }
  if (tiled.empty()) {
    return;
  }

  wlr_box box = usable_area;

  // Outer gap: reserved on every edge of the usable area, whether
  // there's a single tiled window or several -- previously only the
  // *inner* gap between master/stack windows was implemented below, so a
  // lone window (or the outermost edge of a multi-window layout) sat
  // flush against the bar/screen edges with no gap at all, which is what
  // made gap_px look broken with only one window open. Clamped so a
  // large gap_px on a small output can't invert width/height negative.
  int gap = std::max(0, server->theme_config().gap_px);
  int outer_w = std::max(1, box.width - 2 * gap);
  int outer_h = std::max(1, box.height - 2 * gap);
  box.x += (box.width - outer_w) / 2;
  box.y += (box.height - outer_h) / 2;
  box.width = outer_w;
  box.height = outer_h;

  // The focused window "steps forward" a few px into its own share of
  // the outer gap -- explicit user request to make the existing
  // raise-to-top-on-focus behavior more visually pronounced. This is
  // border-only bleed (apply_edge_grow() above, View::resize_border()),
  // deliberately never folded into the box below: every tile_view() call
  // here always uses the same plain, focus-independent box, so a focus
  // change never asks the client to resize (see the grow_* fields'
  // comment in view.hpp for why that used to cause a visible flicker).
  // Capped well below the full gap so there's always some residual gap
  // left even around a grown, focused window (and gap_px == 0 means
  // grow == 0: nothing to step into).
  View* focused = focused_view(server);
  int grow = std::min(6, std::max(0, gap - 1));

  if (tiled.size() == 1) {
    tile_view(tiled[0], box.x, box.y, box.width, box.height);
    apply_edge_grow(tiled[0], box, usable_area, tiled[0] == focused ? grow : 0);
    return;
  }

  int master_width = (box.width - gap) / 2;
  wlr_box master_box{box.x, box.y, master_width, box.height};
  tile_view(tiled[0], master_box.x, master_box.y, master_box.width, master_box.height);
  apply_edge_grow(tiled[0], master_box, usable_area, tiled[0] == focused ? grow : 0);

  int stack_count = static_cast<int>(tiled.size()) - 1;
  int stack_x = box.x + master_width + gap;
  int stack_width = box.width - master_width - gap;
  int stack_height = (box.height - (stack_count - 1) * gap) / stack_count;
  for (int i = 0; i < stack_count; ++i) {
    int y = box.y + i * (stack_height + gap);
    // Last stripe absorbs any remainder from integer division so the
    // stack always exactly fills the output height.
    int h = (i == stack_count - 1) ? (box.y + box.height - y) : stack_height;
    wlr_box stack_box{stack_x, y, stack_width, h};
    tile_view(tiled[i + 1], stack_box.x, stack_box.y, stack_box.width, stack_box.height);
    apply_edge_grow(tiled[i + 1], stack_box, usable_area, tiled[i + 1] == focused ? grow : 0);
  }
}

void Output::create_debug_bars() {
  wlr_box box{};
  wlr_output_layout_get_box(server->output_layout(), wlr_output_ptr, &box);
  debug_base_x_ =
      box.x + box.width - kDebugBarMargin - kDebugBarCount * (kDebugBarWidth + kDebugBarGap);
  debug_base_y_ = box.y + box.height - kDebugBarMargin;

  for (int i = 0; i < kDebugBarCount; ++i) {
    debug_bars_[i] = wlr_scene_rect_create(server->layer_debug(), kDebugBarWidth, 1,
                                            kDebugTransparent);
    wlr_scene_node_set_position(&debug_bars_[i]->node,
                                 debug_base_x_ + i * (kDebugBarWidth + kDebugBarGap),
                                 debug_base_y_ - 1);
  }
  debug_bars_created_ = true;
}

void Output::draw_debug_bars() {
  // debug_next_index_ is where the *next* sample will be written, i.e.
  // the oldest sample still in the ring -- reading from there gives
  // chronological left-to-right order (oldest on the left, most recent
  // frame on the right, matching how a scrolling graph reads).
  for (int i = 0; i < kDebugBarCount; ++i) {
    float ms = debug_frame_times_ms_[(debug_next_index_ + i) % kDebugBarCount];
    int height = static_cast<int>((ms / kDebugBarMsForFullHeight) * kDebugBarMaxHeight);
    height = std::clamp(height, 1, kDebugBarMaxHeight);

    const float* color = ms <= kDebugGoodBudgetMs   ? kDebugGreen
                          : ms <= kDebugWarnBudgetMs ? kDebugYellow
                                                      : kDebugRed;
    wlr_scene_rect_set_size(debug_bars_[i], kDebugBarWidth, height);
    wlr_scene_rect_set_color(debug_bars_[i], color);
    wlr_scene_node_set_position(&debug_bars_[i]->node,
                                 debug_base_x_ + i * (kDebugBarWidth + kDebugBarGap),
                                 debug_base_y_ - height);
  }
}

void Output::update_debug_overlay() {
  if (!server->debug_overlay_enabled()) {
    // Reset rather than leave stale: otherwise re-enabling after a long
    // gap would compute one giant bogus "frame time" spanning the
    // entire disabled period.
    debug_has_last_frame_time_ = false;
    return;
  }

  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (debug_has_last_frame_time_) {
    double delta_ms = (now.tv_sec - debug_last_frame_time_.tv_sec) * 1000.0 +
                       (now.tv_nsec - debug_last_frame_time_.tv_nsec) / 1e6;
    debug_frame_times_ms_[debug_next_index_] = static_cast<float>(delta_ms);
    debug_next_index_ = (debug_next_index_ + 1) % kDebugBarCount;
  }
  debug_last_frame_time_ = now;
  debug_has_last_frame_time_ = true;

  if (!debug_bars_created_) {
    create_debug_bars();
  }
  draw_debug_bars();

  if (!debug_frame_time_row_.created) {
    create_debug_text_rows();
  }
  update_debug_text();
}

void Output::create_debug_text_rows() {
  constexpr int kRowHeightPx = kDebugGlyphHeight * kDebugGlyphPixelSize;
  int top_of_bars_y = debug_base_y_ - kDebugBarMaxHeight;
  int cpu_y = top_of_bars_y - kDebugTextRowGapPx - kRowHeightPx;
  int ram_y = cpu_y - kDebugTextRowGapPx - kRowHeightPx;
  int fps_y = ram_y - kDebugTextRowGapPx - kRowHeightPx;
  int renderer_y = fps_y - kDebugTextRowGapPx - kRowHeightPx;

  create_text_row(server->layer_debug(), debug_frame_time_row_, debug_base_x_, fps_y);
  create_text_row(server->layer_debug(), debug_ram_row_, debug_base_x_, ram_y);
  create_text_row(server->layer_debug(), debug_cpu_row_, debug_base_x_, cpu_y);
  create_text_row(server->layer_debug(), debug_renderer_row_, debug_base_x_, renderer_y);

  // Set once, not on the 500ms refresh cadence in update_debug_text() --
  // the renderer backend is fixed for the process's lifetime.
  render_text_row(debug_renderer_row_, debug_renderer_name(server->renderer()),
                   kDebugTextColor);
}

void Output::update_debug_text() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (debug_has_last_text_update_) {
    double elapsed_ms = (now.tv_sec - debug_last_text_update_.tv_sec) * 1000.0 +
                         (now.tv_nsec - debug_last_text_update_.tv_nsec) / 1e6;
    if (elapsed_ms < kDebugTextIntervalMs) {
      return;
    }
  }
  debug_last_text_update_ = now;
  debug_has_last_text_update_ = true;

  // Average over the ring buffer's real samples only -- zeros from
  // never-yet-written slots (e.g. right after enabling the overlay)
  // would otherwise drag the average down misleadingly.
  double sum_ms = 0;
  int count = 0;
  for (float ms : debug_frame_times_ms_) {
    if (ms > 0.0f) {
      sum_ms += ms;
      ++count;
    }
  }
  int fps = count > 0 ? static_cast<int>(1000.0 / (sum_ms / count)) : 0;

  char buf[DebugTextRow::kMaxChars + 1];
  std::snprintf(buf, sizeof(buf), "%dFPS", fps);
  render_text_row(debug_frame_time_row_, buf, kDebugTextColor);

  int rss_mb = read_self_rss_mb();
  if (rss_mb >= 0) {
    std::snprintf(buf, sizeof(buf), "%dMB", rss_mb);
    render_text_row(debug_ram_row_, buf, kDebugTextColor);
  }

  int cpu_mhz = read_cpu_mhz();
  if (cpu_mhz >= 0) {
    std::snprintf(buf, sizeof(buf), "%dMHz", cpu_mhz);
    render_text_row(debug_cpu_row_, buf, kDebugTextColor);
  }
}

}  // namespace fleetwm
