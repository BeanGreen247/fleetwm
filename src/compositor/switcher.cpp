#include "switcher.hpp"

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <string>

extern "C" {
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
}

#include "backdrop.hpp"
#include "fleetkit.hpp"
#include "output.hpp"
#include "pixel_buffer.hpp"
#include "server.hpp"
#include "view.hpp"

namespace fleetwm {

namespace {

namespace kit = fleetwm::kit;

constexpr int kPad = 20, kGap = 14, kTitleH = 40, kLabelH = 26, kThumbMaxW = 220, kThumbMaxH = 140, kThumbMinW = 96;

// The window title, cut with "..." to fit `max_w` px.
std::string fit(cairo_t* cr, std::string text, double px, bool bold, double max_w) {
  if (kit::measure_text(cr, text, px, bold).width <= max_w) return text;
  while (!text.empty()) {
    size_t end = text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    text.erase(end);
    if (kit::measure_text(cr, text + "...", px, bold).width <= max_w) return text + "...";
  }
  return "";
}

std::string title_of(View* v) {
  const char* title = v->window_title();
  const char* app = v->window_app_id();
  std::string t = title && *title ? title : app && *app ? app : "";
  return t.empty() ? "Window" : t;
}

}  // namespace

void WindowSwitcher::hide() {
  if (tree_) wlr_scene_node_destroy(&tree_->node);
  tree_ = nullptr;
}

void WindowSwitcher::show(const std::vector<View*>& order, size_t selected) {
  hide();
  if (order.empty() || server_->outputs.empty()) return;
  Output* output = order[std::min(selected, order.size() - 1)]->output;
  if (!output) output = server_->outputs.front().get();
  wlr_box area{};
  wlr_output_layout_get_box(server_->output_layout(), output->wlr_output_ptr, &area);
  if (area.width < 200 || area.height < 200) return;

  const int n = static_cast<int>(order.size());
  const int max_panel_w = static_cast<int>(area.width * 0.92);
  int thumb_w = kThumbMaxW;
  while (thumb_w > kThumbMinW && 2 * kPad + n * thumb_w + (n - 1) * kGap > max_panel_w) thumb_w -= 4;
  // Too many windows for one row even at the smallest size: wrap into rows.
  const int per_row = std::max(1, std::min(n, (max_panel_w - 2 * kPad + kGap) / (thumb_w + kGap)));
  const int rows = (n + per_row - 1) / per_row;
  const int thumb_h = thumb_w * kThumbMaxH / kThumbMaxW;
  const int cell_h = thumb_h + kLabelH;
  const int panel_w = 2 * kPad + per_row * thumb_w + (per_row - 1) * kGap;
  const int panel_h = 2 * kPad + kTitleH + rows * cell_h + (rows - 1) * kGap;

  const kit::Palette pal = kit::load_palette(server_->theme_config());
  void* pixels = nullptr;
  size_t stride = 0;
  wlr_buffer* chrome = create_pixel_buffer(panel_w, panel_h, &pixels, &stride);
  if (!chrome) return;
  cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32,
                                                              panel_w, panel_h, static_cast<int>(stride));
  cairo_t* cr = cairo_create(surf);

  if (server_->theme_config().glass) {
    // Glass: the frosted wallpaper behind the panel, a tint, a sheen and a light rim. The panel buffer
    // is drawn once per Alt+Tab step, so this costs nothing while idle.
    cairo_surface_t* backdrop = kit::load_backdrop();
    kit::GlassStyle st;
    st.tint = pal.glass_surface;
    st.tint_alpha = 0.62;
    st.radius = 14;
    kit::paint_glass(cr, backdrop, area.width, area.height, (area.width - panel_w) / 2.0, (area.height - panel_h) / 2.0, 0, 0,
                     panel_w, panel_h, st);
    if (backdrop) cairo_surface_destroy(backdrop);
  } else {
    // Matte: the translucent theme background with a soft light edge.
    kit::Color bg = pal.bg_primary;
    bg.a = 0.82;
    kit::rounded_rect(cr, 0.5, 0.5, panel_w - 1, panel_h - 1, 14);
    kit::set_source(cr, bg);
    cairo_fill_preserve(cr);
    kit::Color edge = pal.fg_primary;
    edge.a = 0.35;
    kit::set_source(cr, edge);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }

