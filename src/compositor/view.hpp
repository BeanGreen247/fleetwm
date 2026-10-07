#pragma once

#include <wayland-server-core.h>

#include <cstdint>
#include <string>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include "config.h"
#include "scene_node_owner.hpp"
#include "titlebar.hpp"

#if FLEETWM_XWAYLAND
extern "C" {
#include <wlr/xwayland.h>
}
#endif

namespace fleetwm {

class Server;
class Workspace;
class Output;

// A toplevel window, native (xdg_toplevel) or XWayland. Phase 1's
// master-stack algorithm lives in Output::relayout(), driven externally;
// View just exposes the pinned/floating flags relayout() reads to decide
// whether to skip a view, plus the border-rect nodes it (and focus
// tracking) render into.
class View;

// Tags a scene node that belongs to a View's decorations (titlebar, resize
// ring) so the hit test can tell it apart from client content. Must start with
// the SceneNodeOwner for the same reason View does.
struct DecorationTag {
  SceneNodeOwner owner = SceneNodeOwner::Decoration;
  View* view = nullptr;
};

class View {
 public:
  enum class Kind { XdgToplevel, XWayland };

  View(Server* server, Kind kind);
  ~View();

  // Must stay first: hit-testing (server.cpp) reads a scene node's
  // node.data as SceneNodeOwner* before reinterpreting further.
  SceneNodeOwner scene_node_owner = SceneNodeOwner::View;

  Server* server;
  Kind kind;
  Workspace* workspace = nullptr;
  // Set at map time alongside workspace; unmap needs it to trigger a
  // relayout of the remaining tiled views after workspace is cleared,
  // and relayout() itself needs it for output-box geometry.
  Output* output = nullptr;

  // container_tree_ is the outer wrapper: it holds the four border rects
  // plus scene_tree (the actual surface content) as a child, offset by
  // border_thickness so the border frames the content rather than
  // overlapping it. Positioning/reparenting-for-pin acts on
  // container_tree_; hit-testing and focus-raise still act on scene_tree,
  // since that's what carries the SceneNodeOwner tag (see
  // scene_node_at() in server.cpp, which walks up past untagged
  // ancestors like container_tree_ to find it).
  wlr_scene_tree* container_tree = nullptr;
  wlr_scene_tree* scene_tree = nullptr;
  wlr_scene_rect* border_top = nullptr;
  wlr_scene_rect* border_bottom = nullptr;
  wlr_scene_rect* border_left = nullptr;
  wlr_scene_rect* border_right = nullptr;
  wlr_xdg_toplevel* xdg_toplevel = nullptr;

  // Whether this view is pinned always-on-top (PowerToys-style): its
  // container_tree lives in Server's layer_pinned_ tree instead of
  // layer_toplevels_ while pinned, so it stays visible and on top across
  // every workspace switch, not just within its own workspace -- see
  // Output::switch_workspace(), which never touches layer_pinned_.
  bool pinned = false;

  // Opts this view out of Output::relayout()'s master-stack math,
  // independent of pinned -- floating (unlike pinned) keeps the view in
  // its normal workspace/layer_toplevels_ position, just skipped by
  // tiling so it keeps whatever position/size it last had.
  bool floating = false;

  // True while this view holds fullscreen state (client-requested via
  // xdg_toplevel.set_fullscreen, e.g. a game or video player) --
  // acknowledged with wlr_xdg_toplevel_set_fullscreen() and reparented
  // into Server's layer_fullscreen_ tree (above the bar, below any
  // layer-shell overlay client) at exactly the output's full geometry,
  // border-less. Skipped by Output::relayout() the same way
  // pinned/floating are; a single unoccluded fullscreen buffer exactly
  // matching the output is also what makes wlr_scene_output_commit's own
  // built-in direct-scanout path eligible to kick in -- no separate
  // scanout code needed here, wlr_scene already does this automatically
  // once the precondition (this) is met.
  bool fullscreen = false;

  // Stays raised above every other view *within its own workspace*,
  // re-asserted on every focus change (Server::focus_view) -- unlike
  // pinned, this does not move the view to layer_pinned_ or bypass
  // Output::switch_workspace(), so it's still hidden/shown with whatever
  // workspace it was opened into, it just never loses the top spot to
  // some other view being focused/raised while visible there. Currently
  // only set for fleetwm-settings (server.cpp's xdg_toplevel_map, matched
  // by app_id) -- explicit user request for the settings panel to always
  // be reachable on top without following you across workspaces.
  bool always_on_top = false;
  // Fleetwm's own panels (Settings, Shortcuts, the language checklist): on top in the Tiling layout,
  // ordinary windows in the Desktop layout (pin one to keep it above).
  bool fleetwm_panel = false;

