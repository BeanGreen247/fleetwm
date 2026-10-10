#include "icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dir_listing.hpp"

namespace fleetwm::fm {

namespace {

struct Rgb {
  double r, g, b;
};
constexpr Rgb rgb(int hex) { return {((hex >> 16) & 255) / 255.0, ((hex >> 8) & 255) / 255.0, (hex & 255) / 255.0}; }

void set(cairo_t* cr, Rgb c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }

void round_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  r = std::min({r, w / 2, h / 2});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

void fill_v(cairo_t* cr, Rgb top, Rgb bottom, double y0, double y1, bool keep = false) {
  cairo_pattern_t* p = cairo_pattern_create_linear(0, y0, 0, y1);
  cairo_pattern_add_color_stop_rgb(p, 0, top.r, top.g, top.b);
  cairo_pattern_add_color_stop_rgb(p, 1, bottom.r, bottom.g, bottom.b);
  cairo_set_source(cr, p);
  if (keep) cairo_fill_preserve(cr);
  else cairo_fill(cr);
  cairo_pattern_destroy(p);
}

void stroke(cairo_t* cr, Rgb c, double w, double a = 1) {
  set(cr, c, a);
  cairo_set_line_width(cr, w);
  cairo_stroke(cr);
}

void shadow(cairo_t* cr, double cx, double cy, double rx, double ry) {
  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_scale(cr, rx, ry);
  cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
  cairo_restore(cr);
  set(cr, {0, 0, 0}, 0.18);
  cairo_fill(cr);
}

// ---- folders ----
void folder(cairo_t* cr, bool open) {
  shadow(cr, 0.5, 0.9, 0.42, 0.045);
  // back panel and tab
  cairo_new_path(cr);
  cairo_move_to(cr, 0.07, 0.80);
  cairo_line_to(cr, 0.07, 0.19);
  cairo_curve_to(cr, 0.07, 0.16, 0.09, 0.14, 0.12, 0.14);
  cairo_line_to(cr, 0.36, 0.14);
  cairo_curve_to(cr, 0.40, 0.14, 0.41, 0.17, 0.43, 0.20);
  cairo_line_to(cr, 0.88, 0.20);
  cairo_curve_to(cr, 0.91, 0.20, 0.93, 0.22, 0.93, 0.25);
  cairo_line_to(cr, 0.93, 0.80);
  cairo_close_path(cr);
  fill_v(cr, rgb(0xd9a441), rgb(0xb67f22), 0.14, 0.8, true);
  stroke(cr, rgb(0x8a5d16), 0.018);
  // paper peeking out
  if (!open) {
    round_rect(cr, 0.14, 0.27, 0.72, 0.30, 0.02);
    set(cr, rgb(0xfdfdfd));
    cairo_fill_preserve(cr);
    stroke(cr, rgb(0x9aa4b4), 0.012);
  }
  // front panel
  cairo_new_path(cr);
  if (open) {
    cairo_move_to(cr, 0.02, 0.84);
    cairo_line_to(cr, 0.16, 0.40);
    cairo_line_to(cr, 0.98, 0.40);
    cairo_line_to(cr, 0.86, 0.84);
  } else {
    cairo_move_to(cr, 0.05, 0.84);
    cairo_line_to(cr, 0.05, 0.38);
    cairo_line_to(cr, 0.95, 0.38);
    cairo_line_to(cr, 0.95, 0.84);
  }
  cairo_close_path(cr);
  fill_v(cr, rgb(0xfbe49a), rgb(0xf0b73c), 0.38, 0.84, true);
  stroke(cr, rgb(0xa8741c), 0.018);
  // gloss line
  cairo_move_to(cr, open ? 0.2 : 0.09, 0.42);
  cairo_line_to(cr, open ? 0.93 : 0.91, 0.42);
  stroke(cr, {1, 1, 1}, 0.018, 0.7);
}

// ---- pages ----
void page(cairo_t* cr, Rgb accent, bool lines) {
  shadow(cr, 0.52, 0.93, 0.32, 0.03);
  const double x = 0.20, y = 0.06, w = 0.60, h = 0.86, fold = 0.2;
  cairo_new_path(cr);
  cairo_move_to(cr, x, y);
  cairo_line_to(cr, x + w - fold, y);
  cairo_line_to(cr, x + w, y + fold);
  cairo_line_to(cr, x + w, y + h);
  cairo_line_to(cr, x, y + h);
  cairo_close_path(cr);
  fill_v(cr, rgb(0xffffff), rgb(0xe3e8f0), y, y + h, true);
  stroke(cr, rgb(0x7f8ba0), 0.018);
  cairo_move_to(cr, x + w - fold, y);
  cairo_line_to(cr, x + w - fold, y + fold);
  cairo_line_to(cr, x + w, y + fold);
  cairo_close_path(cr);
  set(cr, rgb(0xcfd6e2));
  cairo_fill_preserve(cr);
  stroke(cr, rgb(0x7f8ba0), 0.014);
  if (lines) {
    for (int i = 0; i < 4; ++i) {
      cairo_move_to(cr, x + 0.08, 0.42 + i * 0.1);
      cairo_line_to(cr, x + w - 0.08 - (i == 3 ? 0.18 : 0), 0.42 + i * 0.1);
      stroke(cr, accent, 0.022, 0.85);
    }
  }
}

void band(cairo_t* cr, Rgb c) {  // coloured header stripe on a page
  round_rect(cr, 0.20, 0.20, 0.60, 0.13, 0.01);
  set(cr, c);
  cairo_fill(cr);
}

void image_icon(cairo_t* cr) {
  page(cr, rgb(0x5b86c8), false);
  cairo_rectangle(cr, 0.27, 0.30, 0.46, 0.46);
  fill_v(cr, rgb(0x9fd2f5), rgb(0xd9eefc), 0.30, 0.76);
  cairo_arc(cr, 0.62, 0.42, 0.055, 0, 2 * M_PI);
  set(cr, rgb(0xffd54a));
  cairo_fill(cr);
  cairo_move_to(cr, 0.27, 0.76);
  cairo_line_to(cr, 0.43, 0.52);
  cairo_line_to(cr, 0.55, 0.66);
  cairo_line_to(cr, 0.63, 0.58);
  cairo_line_to(cr, 0.73, 0.76);
  cairo_close_path(cr);
  set(cr, rgb(0x4f9a4a));
  cairo_fill(cr);
}

void audio_icon(cairo_t* cr) {
  page(cr, rgb(0x7b5bb8), false);
  cairo_arc(cr, 0.43, 0.68, 0.075, 0, 2 * M_PI);
  set(cr, rgb(0x5b3f9a));
  cairo_fill(cr);
  cairo_arc(cr, 0.62, 0.62, 0.075, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_move_to(cr, 0.50, 0.68);
  cairo_line_to(cr, 0.50, 0.36);
  cairo_line_to(cr, 0.69, 0.30);
  cairo_line_to(cr, 0.69, 0.62);
  stroke(cr, rgb(0x5b3f9a), 0.03);
}

void video_icon(cairo_t* cr) {
  page(cr, rgb(0xb85b5b), false);
  round_rect(cr, 0.27, 0.32, 0.46, 0.38, 0.03);
  fill_v(cr, rgb(0x4a4f5c), rgb(0x22252d), 0.32, 0.70);
  for (int i = 0; i < 4; ++i) {
    cairo_rectangle(cr, 0.285, 0.35 + i * 0.09, 0.03, 0.05);
    cairo_rectangle(cr, 0.685, 0.35 + i * 0.09, 0.03, 0.05);
  }
  set(cr, rgb(0xdcdfe6));
  cairo_fill(cr);
  cairo_move_to(cr, 0.44, 0.40);
  cairo_line_to(cr, 0.44, 0.62);
  cairo_line_to(cr, 0.60, 0.51);
  cairo_close_path(cr);
  set(cr, {1, 1, 1});
  cairo_fill(cr);
}

void archive_icon(cairo_t* cr) {
  page(cr, rgb(0x8a6a3a), false);
  band(cr, rgb(0xc99a4b));
  for (int i = 0; i < 6; ++i) {
    cairo_rectangle(cr, i % 2 ? 0.485 : 0.455, 0.36 + i * 0.08, 0.04, 0.04);
    set(cr, i % 2 ? rgb(0x5a4524) : rgb(0xf2e2c0));
    cairo_fill(cr);
  }
  round_rect(cr, 0.43, 0.72, 0.14, 0.12, 0.02);
  set(cr, rgb(0xc99a4b));
  cairo_fill_preserve(cr);
  stroke(cr, rgb(0x5a4524), 0.012);
}

void code_icon(cairo_t* cr) {
  page(cr, rgb(0x2f6f9f), false);
  band(cr, rgb(0x2f6f9f));
  set(cr, rgb(0x1f4f78));
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, 0.43, 0.46);
  cairo_line_to(cr, 0.32, 0.58);
  cairo_line_to(cr, 0.43, 0.70);
  stroke(cr, rgb(0x1f4f78), 0.04);
  cairo_move_to(cr, 0.57, 0.46);
  cairo_line_to(cr, 0.68, 0.58);
  cairo_line_to(cr, 0.57, 0.70);
  stroke(cr, rgb(0x1f4f78), 0.04);
  cairo_move_to(cr, 0.53, 0.43);
  cairo_line_to(cr, 0.47, 0.73);
  stroke(cr, rgb(0xc0392b), 0.03);
}

void exe_icon(cairo_t* cr) {
  page(cr, rgb(0x4a5568), false);
  round_rect(cr, 0.27, 0.30, 0.46, 0.40, 0.03);
  fill_v(cr, rgb(0xf4f6fa), rgb(0xcfd6e2), 0.30, 0.70, true);
  stroke(cr, rgb(0x4a5568), 0.016);
  cairo_rectangle(cr, 0.27, 0.30, 0.46, 0.07);
  set(cr, rgb(0x3b6fb0));
  cairo_fill(cr);
  cairo_move_to(cr, 0.44, 0.45);
  cairo_line_to(cr, 0.44, 0.62);
  cairo_line_to(cr, 0.60, 0.535);
  cairo_close_path(cr);
  set(cr, rgb(0x2e8b57));
  cairo_fill(cr);
}

void sheet(cairo_t* cr, Rgb accent, int kind) {  // 0 document, 1 spreadsheet, 2 presentation, 3 pdf
  page(cr, accent, kind == 0);
  if (kind == 0) {
    band(cr, accent);
  } else if (kind == 1) {
    band(cr, accent);
    for (int i = 0; i < 4; ++i) {
      cairo_move_to(cr, 0.27, 0.42 + i * 0.1);
      cairo_line_to(cr, 0.73, 0.42 + i * 0.1);
    }
    for (int j = 0; j < 3; ++j) {
      cairo_move_to(cr, 0.27 + j * 0.15, 0.33);
      cairo_line_to(cr, 0.27 + j * 0.15, 0.82);
    }
    stroke(cr, accent, 0.014, 0.8);
  } else if (kind == 2) {
    band(cr, accent);
    cairo_arc(cr, 0.5, 0.58, 0.14, 0, 2 * M_PI);
    set(cr, accent, 0.25);
    cairo_fill(cr);
    cairo_move_to(cr, 0.5, 0.58);
    cairo_arc(cr, 0.5, 0.58, 0.14, -M_PI / 2, 0.3);
    set(cr, accent);
    cairo_fill(cr);
  } else {
    band(cr, accent);
    cairo_move_to(cr, 0.34, 0.74);
    cairo_curve_to(cr, 0.40, 0.50, 0.50, 0.50, 0.52, 0.64);
    cairo_curve_to(cr, 0.54, 0.74, 0.66, 0.70, 0.68, 0.62);
    stroke(cr, accent, 0.03);
  }
}

void text_icon(cairo_t* cr) { page(cr, rgb(0x6b7a90), true); }

void font_icon(cairo_t* cr) {
  page(cr, rgb(0x6b7a90), false);
  cairo_move_to(cr, 0.34, 0.74);
  cairo_line_to(cr, 0.50, 0.30);
  cairo_line_to(cr, 0.66, 0.74);
  stroke(cr, rgb(0x2b3a52), 0.045);
  cairo_move_to(cr, 0.39, 0.62);
  cairo_line_to(cr, 0.61, 0.62);
  stroke(cr, rgb(0x2b3a52), 0.035);
}

// ---- drives ----
void disc(cairo_t* cr, double cx, double cy, double r) {
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  fill_v(cr, rgb(0xf5f7fa), rgb(0xb3bccb), cy - r, cy + r, true);
  stroke(cr, rgb(0x6b7a90), 0.016);
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r * 0.62, -0.6, 1.0);
  stroke(cr, rgb(0x7cc4f0), 0.02, 0.8);
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r * 0.62, 2.5, 4.1);
  stroke(cr, rgb(0xf0b0d0), 0.02, 0.8);
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r * 0.18, 0, 2 * M_PI);
  set(cr, rgb(0xffffff));
  cairo_fill_preserve(cr);
  stroke(cr, rgb(0x6b7a90), 0.012);
}