  // The title of the window you are switching to, centered on top.
  const size_t sel = std::min(selected, order.size() - 1);
  {
    const std::string t = fit(cr, title_of(order[sel]), 16, true, panel_w - 2 * kPad);
    const kit::TextExtents te = kit::measure_text(cr, t, 16, true);
    kit::draw_text(cr, t, (panel_w - te.width) / 2, kPad + (kTitleH - 10 - te.height) / 2 + te.ascent, 16, pal.fg_primary, true);
  }

  struct Slot {
    int x, y;
  };
  std::vector<Slot> slots;
  for (int i = 0; i < n; ++i) {
    const int row = i / per_row, col = i % per_row;
    // The last row is centered when it holds fewer tiles.
    const int in_row = row == rows - 1 ? n - row * per_row : per_row;
    const int row_w = in_row * thumb_w + (in_row - 1) * kGap;
    const int x = (panel_w - row_w) / 2 + col * (thumb_w + kGap);
    const int y = kPad + kTitleH + row * (cell_h + kGap);
    slots.push_back({x, y});
    const bool is_sel = static_cast<size_t>(i) == sel;
    // Tile background; the selected one gets the accent frame and a lighter fill.
    kit::rounded_rect(cr, x - 6 + 0.5, y - 6 + 0.5, thumb_w + 12 - 1, cell_h + 12 - 1, 8);
    kit::Color fill = is_sel ? pal.accent : pal.fg_primary;
    fill.a = is_sel ? 0.22 : 0.06;
    kit::set_source(cr, fill);
    cairo_fill_preserve(cr);
    if (is_sel) {
      kit::Color frame = pal.accent;
      frame.a = 0.95;
      kit::set_source(cr, frame);
      cairo_set_line_width(cr, 2);
      cairo_stroke(cr);
    } else {
      cairo_new_path(cr);
    }
    // Where the preview goes (dark backing so a missing or small preview still looks intentional).
    kit::Color well = pal.bg_secondary;
    well.a = 0.9;
    kit::rounded_rect(cr, x, y, thumb_w, thumb_h, 4);
    kit::set_source(cr, well);
    cairo_fill(cr);
    const std::string label = fit(cr, title_of(order[static_cast<size_t>(i)]), 12, is_sel, thumb_w - 4);
    const kit::TextExtents te = kit::measure_text(cr, label, 12, is_sel);
    kit::draw_text(cr, label, x + (thumb_w - te.width) / 2, y + thumb_h + (kLabelH - te.height) / 2 + te.ascent - 1, 12,
                   is_sel ? pal.fg_primary : pal.fg_secondary, is_sel);
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);

  tree_ = wlr_scene_tree_create(server_->layer_overlay());
  wlr_scene_node_set_position(&tree_->node, area.x + (area.width - panel_w) / 2, area.y + (area.height - panel_h) / 2);
  wlr_scene_buffer* panel = wlr_scene_buffer_create(tree_, chrome);
  wlr_buffer_drop(chrome);
  (void)panel;

  // The previews: each window's current picture, scaled down.
  for (int i = 0; i < n; ++i) {
    wlr_surface* surface = order[static_cast<size_t>(i)]->surface();
    if (!surface || !surface->mapped || surface->current.width < 1 || surface->current.height < 1) continue;
    const double sw = surface->current.width, sh = surface->current.height;
    const double scale = std::min(thumb_w / sw, thumb_h / sh);
    const int w = std::max(1, static_cast<int>(sw * scale)), h = std::max(1, static_cast<int>(sh * scale));
    if (!surface->buffer) continue;
    // A still of the window as it is now. (A scene surface would follow every new frame, but it
    // also resets its own size on each one, which undoes the scaling down to a thumbnail.)
    wlr_scene_buffer* thumb = wlr_scene_buffer_create(tree_, &surface->buffer->base);
    if (!thumb) continue;
    wlr_scene_node_set_position(&thumb->node, slots[static_cast<size_t>(i)].x + (thumb_w - w) / 2,
                                slots[static_cast<size_t>(i)].y + (thumb_h - h) / 2);
    wlr_scene_buffer_set_dest_size(thumb, w, h);
  }
}

}  // namespace fleetwm
