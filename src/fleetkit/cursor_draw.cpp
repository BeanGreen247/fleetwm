#include "cursor_draw.hpp"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace fleetwm::kit {

namespace {

struct Alias {
  const char* name;
  CursorShape shape;
};

// The names the compositor, wp_cursor_shape_v1 (as wlroots spells them), CSS and the common xcursor themes use.
constexpr Alias kAliases[] = {
    {"left_ptr", CursorShape::Arrow}, {"default", CursorShape::Arrow}, {"arrow", CursorShape::Arrow},
    {"top_left_arrow", CursorShape::Arrow}, {"context-menu", CursorShape::Arrow}, {"alias", CursorShape::Arrow},
    {"copy", CursorShape::Arrow}, {"dnd-copy", CursorShape::Arrow}, {"dnd-move", CursorShape::Arrow}, {"cell", CursorShape::Cross},
    {"help", CursorShape::Help}, {"question_arrow", CursorShape::Help}, {"whats_this", CursorShape::Help},
    {"progress", CursorShape::Progress}, {"left_ptr_watch", CursorShape::Progress}, {"half-busy", CursorShape::Progress},
    {"wait", CursorShape::Wait}, {"watch", CursorShape::Wait},
    {"text", CursorShape::Text}, {"xterm", CursorShape::Text}, {"ibeam", CursorShape::Text}, {"vertical-text", CursorShape::Text},
    {"pointer", CursorShape::Hand}, {"hand1", CursorShape::Hand}, {"hand2", CursorShape::Hand}, {"pointing_hand", CursorShape::Hand},
    {"grab", CursorShape::Hand}, {"grabbing", CursorShape::Hand}, {"openhand", CursorShape::Hand}, {"closedhand", CursorShape::Hand},
    {"dnd-link", CursorShape::Hand},
    {"crosshair", CursorShape::Cross}, {"cross", CursorShape::Cross}, {"tcross", CursorShape::Cross},
    {"not-allowed", CursorShape::NotAllowed}, {"crossed_circle", CursorShape::NotAllowed}, {"no-drop", CursorShape::NotAllowed},
    {"forbidden", CursorShape::NotAllowed}, {"dnd-no-drop", CursorShape::NotAllowed},
    {"move", CursorShape::Move}, {"all-scroll", CursorShape::Move}, {"fleur", CursorShape::Move}, {"size_all", CursorShape::Move},
    {"ew-resize", CursorShape::ResizeEW}, {"col-resize", CursorShape::ResizeEW}, {"e-resize", CursorShape::ResizeEW},
    {"w-resize", CursorShape::ResizeEW}, {"sb_h_double_arrow", CursorShape::ResizeEW}, {"h_double_arrow", CursorShape::ResizeEW},
    {"split_h", CursorShape::ResizeEW}, {"size_hor", CursorShape::ResizeEW}, {"left_side", CursorShape::ResizeEW},
    {"right_side", CursorShape::ResizeEW},
    {"ns-resize", CursorShape::ResizeNS}, {"row-resize", CursorShape::ResizeNS}, {"n-resize", CursorShape::ResizeNS},
    {"s-resize", CursorShape::ResizeNS}, {"sb_v_double_arrow", CursorShape::ResizeNS}, {"v_double_arrow", CursorShape::ResizeNS},
    {"split_v", CursorShape::ResizeNS}, {"size_ver", CursorShape::ResizeNS}, {"top_side", CursorShape::ResizeNS},
    {"bottom_side", CursorShape::ResizeNS},
    {"nesw-resize", CursorShape::ResizeNESW}, {"ne-resize", CursorShape::ResizeNESW}, {"sw-resize", CursorShape::ResizeNESW},
    {"fd_double_arrow", CursorShape::ResizeNESW}, {"size_bdiag", CursorShape::ResizeNESW}, {"top_right_corner", CursorShape::ResizeNESW},
    {"bottom_left_corner", CursorShape::ResizeNESW},
    {"nwse-resize", CursorShape::ResizeNWSE}, {"nw-resize", CursorShape::ResizeNWSE}, {"se-resize", CursorShape::ResizeNWSE},
    {"bd_double_arrow", CursorShape::ResizeNWSE}, {"size_fdiag", CursorShape::ResizeNWSE}, {"top_left_corner", CursorShape::ResizeNWSE},
    {"bottom_right_corner", CursorShape::ResizeNWSE},
    {"up_arrow", CursorShape::UpArrow}, {"center_ptr", CursorShape::UpArrow},
};

// ---- paint helpers -----------------------------------------------------------------------------------------------

struct Style {
  bool glass;
};

// Fills the current path (kept) with the style's body and strokes it with the outline.
//   flat:  solid white, solid black 1 px outline.
//   glass: white to pale blue-grey vertical gradient, dark outline with a light inner rim, soft shadow underneath.
void paint_body(cairo_t* cr, const Style& st, double y0, double y1, bool shadow = true) {
  cairo_path_t* path = cairo_copy_path(cr);
  if (st.glass && shadow) {  // the Windows pointer shadow: the same shape pushed down and right, three faint layers
    for (int i = 3; i >= 1; --i) {
      cairo_save(cr);
      cairo_translate(cr, 0.7 * i, 0.9 * i);
      cairo_new_path(cr);
      cairo_append_path(cr, path);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
      cairo_set_line_width(cr, 1.0 + 0.5 * i);
      cairo_fill_preserve(cr);
      cairo_stroke(cr);
      cairo_restore(cr);
    }
  }
  cairo_new_path(cr);
  cairo_append_path(cr, path);
  if (st.glass) {
    cairo_pattern_t* g = cairo_pattern_create_linear(0, y0, 0, y1);
    cairo_pattern_add_color_stop_rgb(g, 0.0, 1.0, 1.0, 1.0);
    cairo_pattern_add_color_stop_rgb(g, 0.55, 0.96, 0.97, 0.99);
    cairo_pattern_add_color_stop_rgb(g, 1.0, 0.78, 0.82, 0.88);
    cairo_set_source(cr, g);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(g);
  } else {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill_preserve(cr);
  }
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 0, 0, 0, st.glass ? 0.92 : 1.0);
  cairo_stroke_preserve(cr);
  if (st.glass) {  // light rim just inside the outline
    cairo_save(cr);
    cairo_clip(cr);
    cairo_new_path(cr);
    cairo_append_path(cr, path);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
    cairo_set_line_width(cr, 1.6);
    cairo_stroke(cr);
    cairo_restore(cr);
    cairo_new_path(cr);
    cairo_append_path(cr, path);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.92);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
  cairo_path_destroy(path);
  cairo_new_path(cr);
}

