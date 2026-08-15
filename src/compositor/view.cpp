#include "view.hpp"

#include "server.hpp"
#include "workspace.hpp"

namespace fleetwm {

View::View(Server* server_, Kind kind_) : server(server_), kind(kind_) {}

View::~View() = default;

wlr_surface* View::wlr_surface() const {
  if (kind == Kind::XdgToplevel) {
    return xdg_toplevel ? xdg_toplevel->base->surface : nullptr;
  }
#if FLEETWM_XWAYLAND
  return xwayland_surface ? xwayland_surface->surface : nullptr;
#else
  return nullptr;
#endif
}

void View::focus() {
  server->focus_view(this);
}

void View::close() {
  if (kind == Kind::XdgToplevel && xdg_toplevel) {
    wlr_xdg_toplevel_send_close(xdg_toplevel);
    return;
  }
#if FLEETWM_XWAYLAND
  if (kind == Kind::XWayland && xwayland_surface) {
    wlr_xwayland_surface_close(xwayland_surface);
  }
#endif
}

}  // namespace fleetwm