void hdd(cairo_t* cr, bool network, bool card) {
  shadow(cr, 0.5, 0.82, 0.40, 0.04);
  if (card) {  // SD card
    cairo_new_path(cr);
    cairo_move_to(cr, 0.26, 0.10);
    cairo_line_to(cr, 0.66, 0.10);
    cairo_line_to(cr, 0.78, 0.22);
    cairo_line_to(cr, 0.78, 0.88);
    cairo_line_to(cr, 0.26, 0.88);
    cairo_close_path(cr);
    fill_v(cr, rgb(0x3a4f78), rgb(0x1f2f4f), 0.1, 0.88, true);
    stroke(cr, rgb(0x141e33), 0.018);
    for (int i = 0; i < 5; ++i) {
      cairo_rectangle(cr, 0.31 + i * 0.075, 0.13, 0.045, 0.14);
    }
    set(cr, rgb(0xd4af37));
    cairo_fill(cr);
    return;
  }
  round_rect(cr, 0.08, 0.30, 0.84, 0.46, 0.06);
  fill_v(cr, rgb(0xe6eaf0), rgb(0x8f9aae), 0.30, 0.76, true);
  stroke(cr, rgb(0x4f5a70), 0.02);
  round_rect(cr, 0.12, 0.34, 0.76, 0.12, 0.04);
  set(cr, {1, 1, 1}, 0.55);
  cairo_fill(cr);
  cairo_rectangle(cr, 0.16, 0.60, 0.40, 0.04);
  set(cr, rgb(0x6b7a90));
  cairo_fill(cr);
  cairo_arc(cr, 0.80, 0.62, 0.035, 0, 2 * M_PI);
  set(cr, network ? rgb(0x2f8fe0) : rgb(0x3ad06a));
  cairo_fill(cr);
  if (network) {  // cable and a globe-ish badge
    cairo_move_to(cr, 0.5, 0.76);
    cairo_line_to(cr, 0.5, 0.86);
    cairo_line_to(cr, 0.28, 0.86);
    stroke(cr, rgb(0x2f8fe0), 0.03);
    cairo_arc(cr, 0.76, 0.84, 0.10, 0, 2 * M_PI);
    fill_v(cr, rgb(0x6fc0f5), rgb(0x2b78c8), 0.74, 0.94, true);
    stroke(cr, rgb(0x1c5a9a), 0.014);
  }
}