void arrow_path(cairo_t* cr, double ox = 0, double oy = 0) {
  cairo_new_path(cr);
  cairo_move_to(cr, ox + 0.5, oy + 0.5);
  cairo_line_to(cr, ox + 0.5, oy + 17.5);
  cairo_line_to(cr, ox + 4.4, oy + 13.7);
  cairo_line_to(cr, ox + 7.1, oy + 19.8);
  cairo_line_to(cr, ox + 10.0, oy + 18.5);
  cairo_line_to(cr, ox + 7.3, oy + 12.6);
  cairo_line_to(cr, ox + 12.6, oy + 12.6);
  cairo_close_path(cr);
}

// The busy ring. Glass: teal glass ring with a bright top edge; flat: a single blue ring.
void ring(cairo_t* cr, const Style& st, double cx, double cy, double r, double w, double spin) {
  cairo_save(cr);
  // white halo so it reads on dark backgrounds
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1, 1, 1, st.glass ? 0.55 : 0.9);
  cairo_set_line_width(cr, w + 2.2);
  cairo_stroke(cr);
  if (st.glass) {
    cairo_pattern_t* g = cairo_pattern_create_linear(cx, cy - r - w / 2, cx, cy + r + w / 2);
    cairo_pattern_add_color_stop_rgb(g, 0.0, 0.76, 0.98, 0.97);
    cairo_pattern_add_color_stop_rgb(g, 0.5, 0.18, 0.78, 0.82);
    cairo_pattern_add_color_stop_rgb(g, 1.0, 0.05, 0.50, 0.58);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_set_source(cr, g);
    cairo_set_line_width(cr, w);
    cairo_stroke(cr);
    cairo_pattern_destroy(g);
    cairo_new_path(cr);  // glossy highlight along the upper left
    cairo_arc(cr, cx, cy, r + w * 0.15, M_PI * 1.05 + spin, M_PI * 1.75 + spin);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.75);
    cairo_set_line_width(cr, w * 0.3);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
    cairo_new_path(cr);  // thin dark teal edges
    cairo_arc(cr, cx, cy, r + w / 2, 0, 2 * M_PI);
    cairo_arc_negative(cr, cx, cy, r - w / 2, 2 * M_PI, 0);
    cairo_set_source_rgba(cr, 0.0, 0.25, 0.30, 0.8);
    cairo_set_line_width(cr, 0.7);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 0.16, 0.50, 0.86);
    cairo_set_line_width(cr, w);
    cairo_stroke(cr);
    cairo_new_path(cr);  // a light quarter that turns with the phase
    cairo_arc(cr, cx, cy, r, M_PI * 1.05 + spin, M_PI * 1.75 + spin);
    cairo_set_source_rgb(cr, 0.62, 0.82, 0.98);
    cairo_set_line_width(cr, w);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