  // Per-edge "step forward" amount (px) set by Output::relayout() for
  // whichever tiled view is currently focused -- resize_border() bleeds
  // the border rects outward by these amounts (into the outer gap) on
  // top of the view's normal border thickness. Deliberately NOT part of
  // the box passed to tile_view()/wlr_xdg_toplevel_set_size(): changing
  // the client's actual content size on every focus switch would make
  // resize_border() bleed outward by these amounts -- the client resize
  // is async, so the border/position would visibly update a frame or
  // more before the client's buffer catches up, reading as a flicker/
  // jump on every focus change. Since these only ever affect border
  // rects (independent scene nodes with no clipping to the container's
  // "logical box"), the content position/size and the client's own
  // resize cycle are completely unaffected -- purely cosmetic border
  // bleed, always synchronous.
  int grow_left = 0;
  int grow_top = 0;
  int grow_right = 0;
  int grow_bottom = 0;

  // Last content size actually requested via wlr_xdg_toplevel_set_size()
  // (output.cpp's tile_view()) -- lets relayout() skip re-requesting a
  // size that hasn't changed. wlr_xdg_toplevel_set_size() schedules a
  // fresh configure/serial unconditionally on every call, with no
  // internal dedup against the previous request; relayout() runs far
  // more often than the tiled layout actually changes (e.g. once a
  // second just from the bar's own clock-tick surface commit, via
  // Output::update_usable_area() -> relayout()), so calling it
  // unconditionally produced a steady stream of needless configure/
  // ack_configure round-trips -- observed over WAYLAND_DEBUG firing every
  // ~1s indefinitely, regardless of any real user action, which is what
  // read as "gaps are glitchy". -1 sentinel so the very first tile_view()
  // call always sends the initial size.
  int last_requested_content_w = -1;
  int last_requested_content_h = -1;

  // Whether this view currently holds keyboard focus -- Server::focus_view
  // sets this on the old/new focused view on every focus change and calls
  // resize_border() so the focus indicator stays in sync without every
  // caller needing to know about borders.
  bool focused = false;

  // Sets/clears the border color and thickness used to indicate pinned
  // state. Safe to call before the view has mapped (border rects exist
  // from construction; resize_border() below applies real dimensions
  // once the surface's actual size is known at map time).
  void set_pinned(bool pinned);
  // Puts the window in the scene layer that matches its state: fullscreen, always on
  // top (Settings, dialogs), pinned, or an ordinary window.
  void update_stacking_layer();
  // Tells the client its size is decided by the compositor (xdg "tiled" on all edges), so
  // terminals and other apps keep the window size when their content changes (a bigger
  // font should shrink the text grid, not grow the window). Leaves dialogs and the
  // settings window, which size themselves, alone.
  void update_size_policy();
  void set_floating(bool floating);
  // Requests/clears fullscreen: acknowledges the client's request via
  // wlr_xdg_toplevel_set_fullscreen(), reparents container_tree into/out
  // of Server::layer_fullscreen(), and positions/sizes it directly at the
  // owning output's full geometry (bypassing gaps/usable_area/border
  // entirely) -- or, when turning off, just triggers a relayout() so
  // normal tiling picks the view back up. No-op if `output` is null (a
  // view can't be sized to an output it isn't mapped on yet).
  void set_fullscreen(bool fullscreen);
  void resize_border();

  // Current border thickness in px, per the same pinned/focused priority
  // resize_border() uses internally -- relayout() needs this to size the
  // toplevel's content area (container_tree's box minus border) rather
  // than the outer box, since wlr_xdg_toplevel_set_size sets the client's
  // surface size, not container_tree's.
  int border_thickness() const;

  wl_listener map{};
  wl_listener unmap{};
  wl_listener destroy{};
  // ---- Desktop (floating) layout: compositor-drawn titlebar ----
  // True when the client asked for server-side decorations through
  // xdg-decoration (set from the decoration handler in server.cpp).
  bool has_decoration = false;
  // Stable id shown to taskbar clients over IPC (0 = not assigned yet).
  uint32_t id = 0;
  // Hidden by the user (titlebar button or taskbar); restored from the taskbar.
  bool minimized = false;
  void set_minimized(bool minimized);
  bool maximized = false;
  wlr_box pre_fullscreen_box{};  // same, for leaving fullscreen
  wlr_box restore_box{};  // container position + content size before maximize
  DecorationTag tag;
  wlr_scene_buffer* titlebar = nullptr;
  // Opaque backdrop behind a window that has been given a fixed slot (snapped,
  // maximized, or placed by the tiling->desktop switch). Clients such as terminals
  // can only size in whole cells, so without it the leftover pixels would show
  // the wallpaper as gaps between neighbouring windows.
  wlr_scene_rect* fill_rect = nullptr;
  // The slot this window holds in a tiled arrangement (index of count), recomputed
  // from the current work area so it follows a moved or resized taskbar.
  size_t placed_index = 0, placed_count = 0;
  bool has_placed = false;
  wlr_scene_rect* grab_rect = nullptr;  // invisible ring around the window: resize handles
  int content_w = 0;                    // last known content width
  int hover_button = geom::kBtnNone;   // geom::TitleButton under the pointer, or -1