void usb(cairo_t* cr) {
  shadow(cr, 0.5, 0.92, 0.22, 0.03);
  round_rect(cr, 0.34, 0.34, 0.32, 0.56, 0.05);
  fill_v(cr, rgb(0x5a6ea8), rgb(0x2e3d6d), 0.34, 0.90, true);
  stroke(cr, rgb(0x1d2748), 0.018);
  cairo_rectangle(cr, 0.40, 0.10, 0.20, 0.26);
  fill_v(cr, rgb(0xeef1f6), rgb(0xa5b0c2), 0.10, 0.36, true);
  stroke(cr, rgb(0x5a6678), 0.016);
  cairo_rectangle(cr, 0.45, 0.16, 0.035, 0.07);
  cairo_rectangle(cr, 0.52, 0.16, 0.035, 0.07);
  set(cr, rgb(0x3d4658));
  cairo_fill(cr);
  round_rect(cr, 0.40, 0.46, 0.20, 0.10, 0.02);
  set(cr, {1, 1, 1}, 0.35);
  cairo_fill(cr);
}

void computer(cairo_t* cr) {
  shadow(cr, 0.5, 0.90, 0.36, 0.035);
  round_rect(cr, 0.10, 0.12, 0.80, 0.58, 0.05);
  fill_v(cr, rgb(0xdfe4ec), rgb(0x8d98ac), 0.12, 0.70, true);
  stroke(cr, rgb(0x4f5a70), 0.02);
  cairo_rectangle(cr, 0.16, 0.18, 0.68, 0.46);
  fill_v(cr, rgb(0x5aa9ec), rgb(0x1f5fa8), 0.18, 0.64);
  cairo_move_to(cr, 0.16, 0.18);
  cairo_line_to(cr, 0.56, 0.18);
  cairo_line_to(cr, 0.16, 0.48);
  cairo_close_path(cr);
  set(cr, {1, 1, 1}, 0.18);
  cairo_fill(cr);
  cairo_rectangle(cr, 0.42, 0.70, 0.16, 0.08);
  set(cr, rgb(0x8d98ac));
  cairo_fill(cr);
  round_rect(cr, 0.28, 0.78, 0.44, 0.06, 0.02);
  fill_v(cr, rgb(0xdfe4ec), rgb(0x8d98ac), 0.78, 0.84, true);
  stroke(cr, rgb(0x4f5a70), 0.014);
}