void question_mark(cairo_t* cr, const Style& st, double cx, double cy) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  for (int pass = 0; pass < 2; ++pass) {  // white halo, then the dark mark
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy - 2.2, 3.0, -M_PI * 1.05, M_PI * 0.45);
    cairo_line_to(cr, cx, cy + 2.6);
    cairo_move_to(cr, cx, cy + 5.4);
    cairo_line_to(cr, cx, cy + 5.4);
    if (pass == 0) {
      cairo_set_source_rgba(cr, 1, 1, 1, 1);
      cairo_set_line_width(cr, 4.4);
    } else {
      cairo_set_source_rgba(cr, 0.05, 0.05, 0.05, st.glass ? 0.95 : 1.0);
      cairo_set_line_width(cr, 2.0);
    }
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

void ibeam(cairo_t* cr, const Style& st) {
  cairo_new_path(cr);
  cairo_move_to(cr, 11.5, 5.5);
  cairo_line_to(cr, 20.5, 5.5);
  cairo_line_to(cr, 20.5, 7.5);
  cairo_line_to(cr, 17.0, 7.5);
  cairo_line_to(cr, 17.0, 24.5);
  cairo_line_to(cr, 20.5, 24.5);
  cairo_line_to(cr, 20.5, 26.5);
  cairo_line_to(cr, 11.5, 26.5);
  cairo_line_to(cr, 11.5, 24.5);
  cairo_line_to(cr, 15.0, 24.5);
  cairo_line_to(cr, 15.0, 7.5);
  cairo_line_to(cr, 11.5, 7.5);
  cairo_close_path(cr);
  paint_body(cr, st, 5, 27);
}

void hand(cairo_t* cr, const Style& st) {
  // index finger up, three folded fingers, palm, thumb: one outline built from rounded pieces
  cairo_new_path(cr);
  cairo_move_to(cr, 11.0, 4.5);
  cairo_curve_to(cr, 11.0, 2.4, 14.8, 2.4, 14.8, 4.5);
  cairo_line_to(cr, 14.8, 12.2);
  cairo_curve_to(cr, 15.4, 11.2, 18.0, 11.2, 18.2, 12.6);   // second finger
  cairo_curve_to(cr, 19.0, 11.6, 21.4, 11.8, 21.6, 13.2);   // third finger
  cairo_curve_to(cr, 22.6, 12.6, 24.8, 13.0, 24.8, 14.8);   // fourth finger
  cairo_line_to(cr, 24.8, 20.5);
  cairo_curve_to(cr, 24.8, 24.5, 22.2, 27.5, 18.5, 27.5);
  cairo_line_to(cr, 14.2, 27.5);
  cairo_curve_to(cr, 12.4, 27.5, 11.2, 26.6, 10.2, 25.2);
  cairo_line_to(cr, 5.8, 19.4);
  cairo_curve_to(cr, 4.8, 18.0, 6.4, 16.4, 7.8, 17.4);       // thumb
  cairo_line_to(cr, 11.0, 20.2);
  cairo_close_path(cr);
  paint_body(cr, st, 3, 28);
  // finger separations
  cairo_set_source_rgba(cr, 0, 0, 0, st.glass ? 0.55 : 0.8);
  cairo_set_line_width(cr, 0.8);
  for (double x : {18.2, 21.6}) {
    cairo_move_to(cr, x, 13.0);
    cairo_line_to(cr, x, 19.5);
  }
  cairo_stroke(cr);
}