  // Desktop layout is active in theme.toml.
  bool desktop_mode() const;
  // Whether this view currently shows a titlebar (Desktop layout, not
  // fullscreen, and the client expects the compositor to decorate it).
  bool wants_titlebar() const;
  int titlebar_height() const;
  // Re-renders the titlebar if its inputs changed; hides it when not wanted.
  void update_titlebar();
  void set_hover_button(int button);
  // Forces the next update_titlebar() to re-render (palette/theme changed).
  void invalidate_titlebar() {
    titlebar_w_ = -1;
    frame_key_ = {};
  }
  void set_maximized(bool maximized);
  // Re-applies the maximized geometry after the output's work area changed.
  void refit_maximized();

  // Snapping (Desktop layout): halves/quarters of the work area. The size and
  // place the window had before are kept in restore_box (shared with maximize).
  geom::SnapZone snap_zone = geom::SnapZone::None;
  void snap_to(geom::SnapZone zone);
  void refit_snapped();
  // Puts the window in slot `index` of `count` of the tiled arrangement (see
  // geom::tile_boxes) without recording a snap zone -- for arrangements that have
  // none, like a stack of four. refit_placed() redoes it after the work area changes.
  void place_tile(size_t index, size_t count);
  void refit_placed();
  // Undoes maximize or a snap, putting the window back where it was.
  void restore_from_snap();

  wl_listener request_move{};
  wl_listener request_maximize{};
  wl_listener set_title{};
  wl_listener request_resize{};
  wl_listener request_fullscreen{};
  wl_listener surface_commit{};
  wl_listener new_popup{};

#if FLEETWM_XWAYLAND
  wlr_xwayland_surface* xwayland_surface = nullptr;
  wl_listener request_configure{};
  wl_listener x_associate{};
  wl_listener x_dissociate{};
  wl_listener x_request_activate{};
  wl_listener x_request_minimize{};
  wl_listener x_set_class{};
  wl_listener x_scene_destroy{};
  wlr_scene_surface* x_scene = nullptr;  // the X11 window's pixels, present while it is mapped
  int x_sent_x = 0, x_sent_y = 0, x_sent_w = -1, x_sent_h = -1;
#endif

  wlr_surface* surface() const;

  // ---- Windows from either shell: xdg-shell toplevels and X11 windows (XWayland) ----
  // Everything that places, sizes or labels a window goes through these, so tiling, snapping,
  // titlebars and the taskbar treat both kinds the same.
  bool is_window() const;        // a real application window (not a popup or menu)
  bool is_child_window() const;  // a dialog or tool window belonging to another window
  wlr_box content_geometry() const;  // size of the client's content (x, y are 0)
  void request_size(int w, int h);   // asks the client for a content size (and tells X11 where it is)
  void set_activated(bool activated);
  const char* window_title() const;   // may be null
  const char* window_app_id() const;  // xdg app id, or the X11 class; may be null
  // X11 clients have to be told where their window is; call after the position changes.
  void sync_x11_position();

 private:
  // What the current titlebar buffer was rendered from.
  int titlebar_w_ = -1;
  int titlebar_h_ = -1;
  TitlebarState rendered_;
  // The sides and bottom of the Windows 7 style frame (the top is part of the titlebar buffer).
  wlr_scene_buffer* frame_left_ = nullptr;
  wlr_scene_buffer* frame_right_ = nullptr;
  wlr_scene_buffer* frame_bottom_ = nullptr;
  struct FrameKey {
    int w = -1, h = -1, th = 0, px = 0;
    bool focused = false, glass = false;
    bool operator==(const FrameKey& o) const {
      return w == o.w && h == o.h && th == o.th && px == o.px && focused == o.focused && glass == o.glass;
    }
  };
  FrameKey frame_key_;
  void update_frame(int content_h);
 public:
  void focus();
  void close();
};

// Creates the border rectangles, the fill and the resize ring inside view->container_tree.
void create_view_rects(View* view);

}  // namespace fleetwm