void globe(cairo_t* cr, bool server) {
  if (server) {
    for (int i = 0; i < 3; ++i) {
      round_rect(cr, 0.20, 0.12 + i * 0.26, 0.60, 0.22, 0.03);
      fill_v(cr, rgb(0xe6eaf0), rgb(0x8f9aae), 0.12 + i * 0.26, 0.34 + i * 0.26, true);
      stroke(cr, rgb(0x4f5a70), 0.016);
      cairo_arc(cr, 0.70, 0.23 + i * 0.26, 0.025, 0, 2 * M_PI);
      set(cr, rgb(0x3ad06a));
      cairo_fill(cr);
    }
    return;
  }
  cairo_arc(cr, 0.5, 0.5, 0.40, 0, 2 * M_PI);
  fill_v(cr, rgb(0x8fd0f8), rgb(0x2b78c8), 0.1, 0.9, true);
  stroke(cr, rgb(0x1c5a9a), 0.02);
  cairo_save(cr);
  cairo_arc(cr, 0.5, 0.5, 0.40, 0, 2 * M_PI);
  cairo_clip(cr);
  cairo_new_path(cr);
  cairo_move_to(cr, 0.28, 0.30);
  cairo_curve_to(cr, 0.45, 0.22, 0.52, 0.40, 0.42, 0.52);
  cairo_curve_to(cr, 0.34, 0.62, 0.20, 0.54, 0.22, 0.42);
  cairo_close_path(cr);
  set(cr, rgb(0x4f9a4a));
  cairo_fill(cr);
  cairo_move_to(cr, 0.62, 0.50);
  cairo_curve_to(cr, 0.80, 0.46, 0.86, 0.64, 0.70, 0.76);
  cairo_curve_to(cr, 0.58, 0.72, 0.54, 0.58, 0.62, 0.50);
  set(cr, rgb(0x4f9a4a));
  cairo_fill(cr);
  cairo_restore(cr);
  cairo_arc(cr, 0.5, 0.5, 0.40, 0, 2 * M_PI);
  stroke(cr, rgb(0x1c5a9a), 0.02);
}

void cloud(cairo_t* cr) {
  cairo_new_path(cr);
  cairo_arc(cr, 0.32, 0.60, 0.16, M_PI / 2, 3 * M_PI / 2);
  cairo_arc(cr, 0.46, 0.40, 0.18, M_PI, 2 * M_PI - 0.4);
  cairo_arc(cr, 0.68, 0.50, 0.14, -M_PI / 2 - 0.3, M_PI / 2);
  cairo_close_path(cr);
  fill_v(cr, rgb(0xf4f9ff), rgb(0x9cc6ee), 0.25, 0.78, true);
  stroke(cr, rgb(0x3b78b8), 0.022);
}

void house(cairo_t* cr) {
  shadow(cr, 0.5, 0.9, 0.38, 0.035);
  cairo_rectangle(cr, 0.22, 0.44, 0.56, 0.42);
  fill_v(cr, rgb(0xf7ecd2), rgb(0xd9c28e), 0.44, 0.86, true);
  stroke(cr, rgb(0x8a6d3a), 0.018);
  cairo_move_to(cr, 0.10, 0.48);
  cairo_line_to(cr, 0.50, 0.12);
  cairo_line_to(cr, 0.90, 0.48);
  cairo_close_path(cr);
  fill_v(cr, rgb(0xd24a3a), rgb(0x9c2b20), 0.12, 0.48, true);
  stroke(cr, rgb(0x6e1d15), 0.018);
  cairo_rectangle(cr, 0.43, 0.60, 0.14, 0.26);
  set(cr, rgb(0x7a5230));
  cairo_fill(cr);
}