void cross(cairo_t* cr, const Style& st) {
  for (int pass = 0; pass < 2; ++pass) {
    cairo_new_path(cr);
    cairo_move_to(cr, 16.5, 8.5);
    cairo_line_to(cr, 16.5, 24.5);
    cairo_move_to(cr, 8.5, 16.5);
    cairo_line_to(cr, 24.5, 16.5);
    if (pass == 0) {
      cairo_set_source_rgba(cr, 1, 1, 1, st.glass ? 0.9 : 1.0);
      cairo_set_line_width(cr, 3.0);
    } else {
      cairo_set_source_rgba(cr, 0, 0, 0, st.glass ? 0.92 : 1.0);
      cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);
  }
}

void not_allowed(cairo_t* cr, const Style& st) {
  const double cx = 16, cy = 16, r = 9;
  cairo_save(cr);
  if (st.glass) {
    for (int i = 3; i >= 1; --i) {  // shadow
      cairo_new_path(cr);
      cairo_arc(cr, cx + 0.7 * i, cy + 0.9 * i, r, 0, 2 * M_PI);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
      cairo_set_line_width(cr, 4.2 + 0.5 * i);
      cairo_stroke(cr);
    }
  }
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
  cairo_set_line_width(cr, 6.4);
  cairo_stroke(cr);
  if (st.glass) {
    cairo_pattern_t* g = cairo_pattern_create_linear(0, cy - r, 0, cy + r);
    cairo_pattern_add_color_stop_rgb(g, 0.0, 0.96, 0.45, 0.40);
    cairo_pattern_add_color_stop_rgb(g, 1.0, 0.68, 0.06, 0.08);
    cairo_set_source(cr, g);
    cairo_pattern_destroy(g);
  } else {
    cairo_set_source_rgb(cr, 0.80, 0.14, 0.12);
  }
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_set_line_width(cr, 3.6);
  cairo_stroke_preserve(cr);
  cairo_new_path(cr);
  cairo_move_to(cr, cx - r * 0.7, cy - r * 0.7);
  cairo_line_to(cr, cx + r * 0.7, cy + r * 0.7);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  cairo_stroke(cr);
  cairo_restore(cr);
}

// A double-headed arrow along x, centred at (16, 16), half length `half` (the heads are 6 long, the shaft 3.6 wide).
void double_arrow_path(cairo_t* cr, double half) {
  const double h = 6.2, s = 1.8, w = 5.4;
  cairo_new_path(cr);
  cairo_move_to(cr, 16 - half, 16);
  cairo_line_to(cr, 16 - half + h, 16 - w);
  cairo_line_to(cr, 16 - half + h, 16 - s);
  cairo_line_to(cr, 16 + half - h, 16 - s);
  cairo_line_to(cr, 16 + half - h, 16 - w);
  cairo_line_to(cr, 16 + half, 16);
  cairo_line_to(cr, 16 + half - h, 16 + w);
  cairo_line_to(cr, 16 + half - h, 16 + s);
  cairo_line_to(cr, 16 - half + h, 16 + s);
  cairo_line_to(cr, 16 - half + h, 16 + w);
  cairo_close_path(cr);
}

void move_path(cairo_t* cr) {
  const double a = 12.5, h = 5.2, w = 5.0, s = 2.3;
  cairo_new_path(cr);
  for (int i = 0; i < 4; ++i) {
    cairo_save(cr);
    cairo_translate(cr, 16, 16);
    cairo_rotate(cr, i * M_PI / 2);
    // one arm pointing up: arrowhead, then the shaft down to the centre
    if (i == 0) cairo_move_to(cr, -s, -s);
    else cairo_line_to(cr, -s, -s);
    cairo_line_to(cr, -s, -a + h);
    cairo_line_to(cr, -w, -a + h);
    cairo_line_to(cr, 0, -a);
    cairo_line_to(cr, w, -a + h);
    cairo_line_to(cr, s, -a + h);
    cairo_line_to(cr, s, -s);
    cairo_restore(cr);
  }
  cairo_close_path(cr);
}