void trash(cairo_t* cr) {
  shadow(cr, 0.5, 0.92, 0.28, 0.03);
  cairo_move_to(cr, 0.24, 0.30);
  cairo_line_to(cr, 0.30, 0.90);
  cairo_line_to(cr, 0.70, 0.90);
  cairo_line_to(cr, 0.76, 0.30);
  cairo_close_path(cr);
  fill_v(cr, rgb(0xeef2f8), rgb(0x9aa6ba), 0.30, 0.90, true);
  stroke(cr, rgb(0x5a6678), 0.02);
  for (int i = 0; i < 3; ++i) {
    cairo_move_to(cr, 0.38 + i * 0.12, 0.40);
    cairo_line_to(cr, 0.39 + i * 0.11, 0.82);
  }
  stroke(cr, rgb(0x6b7a90), 0.018, 0.8);
  round_rect(cr, 0.18, 0.20, 0.64, 0.10, 0.03);
  fill_v(cr, rgb(0xdfe4ec), rgb(0x8d98ac), 0.20, 0.30, true);
  stroke(cr, rgb(0x5a6678), 0.018);
  round_rect(cr, 0.40, 0.12, 0.20, 0.08, 0.02);
  set(cr, rgb(0x8d98ac));
  cairo_fill(cr);
}

void star(cairo_t* cr) {
  for (int i = 0; i < 10; ++i) {
    const double r = i % 2 ? 0.20 : 0.42, a = -M_PI / 2 + i * M_PI / 5;
    if (i == 0) cairo_move_to(cr, 0.5 + r * std::cos(a), 0.54 + r * std::sin(a));
    else cairo_line_to(cr, 0.5 + r * std::cos(a), 0.54 + r * std::sin(a));
  }
  cairo_close_path(cr);
  fill_v(cr, rgb(0xffe27a), rgb(0xf0a91e), 0.12, 0.96, true);
  stroke(cr, rgb(0xa8741c), 0.02);
}

void clock_icon(cairo_t* cr) {
  cairo_arc(cr, 0.5, 0.5, 0.40, 0, 2 * M_PI);
  fill_v(cr, rgb(0xffffff), rgb(0xd6dde8), 0.1, 0.9, true);
  stroke(cr, rgb(0x4f5a70), 0.025);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.5, 0.5);
  cairo_line_to(cr, 0.5, 0.24);
  stroke(cr, rgb(0x2b3a52), 0.035);
  cairo_move_to(cr, 0.5, 0.5);
  cairo_line_to(cr, 0.68, 0.58);
  stroke(cr, rgb(0x2b3a52), 0.035);
}

void library(cairo_t* cr) {
  const Rgb cols[] = {rgb(0x3b78b8), rgb(0xc0562b), rgb(0x4f9a4a), rgb(0xd0a02b)};
  for (int i = 0; i < 4; ++i) {
    round_rect(cr, 0.14 + i * 0.19, 0.18 + (i % 2) * 0.04, 0.16, 0.66 - (i % 2) * 0.04, 0.02);
    set(cr, cols[i]);
    cairo_fill_preserve(cr);
    stroke(cr, {0, 0, 0}, 0.012, 0.35);
  }
  cairo_rectangle(cr, 0.08, 0.84, 0.84, 0.05);
  set(cr, rgb(0x7a5230));
  cairo_fill(cr);
}

// A folder with a small emblem for the well-known folders.
void emblem_folder(cairo_t* cr, IconKind k) {
  folder(cr, false);
  cairo_save(cr);
  cairo_translate(cr, 0.5, 0.62);
  switch (k) {
    case IconKind::Desktop:
      round_rect(cr, -0.17, -0.11, 0.34, 0.22, 0.02);
      set(cr, rgb(0x3b78b8));
      cairo_fill(cr);
      break;
    case IconKind::Documents:
      cairo_rectangle(cr, -0.10, -0.14, 0.20, 0.28);
      set(cr, {1, 1, 1});
      cairo_fill_preserve(cr);
      stroke(cr, rgb(0x6b7a90), 0.014);
      break;
    case IconKind::Downloads:
      cairo_move_to(cr, 0, 0.14);
      cairo_line_to(cr, -0.13, -0.02);
      cairo_line_to(cr, -0.05, -0.02);
      cairo_line_to(cr, -0.05, -0.14);
      cairo_line_to(cr, 0.05, -0.14);
      cairo_line_to(cr, 0.05, -0.02);
      cairo_line_to(cr, 0.13, -0.02);
      cairo_close_path(cr);
      set(cr, rgb(0x2e8b57));
      cairo_fill(cr);
      break;
    case IconKind::Music:
      cairo_arc(cr, -0.04, 0.09, 0.05, 0, 2 * M_PI);
      set(cr, rgb(0x5b3f9a));
      cairo_fill(cr);
      cairo_move_to(cr, 0.01, 0.09);
      cairo_line_to(cr, 0.01, -0.12);
      stroke(cr, rgb(0x5b3f9a), 0.03);
      break;
    case IconKind::Pictures:
      cairo_arc(cr, 0, 0, 0.12, 0, 2 * M_PI);
      fill_v(cr, rgb(0x9fd2f5), rgb(0x4f9a4a), -0.12, 0.12);
      break;
    case IconKind::Videos:
      cairo_move_to(cr, -0.07, -0.11);
      cairo_line_to(cr, -0.07, 0.11);
      cairo_line_to(cr, 0.11, 0);
      cairo_close_path(cr);
      set(cr, rgb(0xb85b5b));
      cairo_fill(cr);
      break;
    default: break;
  }
  cairo_restore(cr);
}