void up_arrow_path(cairo_t* cr) {
  cairo_new_path(cr);
  cairo_move_to(cr, 16, 3.5);
  cairo_line_to(cr, 23, 12.5);
  cairo_line_to(cr, 18.6, 12.5);
  cairo_line_to(cr, 18.6, 27.5);
  cairo_line_to(cr, 13.4, 27.5);
  cairo_line_to(cr, 13.4, 12.5);
  cairo_line_to(cr, 9, 12.5);
  cairo_close_path(cr);
}

}  // namespace

bool cursor_shape_for_name(const char* name, CursorShape* out) {
  if (!name) return false;
  for (const Alias& a : kAliases)
    if (std::strcmp(a.name, name) == 0) {
      *out = a.shape;
      return true;
    }
  return false;
}

CursorHotspot cursor_hotspot(CursorShape shape) {
  switch (shape) {
    case CursorShape::Arrow:
    case CursorShape::Help:
    case CursorShape::Progress: return {0, 0};
    case CursorShape::Hand: return {13, 3};
    case CursorShape::UpArrow: return {16, 4};
    default: return {16, 16};  // ring, I-beam, cross, sign and the resize and move arrows are centred
  }
}

void draw_cursor(cairo_t* cr, CursorShape shape, bool glass, int scale, int phase) {
  const double spin = 2 * M_PI * (phase % kBusyFrames) / kBusyFrames;
  const Style st{glass};
  cairo_save(cr);
  cairo_scale(cr, scale < 1 ? 1 : scale, scale < 1 ? 1 : scale);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
  switch (shape) {
    case CursorShape::Arrow:
      arrow_path(cr);
      paint_body(cr, st, 0, 20);
      break;
    case CursorShape::Help:
      arrow_path(cr);
      paint_body(cr, st, 0, 20);
      question_mark(cr, st, 19.5, 21.0);
      break;
    case CursorShape::Progress:
      arrow_path(cr);
      paint_body(cr, st, 0, 20);
      ring(cr, st, 21.5, 21.5, 4.6, 2.6, spin);
      break;
    case CursorShape::Wait: ring(cr, st, 16, 16, 8.6, 4.2, spin); break;
    case CursorShape::Text: ibeam(cr, st); break;
    case CursorShape::Hand: hand(cr, st); break;
    case CursorShape::Cross: cross(cr, st); break;
    case CursorShape::NotAllowed: not_allowed(cr, st); break;
    case CursorShape::Move:
      move_path(cr);
      paint_body(cr, st, 4, 28);
      break;
    case CursorShape::ResizeEW:
      double_arrow_path(cr, 12.5);
      paint_body(cr, st, 10, 22);
      break;
    case CursorShape::ResizeNS:
      cairo_translate(cr, 16, 16);
      cairo_rotate(cr, M_PI / 2);
      cairo_translate(cr, -16, -16);
      double_arrow_path(cr, 12.5);
      paint_body(cr, st, 10, 22);
      break;
    case CursorShape::ResizeNESW:
      cairo_translate(cr, 16, 16);
      cairo_rotate(cr, -M_PI / 4);
      cairo_translate(cr, -16, -16);
      double_arrow_path(cr, 12.0);
      paint_body(cr, st, 10, 22);
      break;
    case CursorShape::ResizeNWSE:
      cairo_translate(cr, 16, 16);
      cairo_rotate(cr, M_PI / 4);
      cairo_translate(cr, -16, -16);
      double_arrow_path(cr, 12.0);
      paint_body(cr, st, 10, 22);
      break;
    case CursorShape::UpArrow:
      up_arrow_path(cr);
      paint_body(cr, st, 3, 28);
      break;
    case CursorShape::Count: break;
  }
  cairo_restore(cr);
}

}  // namespace fleetwm::kit