void app(cairo_t* cr) {
  // Keep the application mark quiet and legible at small taskbar sizes: one folder,
  // one blue face panel, and four simple panes. The runtime icon is always drawn here.
  shadow(cr, 0.5, 0.92, 0.42, 0.045);
  cairo_new_path(cr);
  cairo_move_to(cr, 0.06, 0.82);
  cairo_line_to(cr, 0.06, 0.18);
  cairo_curve_to(cr, 0.06, 0.145, 0.085, 0.12, 0.12, 0.12);
  cairo_line_to(cr, 0.37, 0.12);
  cairo_curve_to(cr, 0.41, 0.12, 0.43, 0.15, 0.45, 0.19);
  cairo_line_to(cr, 0.89, 0.19);
  cairo_curve_to(cr, 0.92, 0.19, 0.94, 0.215, 0.94, 0.25);
  cairo_line_to(cr, 0.94, 0.82);
  cairo_close_path(cr);
  fill_v(cr, rgb(0x4f9fe6), rgb(0x173a6a), 0.12, 0.82, true);
  stroke(cr, rgb(0x0e2849), 0.02);
  cairo_new_path(cr);
  cairo_move_to(cr, 0.07, 0.83);
  cairo_line_to(cr, 0.07, 0.36);
  cairo_curve_to(cr, 0.07, 0.33, 0.09, 0.31, 0.12, 0.31);
  cairo_line_to(cr, 0.88, 0.31);
  cairo_curve_to(cr, 0.91, 0.31, 0.93, 0.33, 0.93, 0.36);
  cairo_line_to(cr, 0.93, 0.83);
  cairo_close_path(cr);
  fill_v(cr, rgb(0x83d1ff), rgb(0x277ac7), 0.31, 0.83, true);
  stroke(cr, rgb(0x14518f), 0.02);
  // Four light panes provide the Windows cue without a separate badge.
  for (int row = 0; row < 2; ++row)
    for (int col = 0; col < 2; ++col) {
      round_rect(cr, 0.20 + col * 0.16, 0.43 + row * 0.12, 0.13, 0.09, 0.012);
      set(cr, {1, 1, 1}, 0.86);
      cairo_fill(cr);
    }
  // A single highlight keeps the shape readable without adding a face or tick.
  cairo_move_to(cr, 0.10, 0.37);
  cairo_line_to(cr, 0.90, 0.37);
  stroke(cr, {1, 1, 1}, 0.018, 0.42);
}

}  // namespace

void draw_link_badge(cairo_t* cr, double size) {
  cairo_save(cr);
  cairo_scale(cr, size, size);
  round_rect(cr, 0.04, 0.60, 0.34, 0.34, 0.05);
  set(cr, {1, 1, 1});
  cairo_fill_preserve(cr);
  stroke(cr, rgb(0x4f5a70), 0.02);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.12, 0.86);
  cairo_line_to(cr, 0.30, 0.68);
  stroke(cr, rgb(0x2f6fb8), 0.045);
  cairo_move_to(cr, 0.18, 0.68);
  cairo_line_to(cr, 0.30, 0.68);
  cairo_line_to(cr, 0.30, 0.80);
  stroke(cr, rgb(0x2f6fb8), 0.045);
  cairo_restore(cr);
}

void draw_lock_badge(cairo_t* cr, double size) {
  cairo_save(cr);
  cairo_scale(cr, size, size);
  cairo_arc(cr, 0.80, 0.70, 0.07, M_PI, 2 * M_PI);
  stroke(cr, rgb(0x4f5a70), 0.035);
  round_rect(cr, 0.68, 0.70, 0.24, 0.22, 0.03);
  fill_v(cr, rgb(0xffe27a), rgb(0xe0a21e), 0.70, 0.92, true);
  stroke(cr, rgb(0x8a5d16), 0.018);
  cairo_restore(cr);
}

void draw_icon(cairo_t* cr, IconKind kind, double size) {
  cairo_save(cr);
  cairo_scale(cr, size, size);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  switch (kind) {
    case IconKind::App: app(cr); break;
    case IconKind::Folder: folder(cr, false); break;
    case IconKind::FolderOpen: folder(cr, true); break;
    case IconKind::File: page(cr, rgb(0x9aa4b4), false); break;
    case IconKind::Text: text_icon(cr); break;
    case IconKind::Document: sheet(cr, rgb(0x2f6fb8), 0); break;
    case IconKind::Spreadsheet: sheet(cr, rgb(0x2e8b57), 1); break;
    case IconKind::Presentation: sheet(cr, rgb(0xd0702b), 2); break;
    case IconKind::Pdf: sheet(cr, rgb(0xc0392b), 3); break;
    case IconKind::Image: image_icon(cr); break;
    case IconKind::Audio: audio_icon(cr); break;
    case IconKind::Video: video_icon(cr); break;
    case IconKind::Archive: archive_icon(cr); break;
    case IconKind::Code: code_icon(cr); break;
    case IconKind::Executable: exe_icon(cr); break;
    case IconKind::Font: font_icon(cr); break;
    case IconKind::Disc: disc(cr, 0.5, 0.5, 0.40); break;
    case IconKind::DriveInternal: hdd(cr, false, false); break;
    case IconKind::DriveUsb: usb(cr); break;
    case IconKind::DriveOptical: disc(cr, 0.5, 0.5, 0.42); break;
    case IconKind::DriveNetwork: hdd(cr, true, false); break;
    case IconKind::DriveCard: hdd(cr, false, true); break;
    case IconKind::Computer: computer(cr); break;
    case IconKind::Network: globe(cr, false); break;
    case IconKind::NetworkServer: globe(cr, true); break;
    case IconKind::Cloud: cloud(cr); break;
    case IconKind::Home: house(cr); break;
    case IconKind::Desktop:
    case IconKind::Documents:
    case IconKind::Downloads:
    case IconKind::Music:
    case IconKind::Pictures:
    case IconKind::Videos: emblem_folder(cr, kind); break;
    case IconKind::Trash: trash(cr); break;
    case IconKind::Recent: clock_icon(cr); break;
    case IconKind::Star: star(cr); break;
    case IconKind::Library: library(cr); break;
    case IconKind::Count: break;
  }
  cairo_restore(cr);
}

const char* icon_kind_name(IconKind k) {
  static const char* names[] = {"app", "folder", "folder-open", "file", "text", "document", "spreadsheet", "presentation", "pdf", "image", "audio", "video", "archive", "code",
                                "executable", "font", "disc", "drive-internal", "drive-usb", "drive-optical", "drive-network", "drive-card", "computer", "network",
                                "network-server", "cloud", "home", "desktop", "documents", "downloads", "music", "pictures", "videos", "trash", "recent", "star", "library"};
  const size_t i = static_cast<size_t>(k);
  return i < sizeof names / sizeof *names ? names[i] : "file";
}

namespace {
struct ExtMap {
  const char* ext;
  IconKind kind;
  const char* description;
};
const ExtMap kExts[] = {
    {"txt", IconKind::Text, "Text Document"}, {"log", IconKind::Text, "Text Document"}, {"md", IconKind::Text, "Markdown Document"}, {"rst", IconKind::Text, "Text Document"},
    {"ini", IconKind::Text, "Configuration settings"}, {"conf", IconKind::Text, "Configuration file"}, {"cfg", IconKind::Text, "Configuration file"},
    {"toml", IconKind::Text, "TOML file"}, {"yaml", IconKind::Text, "YAML file"}, {"yml", IconKind::Text, "YAML file"}, {"json", IconKind::Code, "JSON file"},
    {"xml", IconKind::Code, "XML Document"}, {"csv", IconKind::Spreadsheet, "CSV file"}, {"nfo", IconKind::Text, "Info file"},
    {"doc", IconKind::Document, "Word Document"}, {"docx", IconKind::Document, "Word Document"}, {"odt", IconKind::Document, "OpenDocument Text"},
    {"rtf", IconKind::Document, "Rich Text Document"}, {"xls", IconKind::Spreadsheet, "Excel Worksheet"}, {"xlsx", IconKind::Spreadsheet, "Excel Worksheet"},
    {"ods", IconKind::Spreadsheet, "OpenDocument Spreadsheet"}, {"ppt", IconKind::Presentation, "PowerPoint Presentation"},
    {"pptx", IconKind::Presentation, "PowerPoint Presentation"}, {"odp", IconKind::Presentation, "OpenDocument Presentation"}, {"pdf", IconKind::Pdf, "PDF Document"},
    {"epub", IconKind::Document, "E-book"},
    {"jpg", IconKind::Image, "JPEG image"}, {"jpeg", IconKind::Image, "JPEG image"}, {"png", IconKind::Image, "PNG image"}, {"gif", IconKind::Image, "GIF image"},
    {"bmp", IconKind::Image, "Bitmap image"}, {"webp", IconKind::Image, "WebP image"}, {"svg", IconKind::Image, "SVG image"}, {"tif", IconKind::Image, "TIFF image"},
    {"tiff", IconKind::Image, "TIFF image"}, {"ico", IconKind::Image, "Icon"}, {"heic", IconKind::Image, "HEIC image"}, {"raw", IconKind::Image, "RAW image"},
    {"mp3", IconKind::Audio, "MP3 Audio"}, {"flac", IconKind::Audio, "FLAC Audio"}, {"wav", IconKind::Audio, "WAV Audio"}, {"ogg", IconKind::Audio, "Ogg Audio"},
    {"opus", IconKind::Audio, "Opus Audio"}, {"m4a", IconKind::Audio, "M4A Audio"}, {"aac", IconKind::Audio, "AAC Audio"}, {"wma", IconKind::Audio, "WMA Audio"},
    {"mid", IconKind::Audio, "MIDI Sequence"}, {"mp4", IconKind::Video, "MP4 Video"}, {"mkv", IconKind::Video, "Matroska Video"}, {"avi", IconKind::Video, "AVI Video"},
    {"mov", IconKind::Video, "QuickTime Movie"}, {"webm", IconKind::Video, "WebM Video"}, {"wmv", IconKind::Video, "WMV Video"}, {"flv", IconKind::Video, "Flash Video"},
    {"m4v", IconKind::Video, "M4V Video"}, {"mpg", IconKind::Video, "MPEG Video"}, {"mpeg", IconKind::Video, "MPEG Video"},
    {"zip", IconKind::Archive, "Compressed (zipped) Folder"}, {"tar", IconKind::Archive, "TAR Archive"}, {"gz", IconKind::Archive, "GZ Archive"},
    {"xz", IconKind::Archive, "XZ Archive"}, {"bz2", IconKind::Archive, "BZ2 Archive"}, {"zst", IconKind::Archive, "Zstandard Archive"},
    {"7z", IconKind::Archive, "7-Zip Archive"}, {"rar", IconKind::Archive, "RAR Archive"}, {"tgz", IconKind::Archive, "TAR.GZ Archive"},
    {"deb", IconKind::Archive, "Debian Package"}, {"rpm", IconKind::Archive, "RPM Package"}, {"iso", IconKind::Disc, "Disc Image File"}, {"img", IconKind::Disc, "Disk Image File"},
    {"c", IconKind::Code, "C Source"}, {"h", IconKind::Code, "C Header"}, {"cpp", IconKind::Code, "C++ Source"}, {"cc", IconKind::Code, "C++ Source"},
    {"hpp", IconKind::Code, "C++ Header"}, {"rs", IconKind::Code, "Rust Source"}, {"go", IconKind::Code, "Go Source"}, {"py", IconKind::Code, "Python Script"},
    {"js", IconKind::Code, "JavaScript File"}, {"ts", IconKind::Code, "TypeScript File"}, {"java", IconKind::Code, "Java Source"}, {"sh", IconKind::Code, "Shell Script"},
    {"html", IconKind::Code, "HTML Document"}, {"htm", IconKind::Code, "HTML Document"}, {"css", IconKind::Code, "CSS File"}, {"php", IconKind::Code, "PHP File"},
    {"rb", IconKind::Code, "Ruby Script"}, {"lua", IconKind::Code, "Lua Script"}, {"pl", IconKind::Code, "Perl Script"}, {"sql", IconKind::Code, "SQL File"},
    {"meson", IconKind::Code, "Meson build file"}, {"cmake", IconKind::Code, "CMake file"}, {"mk", IconKind::Code, "Makefile"},
    {"exe", IconKind::Executable, "Application"}, {"appimage", IconKind::Executable, "AppImage Application"}, {"run", IconKind::Executable, "Installer"},
    {"bin", IconKind::Executable, "Binary file"}, {"so", IconKind::Executable, "Shared library"}, {"desktop", IconKind::Executable, "Desktop Entry"},
    {"ttf", IconKind::Font, "TrueType Font"}, {"otf", IconKind::Font, "OpenType Font"}, {"woff", IconKind::Font, "Web Font"}, {"woff2", IconKind::Font, "Web Font"},
};
const ExtMap* find_ext(const std::string& ext) {
  for (const ExtMap& e : kExts)
    if (ext == e.ext) return &e;
  return nullptr;
}
}  // namespace

IconKind icon_for_folder_name(std::string_view n) {
  if (n == "Desktop") return IconKind::Desktop;
  if (n == "Documents") return IconKind::Documents;
  if (n == "Downloads") return IconKind::Downloads;
  if (n == "Music") return IconKind::Music;
  if (n == "Pictures") return IconKind::Pictures;
  if (n == "Videos") return IconKind::Videos;
  return IconKind::Folder;
}

IconKind icon_for_file(std::string_view name, bool is_dir) {
  if (is_dir) return IconKind::Folder;
  if (name == "Makefile" || name == "CMakeLists.txt" || name == "meson.build" || name == "Dockerfile" || name == "PKGBUILD") return IconKind::Code;
  if (const ExtMap* e = find_ext(extension_of(name))) return e->kind;
  return IconKind::File;
}

std::string type_description(std::string_view name, bool is_dir) {
  if (is_dir) return "File folder";
  const std::string ext = extension_of(name);
  if (const ExtMap* e = find_ext(ext)) return e->description;
  if (ext.empty()) return "File";
  std::string up = ext;
  for (char& c : up)
    if (c >= 'a' && c <= 'z') c -= 32;
  return up + " File";
}

bool write_icon_png(IconKind kind, int size, const std::string& path) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(s);
  draw_icon(cr, kind, size);
  cairo_destroy(cr);
  const bool ok = cairo_surface_write_to_png(s, path.c_str()) == CAIRO_STATUS_SUCCESS;
  cairo_surface_destroy(s);
  return ok;
}

IconCache::~IconCache() { clear(); }

void IconCache::clear() {
  for (auto& [k, slot] : map_) cairo_surface_destroy(slot.surface);
  map_.clear();
}

cairo_surface_t* IconCache::get(IconKind kind, int size) {
  const uint32_t key = (static_cast<uint32_t>(kind) << 16) | static_cast<uint32_t>(size & 0xffff);
  auto it = map_.find(key);
  if (it != map_.end()) {
    ++hits_;
    it->second.used = ++tick_;
    return it->second.surface;
  }
  ++misses_;
  if (map_.size() >= max_) {
    auto victim = map_.begin();
    for (auto i = map_.begin(); i != map_.end(); ++i)
      if (i->second.used < victim->second.used) victim = i;
    cairo_surface_destroy(victim->second.surface);
    map_.erase(victim);
  }
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(s);
  draw_icon(cr, kind, size);
  cairo_destroy(cr);
  map_[key] = {s, ++tick_};
  return s;
}

}  // namespace fleetwm::fm
