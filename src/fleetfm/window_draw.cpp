#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "file_ops.hpp"
#include "window.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
enum Glyph {
  G_NONE, G_BACK, G_FORWARD, G_UP, G_RELOAD, G_NEWFOLDER, G_COPY, G_PASTE, G_CUT, G_DELETE, G_RENAME, G_PROPS, G_GRID, G_LIST, G_SEARCH, G_MENU, G_EJECT,
  G_PLUG, G_HOME, G_CHEV_R, G_CHEV_D, G_CLOSE, G_PLUS, G_SORT_UP, G_SORT_DOWN, G_CHECK, G_PANE, G_OPEN, G_STAR, G_CLOUD, G_DETAILS, G_TILES, G_CHEV_L
};

std::string lower_copy(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c += 32;
  return s;
}
}  // namespace

double FmWindow::now_() const { return host_.now ? host_.now() : 0.0; }

int FmWindow::icon_px() const { return s_.icon_px ? s_.icon_px : view_mode_icon_px(tab().mode); }
int FmWindow::row_h() const { return s_.row_height ? s_.row_height : (s_.compact_rows ? std::max(18, style_->row_h - 4) : style_->row_h); }
int FmWindow::font_px() const { return s_.font_px ? s_.font_px : style_->font_px; }

// ---------------------------------------------------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------------------------------------------------

double FmWindow::text_w(const std::string& s, double px, bool bold) {
  // The same names are measured on every repaint (hover moves, scrolling); remember the widths.
  if (width_cache_.size() > 8192) width_cache_.clear();
  const std::string key = std::to_string(static_cast<int>(px * 4)) + (bold ? "b" : "n") + s;
  auto it = width_cache_.find(key);
  if (it != width_cache_.end()) return it->second;
  const double w = kit::measure_text(cr_, s, px, bold).width;
  width_cache_.emplace(key, w);
  return w;
}

double FmWindow::text(const std::string& s, double x, double y_mid, double px, const Color& c, bool bold) {
  const int key = static_cast<int>(px * 4) * 2 + (bold ? 1 : 0);
  auto it = font_cache_.find(key);
  if (it == font_cache_.end()) {
    const kit::TextExtents te = kit::measure_text(cr_, "Hg", px, bold);
    it = font_cache_.emplace(key, std::make_pair(te.ascent, te.height)).first;
  }
  const double base = y_mid - it->second.second / 2 + it->second.first;
  return kit::draw_text(cr_, s, x, std::round(base), px, c, bold);
}

std::string FmWindow::fit(const std::string& s, double max_w, double px, bool bold) {
  if (max_w <= 0) return {};
  // No glyph here is wider than 1.2 em, so a short enough string needs no measuring at all.
  if (static_cast<double>(s.size()) * px * 0.35 <= max_w && static_cast<double>(s.size()) * px * 1.2 <= max_w) return s;
  if (text_w(s, px, bold) <= max_w) return s;
  const std::string ell = "\xE2\x80\xA6";
  const double ew = text_w(ell, px, bold);
  size_t lo = 0, hi = s.size();
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    while (mid > lo && (static_cast<unsigned char>(s[mid]) & 0xC0) == 0x80) --mid;
    if (mid == lo) break;
    if (text_w(s.substr(0, mid), px, bold) + ew <= max_w) lo = mid;
    else hi = mid - 1;
  }
  return s.substr(0, lo) + ell;
}

void FmWindow::box(double x, double y, double w, double h, double r, const Color& top, const Color& bot, const Color* border, double bw) {
  if (w <= 0 || h <= 0) return;
  cairo_save(cr_);
  if (r > 0) kit::rounded_rect(cr_, x, y, w, h, r);
  else cairo_rectangle(cr_, x, y, w, h);
  if (top.r == bot.r && top.g == bot.g && top.b == bot.b && top.a == bot.a) {
    kit::set_source(cr_, top);
  } else {
    cairo_pattern_t* p = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(p, 0, top.r, top.g, top.b, top.a);
    cairo_pattern_add_color_stop_rgba(p, 1, bot.r, bot.g, bot.b, bot.a);
    cairo_set_source(cr_, p);
    cairo_pattern_destroy(p);
  }
  if (border) {
    cairo_fill_preserve(cr_);
    cairo_new_path(cr_);
    if (r > 0) kit::rounded_rect(cr_, x + bw / 2, y + bw / 2, w - bw, h - bw, std::max(0.0, r - bw / 2));
    else cairo_rectangle(cr_, x + bw / 2, y + bw / 2, w - bw, h - bw);
    kit::set_source(cr_, *border);
    cairo_set_line_width(cr_, bw);
    cairo_stroke(cr_);
  } else {
    cairo_fill(cr_);
  }
  cairo_restore(cr_);
}

void FmWindow::use_clip(const Rect& r) {
  cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
  cairo_clip(cr_);
}

void FmWindow::add_region(const Rect& r, R kind, int arg, int arg2) {
  if (r.w <= 0 || r.h <= 0) return;
  regions_.push_back({r, kind, arg, arg2});
}

const FmWindow::Region* FmWindow::region_at(double x, double y) const {
  for (auto it = prev_regions_.rbegin(); it != prev_regions_.rend(); ++it)
    if (it->r.contains(static_cast<int>(x), static_cast<int>(y))) return &*it;
  return nullptr;
}

void FmWindow::icon(IconKind k, double x, double y, int px) {
  cairo_surface_t* s = icons_.get(k, px);
  cairo_set_source_surface(cr_, s, std::round(x), std::round(y));
  cairo_paint(cr_);
}

void FmWindow::button(const Rect& r, const std::string& label, bool hot, bool down, bool enabled, bool accent) {
  Color top = down ? col_.btn_down_top : (hot ? col_.btn_hot_top : col_.btn_top), bot = down ? col_.btn_down_bot : (hot ? col_.btn_hot_bot : col_.btn_bot);
  Color txt = enabled ? col_.text : col_.text_off;
  if (accent && enabled) {
    top = down ? col_.sel_bot : col_.accent;
    bot = col_.accent;
    txt = col_.accent_text;
  }
  const double rad = style_->corner > 0 ? std::max(2, style_->corner) : 2;
  box(r.x, r.y, r.w, r.h, rad, top, bot, &col_.btn_border);
  const double tw = text_w(label, font_px());
  text(label, r.x + (r.w - tw) / 2, r.y + r.h / 2.0, font_px(), txt);
}

void FmWindow::glyph(int kind, double x, double y, double size, const Color& c) {
  cairo_save(cr_);
  cairo_translate(cr_, x, y);
  cairo_scale(cr_, size, size);
  kit::set_source(cr_, c);
  cairo_set_line_width(cr_, std::max(1.3 / size, 0.09));
  cairo_set_line_cap(cr_, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
  auto line = [&](double x0, double y0, double x1, double y1) {
    cairo_move_to(cr_, x0, y0);
    cairo_line_to(cr_, x1, y1);
  };
  switch (kind) {
    case G_BACK:
      line(0.8, 0.5, 0.2, 0.5);
      cairo_move_to(cr_, 0.45, 0.25);
      cairo_line_to(cr_, 0.2, 0.5);
      cairo_line_to(cr_, 0.45, 0.75);
      cairo_stroke(cr_);
      break;
    case G_FORWARD:
      line(0.2, 0.5, 0.8, 0.5);
      cairo_move_to(cr_, 0.55, 0.25);
      cairo_line_to(cr_, 0.8, 0.5);
      cairo_line_to(cr_, 0.55, 0.75);
      cairo_stroke(cr_);
      break;
    case G_UP:
      line(0.5, 0.82, 0.5, 0.2);
      cairo_move_to(cr_, 0.25, 0.45);
      cairo_line_to(cr_, 0.5, 0.2);
      cairo_line_to(cr_, 0.75, 0.45);
      cairo_stroke(cr_);
      break;
    case G_RELOAD:
      cairo_arc(cr_, 0.5, 0.5, 0.3, -0.4, 4.7);
      cairo_stroke(cr_);
      cairo_move_to(cr_, 0.75, 0.12);
      cairo_line_to(cr_, 0.8, 0.36);
      cairo_line_to(cr_, 0.57, 0.3);
      cairo_stroke(cr_);
      break;
    case G_NEWFOLDER:
      cairo_move_to(cr_, 0.1, 0.78);
      cairo_line_to(cr_, 0.1, 0.25);
      cairo_line_to(cr_, 0.38, 0.25);
      cairo_line_to(cr_, 0.46, 0.35);
      cairo_line_to(cr_, 0.85, 0.35);
      cairo_line_to(cr_, 0.85, 0.78);
      cairo_close_path(cr_);
      cairo_stroke(cr_);
      line(0.5, 0.45, 0.5, 0.7);
      line(0.375, 0.575, 0.625, 0.575);
      cairo_stroke(cr_);
      break;
    case G_COPY:
      cairo_rectangle(cr_, 0.12, 0.12, 0.45, 0.55);
      cairo_stroke(cr_);
      cairo_rectangle(cr_, 0.38, 0.3, 0.45, 0.58);
      cairo_stroke(cr_);
      break;
    case G_PASTE:
      cairo_rectangle(cr_, 0.15, 0.2, 0.55, 0.65);
      cairo_stroke(cr_);
      cairo_rectangle(cr_, 0.3, 0.1, 0.25, 0.18);
      cairo_stroke(cr_);
      cairo_rectangle(cr_, 0.5, 0.45, 0.35, 0.42);
      cairo_stroke(cr_);
      break;
    case G_CUT:
      line(0.3, 0.1, 0.65, 0.65);
      line(0.7, 0.1, 0.35, 0.65);
      cairo_stroke(cr_);
      cairo_arc(cr_, 0.3, 0.75, 0.12, 0, 2 * M_PI);
      cairo_stroke(cr_);
      cairo_arc(cr_, 0.7, 0.75, 0.12, 0, 2 * M_PI);
      cairo_stroke(cr_);
      break;
    case G_DELETE:
      line(0.2, 0.3, 0.8, 0.3);
      cairo_move_to(cr_, 0.28, 0.3);
      cairo_line_to(cr_, 0.33, 0.85);
      cairo_line_to(cr_, 0.67, 0.85);
      cairo_line_to(cr_, 0.72, 0.3);
      cairo_stroke(cr_);
      line(0.4, 0.15, 0.6, 0.15);
      cairo_stroke(cr_);
      break;
    case G_RENAME:
      line(0.2, 0.8, 0.28, 0.55);
      line(0.28, 0.55, 0.65, 0.18);
      line(0.65, 0.18, 0.82, 0.35);
      line(0.82, 0.35, 0.45, 0.72);
      line(0.45, 0.72, 0.2, 0.8);
      cairo_stroke(cr_);
      break;
    case G_PROPS:
      cairo_arc(cr_, 0.5, 0.5, 0.38, 0, 2 * M_PI);
      cairo_stroke(cr_);
      line(0.5, 0.45, 0.5, 0.7);
      cairo_stroke(cr_);
      cairo_arc(cr_, 0.5, 0.3, 0.04, 0, 2 * M_PI);
      cairo_fill(cr_);
      break;
    case G_GRID:
      for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
          cairo_rectangle(cr_, 0.15 + i * 0.37, 0.15 + j * 0.37, 0.26, 0.26);
          cairo_stroke(cr_);
        }
      break;
    case G_LIST:
      for (int i = 0; i < 3; ++i) {
        line(0.15, 0.25 + i * 0.25, 0.85, 0.25 + i * 0.25);
        cairo_stroke(cr_);
      }
      break;
    case G_DETAILS:
      for (int i = 0; i < 3; ++i) {
        line(0.12, 0.25 + i * 0.25, 0.3, 0.25 + i * 0.25);
        line(0.4, 0.25 + i * 0.25, 0.88, 0.25 + i * 0.25);
        cairo_stroke(cr_);
      }
      break;
    case G_TILES:
      cairo_rectangle(cr_, 0.12, 0.2, 0.3, 0.25);
      cairo_rectangle(cr_, 0.58, 0.2, 0.3, 0.25);
      cairo_rectangle(cr_, 0.12, 0.58, 0.3, 0.25);
      cairo_rectangle(cr_, 0.58, 0.58, 0.3, 0.25);
      cairo_stroke(cr_);
      break;
    case G_SEARCH:
      cairo_arc(cr_, 0.42, 0.42, 0.26, 0, 2 * M_PI);
      cairo_stroke(cr_);
      line(0.62, 0.62, 0.85, 0.85);
      cairo_stroke(cr_);
      break;
    case G_MENU:
      for (int i = 0; i < 3; ++i) {
        line(0.18, 0.28 + i * 0.22, 0.82, 0.28 + i * 0.22);
        cairo_stroke(cr_);
      }
      break;
    case G_EJECT:
      cairo_move_to(cr_, 0.5, 0.15);
      cairo_line_to(cr_, 0.85, 0.55);
      cairo_line_to(cr_, 0.15, 0.55);
      cairo_close_path(cr_);
      cairo_fill(cr_);
      cairo_rectangle(cr_, 0.15, 0.68, 0.7, 0.14);
      cairo_fill(cr_);
      break;
    case G_PLUG:
      cairo_arc(cr_, 0.5, 0.5, 0.3, 0, 2 * M_PI);
      cairo_stroke(cr_);
      line(0.2, 0.5, 0.8, 0.5);
      cairo_stroke(cr_);
      cairo_move_to(cr_, 0.5, 0.2);
      cairo_curve_to(cr_, 0.35, 0.35, 0.35, 0.65, 0.5, 0.8);
      cairo_stroke(cr_);
      cairo_move_to(cr_, 0.5, 0.2);
      cairo_curve_to(cr_, 0.65, 0.35, 0.65, 0.65, 0.5, 0.8);
      cairo_stroke(cr_);
      break;
    case G_HOME:
      cairo_move_to(cr_, 0.1, 0.5);
      cairo_line_to(cr_, 0.5, 0.15);
      cairo_line_to(cr_, 0.9, 0.5);
      cairo_stroke(cr_);
      cairo_rectangle(cr_, 0.22, 0.48, 0.56, 0.38);
      cairo_stroke(cr_);
      break;
    case G_CHEV_R:
      cairo_move_to(cr_, 0.35, 0.2);
      cairo_line_to(cr_, 0.65, 0.5);
      cairo_line_to(cr_, 0.35, 0.8);
      cairo_stroke(cr_);
      break;
    case G_CHEV_L:
      cairo_move_to(cr_, 0.65, 0.2);
      cairo_line_to(cr_, 0.35, 0.5);
      cairo_line_to(cr_, 0.65, 0.8);
      cairo_stroke(cr_);
      break;
    case G_CHEV_D:
      cairo_move_to(cr_, 0.2, 0.35);
      cairo_line_to(cr_, 0.5, 0.65);
      cairo_line_to(cr_, 0.8, 0.35);
      cairo_stroke(cr_);
      break;
    case G_CLOSE:
      line(0.25, 0.25, 0.75, 0.75);
      line(0.75, 0.25, 0.25, 0.75);
      cairo_stroke(cr_);
      break;
    case G_PLUS:
      line(0.5, 0.2, 0.5, 0.8);
      line(0.2, 0.5, 0.8, 0.5);
      cairo_stroke(cr_);
      break;
    case G_SORT_UP:
      cairo_move_to(cr_, 0.2, 0.7);
      cairo_line_to(cr_, 0.5, 0.3);
      cairo_line_to(cr_, 0.8, 0.7);
      cairo_close_path(cr_);
      cairo_fill(cr_);
      break;
    case G_SORT_DOWN:
      cairo_move_to(cr_, 0.2, 0.3);
      cairo_line_to(cr_, 0.5, 0.7);
      cairo_line_to(cr_, 0.8, 0.3);
      cairo_close_path(cr_);
      cairo_fill(cr_);
      break;
    case G_CHECK:
      cairo_move_to(cr_, 0.2, 0.55);
      cairo_line_to(cr_, 0.42, 0.75);
      cairo_line_to(cr_, 0.82, 0.25);
      cairo_stroke(cr_);
      break;
    case G_PANE:
      cairo_rectangle(cr_, 0.12, 0.2, 0.76, 0.6);
      cairo_stroke(cr_);
      line(0.62, 0.2, 0.62, 0.8);
      cairo_stroke(cr_);
      break;
    case G_OPEN:
      cairo_move_to(cr_, 0.1, 0.8);
      cairo_line_to(cr_, 0.1, 0.25);
      cairo_line_to(cr_, 0.38, 0.25);
      cairo_line_to(cr_, 0.46, 0.35);
      cairo_line_to(cr_, 0.85, 0.35);
      cairo_line_to(cr_, 0.85, 0.5);
      cairo_stroke(cr_);
      cairo_move_to(cr_, 0.1, 0.8);
      cairo_line_to(cr_, 0.25, 0.5);
      cairo_line_to(cr_, 0.95, 0.5);
      cairo_line_to(cr_, 0.8, 0.8);
      cairo_close_path(cr_);
      cairo_stroke(cr_);
      break;
    case G_STAR:
      for (int i = 0; i < 10; ++i) {
        const double r = i % 2 ? 0.2 : 0.42, a = -M_PI / 2 + i * M_PI / 5;
        if (i == 0) cairo_move_to(cr_, 0.5 + r * std::cos(a), 0.55 + r * std::sin(a));
        else cairo_line_to(cr_, 0.5 + r * std::cos(a), 0.55 + r * std::sin(a));
      }
      cairo_close_path(cr_);
      cairo_stroke(cr_);
      break;
    default: break;
  }
  cairo_restore(cr_);
}

// ---------------------------------------------------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::draw(cairo_t* cr, int w, int h) {
  cr_ = cr;
  W_ = w;
  H_ = h;
  regions_.clear();
  if (nav_dirty_) rebuild_nav();
  reap_jobs();

  LayoutInput in;
  in.width = w;
  in.height = h;
  in.nav_pane = s_.show_navigation_pane;
  in.details_pane = s_.show_details_pane && style_->details_pane;
  in.preview = s_.show_preview_pane;
  in.status_bar = s_.show_status_bar;
  in.search_open = search_box_open_ || search_focus_ || !search_edit_.text.empty();
  in.menu_bar = s_.menu_bar == MenuBar::Always || (s_.menu_bar == MenuBar::Alt && menu_bar_shown_);
  in.tabs = tabs_.size() > 1 || s_.always_show_tabs;
  in.nav_width_override = s_.nav_width;
  lay_ = compute_layout(*style_, in);
  if (Browser& cur = tab(); !cur.stat_complete && cur.selected_count() > 1) {
    bool complete = true;
    cur.selected_bytes(&complete);
    if (!complete) request_full_stat();  // a selection reached rows that were never drawn: read their sizes in the background
  }

  cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
  kit::set_source(cr_, col_.window);
  cairo_paint(cr_);
  cairo_set_operator(cr_, CAIRO_OPERATOR_OVER);

  paint_content();
  paint_preview();
  paint_nav();
  paint_pane_and_status();
  paint_chrome();
  paint_tabs();
  if (lay_.menu.h) paint_menu_bar();
  paint_overlays();
  prev_regions_ = regions_;
}

void FmWindow::paint_chrome() {
  // one background for both rows
  const int bottom = std::max({lay_.toolbar.y + lay_.toolbar.h, lay_.address.h ? lay_.address.y + lay_.address.h + 3 : 0});
  box(0, lay_.menu.h, W_, bottom - lay_.menu.h, 0, col_.chrome_top, col_.chrome_bot, nullptr);
  cairo_move_to(cr_, 0, bottom - 0.5);
  cairo_line_to(cr_, W_, bottom - 0.5);
  kit::set_source(cr_, col_.chrome_border);
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  if (style_->address_in_toolbar_row || style_->toolbar == ToolbarKind::HeaderBar || style_->toolbar == ToolbarKind::Finder || style_->toolbar == ToolbarKind::Ribbon) {
    // back / forward (and Up in the ribbon style) live in the address row
    const Browser& b = tab();
    const int bw = lay_.back_forward_w;
    const int cy = lay_.address.h ? lay_.address.y + lay_.address.h / 2 : lay_.toolbar.y + lay_.toolbar.h / 2;
    const bool fancy = s_.style == ViewStyle::Windows7;
    const int nbtn = style_->toolbar == ToolbarKind::Ribbon ? 3 : 2;
    for (int k = 0; k < nbtn; ++k) {
      const bool enabled = k == 0 ? b.can_back() : (k == 1 ? b.can_forward() : b.can_up());
      const Cmd cmd_k = k == 0 ? Cmd::Back : (k == 1 ? Cmd::Forward : Cmd::Up);
      const int glyph_k = k == 0 ? G_BACK : (k == 1 ? G_FORWARD : G_UP);
      const double d = fancy ? 28 : 26;
      const double x = 6 + k * (d + 2), y = cy - d / 2;
      const Rect r{static_cast<int>(x), static_cast<int>(y), static_cast<int>(d), static_cast<int>(d)};
      const Region* hr = region_at(mx_, my_);
      const bool hot = hr && hr->kind == R::Tool && hr->arg == static_cast<int>(cmd_k);
      if (fancy) {
        cairo_arc(cr_, x + d / 2, y + d / 2, d / 2 - 0.5, 0, 2 * M_PI);
        cairo_pattern_t* p = cairo_pattern_create_linear(0, y, 0, y + d);
        if (enabled) {
          cairo_pattern_add_color_stop_rgb(p, 0, hot ? 0.62 : 0.52, hot ? 0.80 : 0.72, hot ? 0.96 : 0.92);
          cairo_pattern_add_color_stop_rgb(p, 1, hot ? 0.18 : 0.15, hot ? 0.42 : 0.36, hot ? 0.78 : 0.70);
        } else {
          cairo_pattern_add_color_stop_rgb(p, 0, 0.88, 0.90, 0.93);
          cairo_pattern_add_color_stop_rgb(p, 1, 0.78, 0.80, 0.85);
        }
        cairo_set_source(cr_, p);
        cairo_fill_preserve(cr_);
        cairo_pattern_destroy(p);
        kit::set_source(cr_, enabled ? Color{0.1, 0.25, 0.5, 1} : Color{0.6, 0.64, 0.7, 1});
        cairo_set_line_width(cr_, 1);
        cairo_stroke(cr_);
        glyph(glyph_k, x + 5, y + 5, d - 10, enabled ? Color{1, 1, 1, 1} : Color{0.95, 0.96, 0.98, 1});
      } else {
        if (hot && enabled) box(r.x, r.y, r.w, r.h, style_->corner ? 4 : 2, col_.btn_hot_top, col_.btn_hot_bot, nullptr);
        glyph(glyph_k, x + 4, y + 4, d - 8, enabled ? col_.text : col_.text_off);
      }
      if (enabled) add_region(r, R::Tool, static_cast<int>(cmd_k));
    }
    (void)bw;
  }
  paint_toolbar();
  paint_address_row();
}

std::vector<FmWindow::ToolItem> FmWindow::tool_items() const {
  const Browser& b = tab();
  const bool has_sel = b.selected_count() > 0;
  const bool one = b.selected_count() == 1;
  const bool fs_place = b.place == PlaceKind::Local || b.place == PlaceKind::Remote;
  std::vector<ToolItem> t;
  auto add = [&](Cmd c, int g, const char* label, bool enabled = true, bool dropdown = false, bool right = false, bool sep = false, int arg = 0) {
    ToolItem it;
    it.cmd = c;
    it.glyph = g;
    it.label = label;
    it.enabled = enabled;
    it.dropdown = dropdown;
    it.right = right;
    it.sep_before = sep;
    it.arg = arg;
    t.push_back(it);
  };
  const bool on_pc = b.place == PlaceKind::Computer || b.place == PlaceKind::Network;
  switch (style_->toolbar) {
    case ToolbarKind::CommandBar:
      add(Cmd::MenuOrganize, G_MENU, "Organize", true, true);
      if (on_pc) {
        add(Cmd::ConnectServer, G_PLUG, "Connect to server");
        add(Cmd::AddNextcloud, G_CLOUD, "Add Nextcloud");
        add(Cmd::Eject, G_EJECT, "Eject", has_sel || false);
      } else if (b.place == PlaceKind::Trash) {
        add(Cmd::EmptyTrash, G_DELETE, "Empty the Trash");
        add(Cmd::Restore, G_BACK, "Restore selected items", has_sel);
      } else {
        add(Cmd::Open, G_OPEN, "Open", one);
        add(Cmd::NewFolder, G_NEWFOLDER, "New folder", fs_place);
        if (volume_for_path(b.path) && (volume_for_path(b.path)->ejectable || volume_for_path(b.path)->kind == DriveKind::Network) && !volume_for_path(b.path)->system)
          add(Cmd::Eject, G_EJECT, "Eject", true);
      }
      add(Cmd::MenuView, G_DETAILS, "", true, true, true);
      add(Cmd::TogglePreview, G_PANE, "", true, false, true);
      break;
    case ToolbarKind::Ribbon:
      add(Cmd::Copy, G_COPY, "Copy", has_sel);
      add(Cmd::Paste, G_PASTE, "Paste", fs_place && (!clip_.empty() || true));
      add(Cmd::Cut, G_CUT, "Cut", has_sel);
      add(Cmd::Delete, G_DELETE, "Delete", has_sel, false, false, true);
      add(Cmd::Rename, G_RENAME, "Rename", one);
      add(Cmd::NewFolder, G_NEWFOLDER, "New folder", fs_place, false, false, true);
      add(Cmd::Properties, G_PROPS, "Properties", true);
      if (on_pc) add(Cmd::ConnectServer, G_PLUG, "Map network drive", true, false, false, true);
      if (on_pc) add(Cmd::Eject, G_EJECT, "Eject", has_sel);
      add(Cmd::MenuView, G_DETAILS, "View", true, true, true);
      add(Cmd::Settings, G_MENU, "Options", true, false, true);
      break;
    case ToolbarKind::HeaderBar:
      add(Cmd::MenuView, G_DETAILS, "", true, true, true);
      add(Cmd::FocusSearch, G_SEARCH, "", true, false, true);
      add(Cmd::MenuOrganize, G_MENU, "", true, false, true);
      break;
    case ToolbarKind::Finder:
      add(Cmd::SetView, G_GRID, "", true, false, true, false, static_cast<int>(ViewMode::MediumIcons));
      add(Cmd::SetView, G_LIST, "", true, false, true, false, static_cast<int>(ViewMode::Details));
      add(Cmd::MenuSort, G_SORT_DOWN, "", true, true, true);
      add(Cmd::FocusSearch, G_SEARCH, "", true, false, true);
      add(Cmd::MenuOrganize, G_MENU, "", true, false, true);
      break;
    case ToolbarKind::Icons:
    case ToolbarKind::Compact:
      add(Cmd::Back, G_BACK, "Back", b.can_back(), false, false, false);
      add(Cmd::Forward, G_FORWARD, "Forward", b.can_forward());
      add(Cmd::Up, G_UP, "Up", b.can_up());
      add(Cmd::Reload, G_RELOAD, "Reload");
      add(Cmd::Home, G_HOME, "Home");
      if (style_->toolbar == ToolbarKind::Compact) {
        add(Cmd::NewFolder, G_NEWFOLDER, "", fs_place, false, false, true);
        add(Cmd::Cut, G_CUT, "", has_sel);
        add(Cmd::Copy, G_COPY, "", has_sel);
        add(Cmd::Paste, G_PASTE, "", fs_place);
      }
      add(Cmd::SetView, G_GRID, "", true, false, true, false, static_cast<int>(ViewMode::MediumIcons));
      add(Cmd::SetView, G_LIST, "", true, false, true, false, static_cast<int>(ViewMode::List));
      add(Cmd::SetView, G_DETAILS, "", true, false, true, false, static_cast<int>(ViewMode::Details));
      add(Cmd::MenuOrganize, G_MENU, "", true, false, true);
      break;
  }
  return t;
}

void FmWindow::tool_button(Rect r, const ToolItem& t, bool icon_only) {
  const Region* hr = region_at(mx_, my_);
  const bool hot = hr && hr->kind == R::Tool && hr->arg == static_cast<int>(t.cmd) && hr->arg2 == t.arg && t.enabled;
  const bool down = hot && sel_drag_ == false && dragging_ == false && false;
  const bool active_view = t.cmd == Cmd::SetView && tab().mode == static_cast<ViewMode>(t.arg);
  const bool win7 = s_.style == ViewStyle::Windows7;
  if (hot || active_view || t.toggled) {
    if (win7 || s_.style == ViewStyle::Windows10) box(r.x, r.y, r.w, r.h, win7 ? 3 : 0, active_view ? col_.btn_down_top : col_.btn_hot_top, active_view ? col_.btn_down_bot : col_.btn_hot_bot, win7 ? &col_.btn_border : nullptr);
    else box(r.x, r.y, r.w, r.h, style_->corner, active_view ? col_.btn_down_top : col_.btn_hot_top, active_view ? col_.btn_down_bot : col_.btn_hot_bot, &col_.btn_border);
  }
  (void)down;
  const Color c = t.enabled ? col_.text : col_.text_off;
  double x = r.x + 6;
  const double gs = std::min<double>(r.h - 8, 18);
  if (t.glyph) {
    glyph(t.glyph, x, r.y + (r.h - gs) / 2, gs, t.enabled ? (win7 || s_.style == ViewStyle::Windows10 ? Color{0.2, 0.35, 0.6, 1} : col_.text) : col_.text_off);
    x += gs + (icon_only ? 0 : 5);
  }
  if (!icon_only && !t.label.empty()) x += text(t.label, x, r.y + r.h / 2.0, font_px(), c);
  if (t.dropdown) glyph(G_CHEV_D, x + (icon_only ? 3 : 4), r.y + (r.h - 10) / 2.0, 10, c);
  if (t.enabled) add_region(r, R::Tool, static_cast<int>(t.cmd), t.arg);
}

void FmWindow::paint_toolbar() {
  const Rect bar = lay_.toolbar;
  if (bar.h == 0) return;
  std::vector<ToolItem> items = tool_items();
  const bool icon_only_style = style_->toolbar == ToolbarKind::Icons || style_->toolbar == ToolbarKind::Compact || style_->toolbar == ToolbarKind::HeaderBar || style_->toolbar == ToolbarKind::Finder;
  int left_x = style_->toolbar == ToolbarKind::HeaderBar || style_->toolbar == ToolbarKind::Finder ? 0 : 6;
  if (style_->address_in_toolbar_row || style_->toolbar == ToolbarKind::HeaderBar || style_->toolbar == ToolbarKind::Finder) left_x = 8;
  int right_x = bar.w - 6;
  const int by = bar.y + 3, bh = bar.h - 6;
  if (style_->toolbar == ToolbarKind::HeaderBar || style_->toolbar == ToolbarKind::Finder) {
    // the buttons sit in the address row on these styles
  }
  for (const ToolItem& t : items) {
    const bool io = icon_only_style || t.label.empty();
    const double tw = (io ? 0 : text_w(t.label, font_px())) + (t.glyph ? 18 + (io ? 0 : 5) : 0) + (t.dropdown ? 14 : 0) + 14;
    const int w = static_cast<int>(std::max(io ? 30.0 : 0.0, tw));
    if (t.right) {
      right_x -= w;
      tool_button({right_x, by, w, bh}, t, io);
      right_x -= 2;
    } else {
      if (t.sep_before) {
        cairo_move_to(cr_, left_x + 2.5, by + 4);
        cairo_line_to(cr_, left_x + 2.5, by + bh - 4);
        kit::set_source(cr_, col_.chrome_border);
        cairo_set_line_width(cr_, 1);
        cairo_stroke(cr_);
        left_x += 6;
      }
      if (left_x + w > right_x) continue;  // no room: it is in the Organize menu anyway
      tool_button({left_x, by, w, bh}, t, io);
      left_x += w + 2;
    }
  }
  if (style_->toolbar == ToolbarKind::Finder || style_->toolbar == ToolbarKind::HeaderBar) {
    // title centred between the buttons
    std::string title_text;
    const auto c = crumbs();
    title_text = c.empty() ? "" : c.back().first;
    const std::string t = fit(title_text, std::max(40, right_x - left_x - 180), font_px() + 1, true);
    if (style_->toolbar == ToolbarKind::Finder) text(t, left_x + 60, bar.y + bar.h / 2.0, font_px() + 1, col_.text, true);
  }
}

std::vector<std::pair<std::string, std::string>> FmWindow::crumbs() const {
  const Browser& b = tab();
  std::vector<std::pair<std::string, std::string>> out;
  auto add_components = [&](const std::string& base_label, const std::string& base_path, const std::string& full_path) {
    out.emplace_back(base_label, base_path);
    std::string cur = base_path;
    std::string rest = full_path.substr(std::min(full_path.size(), base_path == "/" ? 1 : base_path.size()));
    size_t i = 0;
    while (i < rest.size()) {
      if (rest[i] == '/') {
        ++i;
        continue;
      }
      const size_t j = rest.find('/', i);
      const std::string part = rest.substr(i, j == std::string::npos ? std::string::npos : j - i);
      cur += (cur.back() == '/' ? "" : "/") + part;
      out.emplace_back(part, cur);
      i = j == std::string::npos ? rest.size() : j;
    }
  };
  switch (b.place) {
    case PlaceKind::Computer: out.emplace_back(s_.style == ViewStyle::Windows10 ? "This PC" : "Computer", "computer:///"); break;
    case PlaceKind::Network: out.emplace_back("Network", "network:///"); break;
    case PlaceKind::Trash: out.emplace_back("Trash", "trash:///"); break;
    case PlaceKind::Recent: out.emplace_back("Recent Places", "recent:///"); break;
    case PlaceKind::Remote: {
      out.emplace_back("Network", "network:///");
      const Uri u = parse_uri(b.uri);
      Uri base = u;
      base.path.clear();
      out.emplace_back(u.host.empty() ? b.uri : u.host, uri_to_string(base));
      std::string cur;
      std::string p = u.path;
      size_t i = 0;
      while (i < p.size()) {
        if (p[i] == '/') {
          ++i;
          continue;
        }
        const size_t j = p.find('/', i);
        const std::string part = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        cur += "/" + part;
        Uri step = u;
        step.path = cur;
        out.emplace_back(part, uri_to_string(step));
        i = j == std::string::npos ? p.size() : j;
      }
      break;
    }
    default: {
      out.emplace_back(s_.style == ViewStyle::Windows10 ? "This PC" : "Computer", "computer:///");
      const Volume* v = volume_for_path(b.path);
      if (v && v->mountpoint != "/") add_components(volume_display_name(*v), v->mountpoint, b.path);
      else if (v) add_components(volume_display_name(*v), "/", b.path);
      else add_components("/", "/", b.path);
      break;
    }
  }
  if (!b.search.empty()) out.emplace_back("Search Results", b.address());
  return out;
}

void FmWindow::paint_breadcrumbs(const Rect& r) {
  const bool win7 = s_.style == ViewStyle::Windows7;
  box(r.x, r.y, r.w, r.h, win7 ? 2 : (style_->corner ? 4 : 1), col_.field, col_.field, &col_.field_border);
  add_region(r, R::Address, 0);
  const double px = font_px();
  if (addr_focus_) {
    const double tx = r.x + 8;
    cairo_save(cr_);
    use_clip({r.x + 2, r.y + 1, r.w - 4, r.h - 2});
    if (addr_edit_.has_selection()) {
      const double a = text_w(addr_edit_.text.substr(0, addr_edit_.sel_begin()), px), b = text_w(addr_edit_.text.substr(0, addr_edit_.sel_end()), px);
      box(tx + a, r.y + 3, b - a, r.h - 6, 0, col_.accent, col_.accent, nullptr);
    }
    text(addr_edit_.text, tx, r.y + r.h / 2.0, px, col_.text);
    if (static_cast<long>(now_() * 2) % 2 == 0 || true) {
      const double cx = tx + text_w(addr_edit_.text.substr(0, addr_edit_.caret), px);
      cairo_move_to(cr_, std::round(cx) + 0.5, r.y + 4);
      cairo_line_to(cr_, std::round(cx) + 0.5, r.y + r.h - 4);
      kit::set_source(cr_, col_.text);
      cairo_set_line_width(cr_, 1);
      cairo_stroke(cr_);
    }
    cairo_restore(cr_);
    return;
  }
  const auto c = crumbs();
  // folder icon at the left
  IconKind ik = IconKind::Folder;
  switch (tab().place) {
    case PlaceKind::Computer: ik = IconKind::Computer; break;
    case PlaceKind::Network:
    case PlaceKind::Remote: ik = IconKind::Network; break;
    case PlaceKind::Trash: ik = IconKind::Trash; break;
    case PlaceKind::Recent: ik = IconKind::Recent; break;
    default: break;
  }
  icon(ik, r.x + 5, r.y + (r.h - 16) / 2.0, 16);
  double x = r.x + 26;
  const double avail_end = r.x + r.w - 26;
  // Drop the leftmost crumbs when the path is too long, like Explorer's address bar does.
  std::vector<double> widths;
  double total = 0;
  for (const auto& cr : c) {
    widths.push_back(text_w(cr.first, px) + 12 + 14);
    total += widths.back();
  }
  size_t first = 0;
  while (first + 1 < c.size() && x + total > avail_end) total -= widths[first++];
  if (first > 0) {
    glyph(G_CHEV_L, x, r.y + (r.h - 12) / 2.0, 12, col_.text_dim);
    x += 14;
  }
  cairo_save(cr_);
  use_clip({r.x + 2, r.y + 1, r.w - 4, r.h - 2});
  const Region* hr = region_at(mx_, my_);
  for (size_t i = first; i < c.size(); ++i) {
    const double tw = text_w(c[i].first, px);
    const Rect cr_r{static_cast<int>(x), r.y + 2, static_cast<int>(tw + 12), r.h - 4};
    const bool hot = hr && hr->kind == R::Crumb && hr->arg == static_cast<int>(i);
    if (hot) box(cr_r.x, cr_r.y, cr_r.w, cr_r.h, win7 ? 2 : 3, col_.hot_top, col_.hot_bot, &col_.hot_border);
    text(c[i].first, x + 6, r.y + r.h / 2.0, px, col_.text);
    add_region(cr_r, R::Crumb, static_cast<int>(i));
    x += tw + 12;
    const Rect ar{static_cast<int>(x), r.y + 2, 14, r.h - 4};
    const bool ahot = hr && hr->kind == R::CrumbArrow && hr->arg == static_cast<int>(i);
    if (ahot) box(ar.x, ar.y, ar.w, ar.h, 2, col_.hot_top, col_.hot_bot, &col_.hot_border);
    glyph(G_CHEV_R, x + 2, r.y + (r.h - 10) / 2.0, 10, col_.text_dim);
    add_region(ar, R::CrumbArrow, static_cast<int>(i));
    x += 14;
    if (x > avail_end) break;
  }
  cairo_restore(cr_);
}

void FmWindow::paint_address_row() {
  const Rect a = lay_.address;
  const Browser& b = tab();
  if (a.w > 0 && a.h > 0) {
    if (style_->address == AddressKind::Breadcrumb && !(style_->toolbar == ToolbarKind::Finder)) {
      paint_breadcrumbs(a);
    } else {
      // path entry: always the text field
      box(a.x, a.y, a.w, a.h, style_->corner ? 3 : 1, col_.field, col_.field, &col_.field_border);
      add_region(a, R::Address, 0);
      const double px = font_px();
      const double tx = a.x + 8;
      cairo_save(cr_);
      use_clip({a.x + 2, a.y + 1, a.w - 4, a.h - 2});
      if (addr_focus_) {
        if (addr_edit_.has_selection()) {
          const double s0 = text_w(addr_edit_.text.substr(0, addr_edit_.sel_begin()), px), s1 = text_w(addr_edit_.text.substr(0, addr_edit_.sel_end()), px);
          box(tx + s0, a.y + 3, s1 - s0, a.h - 6, 0, col_.accent, col_.accent, nullptr);
        }
        text(addr_edit_.text, tx, a.y + a.h / 2.0, px, col_.text);
        const double cx = tx + text_w(addr_edit_.text.substr(0, addr_edit_.caret), px);
        cairo_move_to(cr_, std::round(cx) + 0.5, a.y + 4);
        cairo_line_to(cr_, std::round(cx) + 0.5, a.y + a.h - 4);
        kit::set_source(cr_, col_.text);
        cairo_set_line_width(cr_, 1);
        cairo_stroke(cr_);
      } else {
        text(b.address(), tx, a.y + a.h / 2.0, px, col_.text);
      }
      cairo_restore(cr_);
    }
  }
  // search box
  const bool searchable = b.place == PlaceKind::Local || b.place == PlaceKind::Remote;
  const bool show_box = style_->search_box_visible || search_box_open_ || search_focus_ || !search_edit_.text.empty();
  if (show_box && lay_.search_w > 0) {
    Rect sr;
    if (a.w > 0) sr = {a.x + a.w + 8, a.y, W_ - (a.x + a.w) - 16, a.h};
    else sr = {W_ - lay_.search_w - 160, lay_.toolbar.y + 6, lay_.search_w, lay_.toolbar.h - 12};
    sr.w = std::min(sr.w, std::max(lay_.search_w, 140));
    if (a.w > 0) sr.x = a.x + a.w + 8;
    box(sr.x, sr.y, sr.w, sr.h, s_.style == ViewStyle::Windows7 ? 2 : (style_->corner ? sr.h / 2 : 1), col_.field, col_.field, &col_.field_border);
    add_region(sr, R::Search, 0);
    const double px = font_px();
    cairo_save(cr_);
    use_clip({sr.x + 2, sr.y + 1, sr.w - 4 - 18, sr.h - 2});
    if (search_edit_.text.empty() && !search_focus_) {
      const std::string name = b.place == PlaceKind::Local ? (b.path == "/" ? "/" : fs::path(b.path).filename().string()) : "folder";
      text(fit("Search " + name, sr.w - 34, px), sr.x + 8, sr.y + sr.h / 2.0, px, col_.text_off);
    } else {
      const double tx = sr.x + 8;
      if (search_edit_.has_selection()) {
        const double s0 = text_w(search_edit_.text.substr(0, search_edit_.sel_begin()), px), s1 = text_w(search_edit_.text.substr(0, search_edit_.sel_end()), px);
        box(tx + s0, sr.y + 3, s1 - s0, sr.h - 6, 0, col_.accent, col_.accent, nullptr);
      }
      text(search_edit_.text, tx, sr.y + sr.h / 2.0, px, col_.text);
      if (search_focus_) {
        const double cx = tx + text_w(search_edit_.text.substr(0, search_edit_.caret), px);
        cairo_move_to(cr_, std::round(cx) + 0.5, sr.y + 4);
        cairo_line_to(cr_, std::round(cx) + 0.5, sr.y + sr.h - 4);
        kit::set_source(cr_, col_.text);
        cairo_set_line_width(cr_, 1);
        cairo_stroke(cr_);
      }
    }
    cairo_restore(cr_);
    if (!search_edit_.text.empty()) {
      const Rect xr{sr.x + sr.w - 20, sr.y + 2, 18, sr.h - 4};
      glyph(G_CLOSE, xr.x + 3, xr.y + (xr.h - 12) / 2.0, 12, col_.text_dim);
      add_region(xr, R::SearchClear, 0);
    } else {
      glyph(G_SEARCH, sr.x + sr.w - 20, sr.y + (sr.h - 14) / 2.0, 14, searchable ? col_.text_dim : col_.text_off);
    }
  }
}

void FmWindow::paint_menu_bar() {
  const Rect r = lay_.menu;
  box(r.x, r.y, r.w, r.h, 0, col_.chrome_top, col_.chrome_top, nullptr);
  static const char* names[] = {"File", "Edit", "View", "Tools", "Help"};
  double x = 6;
  for (int i = 0; i < 5; ++i) {
    const double w = text_w(names[i], font_px()) + 16;
    const Rect rr{static_cast<int>(x), r.y, static_cast<int>(w), r.h};
    const Region* hr = region_at(mx_, my_);
    if (hr && hr->kind == R::MenuBar && hr->arg == i) box(rr.x, rr.y + 1, rr.w, rr.h - 2, 2, col_.hot_top, col_.hot_bot, &col_.hot_border);
    text(names[i], x + 8, r.y + r.h / 2.0, font_px(), col_.text);
    add_region(rr, R::MenuBar, i);
    x += w;
  }
}

void FmWindow::paint_tabs() {
  const Rect r = lay_.tabs;
  if (r.h == 0) return;
  box(r.x, r.y, r.w, r.h, 0, col_.tab_off, col_.tab_off, nullptr);
  const int n = static_cast<int>(tabs_.size());
  const int newbtn = 30;
  const int w = std::clamp((r.w - newbtn - 8) / std::max(1, n), 70, 200);
  const Region* hr = region_at(mx_, my_);
  for (int i = 0; i < n; ++i) {
    const Rect tr{4 + i * w, r.y + 3, w - 2, r.h - 3};
    const bool on = static_cast<size_t>(i) == cur_;
    const bool hot = hr && (hr->kind == R::Tab || hr->kind == R::TabClose) && hr->arg == i;
    box(tr.x, tr.y, tr.w, tr.h + 1, 4, on ? col_.tab_on : (hot ? col_.btn_hot_top : col_.tab_off), on ? col_.tab_on : (hot ? col_.btn_hot_bot : col_.tab_off), &col_.tab_border);
    if (on) box(tr.x + 1, tr.y + tr.h - 2, tr.w - 2, 4, 0, col_.tab_on, col_.tab_on, nullptr);
    const Browser& tb = tabs_[i];
    IconKind ik = IconKind::Folder;
    if (tb.place == PlaceKind::Computer) ik = IconKind::Computer;
    else if (tb.place == PlaceKind::Trash) ik = IconKind::Trash;
    else if (tb.place == PlaceKind::Network || tb.place == PlaceKind::Remote) ik = IconKind::Network;
    else if (tb.place == PlaceKind::Recent) ik = IconKind::Recent;
    icon(ik, tr.x + 8, tr.y + (tr.h - 16) / 2.0, 16);
    std::string name;
    switch (tb.place) {
      case PlaceKind::Computer: name = "Computer"; break;
      case PlaceKind::Network: name = "Network"; break;
      case PlaceKind::Trash: name = "Trash"; break;
      case PlaceKind::Recent: name = "Recent Places"; break;
      default: name = tb.path == "/" ? "/" : fs::path(tb.path).filename().string();
    }
    cairo_save(cr_);
    use_clip({tr.x, tr.y, tr.w - 20, tr.h});
    text(fit(name, tr.w - 54, font_px()), tr.x + 30, tr.y + tr.h / 2.0, font_px(), on ? col_.text : col_.text_dim);
    cairo_restore(cr_);
    const Rect xr{tr.x + tr.w - 20, tr.y + (tr.h - 16) / 2, 16, 16};
    if (n > 1 && (on || hot)) {
      const bool xhot = hr && hr->kind == R::TabClose && hr->arg == i;
      if (xhot) box(xr.x, xr.y, xr.w, xr.h, 3, col_.hot_top, col_.hot_bot, &col_.hot_border);
      glyph(G_CLOSE, xr.x + 2, xr.y + 2, 12, col_.text_dim);
      add_region(xr, R::TabClose, i);
    }
    add_region(tr, R::Tab, i);
  }
  const Rect nr{4 + n * w + 2, r.y + 4, 26, r.h - 8};
  const bool nhot = hr && hr->kind == R::TabNew;
  if (nhot) box(nr.x, nr.y, nr.w, nr.h, 3, col_.hot_top, col_.hot_bot, &col_.hot_border);
  glyph(G_PLUS, nr.x + 6, nr.y + (nr.h - 14) / 2.0, 14, col_.text_dim);
  add_region(nr, R::TabNew);
}

// ---------------------------------------------------------------------------------------------------------------------
// navigation pane
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::paint_nav() {
  const Rect r = lay_.nav;
  if (r.w <= 0) return;
  cairo_save(cr_);
  use_clip(r);
  box(r.x, r.y, r.w, r.h, 0, col_.nav_bg, col_.nav_bg, nullptr);
  const bool tree = style_->nav == NavKind::Tree;
  const int rh = tree ? (s_.compact_rows ? 20 : 22) : (style_->row_h + 2);
  const auto& rows = nav_.rows();
  int cur = nav_.row_for_address(tab().place == PlaceKind::Local || tab().place == PlaceKind::Remote ? tab().path : tab().address());
  const Region* hr = region_at(mx_, my_);
  double y = r.y + 4 - nav_scroll_;
  double content_h = 4;
  for (size_t i = 0; i < rows.size(); ++i) {
    const NavRow& row = rows[i];
    int h = rh;
    if (row.header && !tree) h = rh + 6;
    if (row.has_volume && !row.sub.empty() && !tree) h = rh + 10;
    if (row.header && tree) h = rh + 4;
    content_h += h;
    if (y + h < r.y || y > r.y + r.h) {
      y += h;
      continue;
    }
    const Rect rr{r.x + 2, static_cast<int>(y), r.w - 4, h};
    const bool sel = static_cast<int>(i) == cur;
    const bool hot = (hr && (hr->kind == R::NavItem || hr->kind == R::NavArrow) && hr->arg == static_cast<int>(i)) || (!drop_hot_.empty() && row.address == drop_hot_);
    if (row.header && !tree) {
      text(lower_copy(row.label).empty() ? row.label : row.label, r.x + 12, y + h - 10, font_px() - 1, col_.nav_head, true);
      y += h;
      continue;
    }
    if (sel || hot) {
      const Color& t = sel ? (focused_ ? col_.sel_top : col_.selx_top) : col_.hot_top;
      const Color& b2 = sel ? (focused_ ? col_.sel_bot : col_.selx_bot) : col_.hot_bot;
      const Color& bd = sel ? (focused_ ? col_.sel_border : col_.selx_border) : col_.hot_border;
      box(rr.x, rr.y, rr.w, rr.h, style_->corner > 4 ? style_->corner : (s_.style == ViewStyle::Windows7 ? 3 : 2), t, b2, &bd);
    }
    const Color tc = sel && focused_ && style_->selection != SelectionPaint::Win7Glass && style_->selection != SelectionPaint::Flat ? col_.sel_text : (sel && focused_ && col_.sel_text.r > 0.9 ? col_.sel_text : col_.nav_text);
    double x = r.x + 6 + row.depth * 14;
    if (tree && row.expandable) {
      const bool ahot = hr && hr->kind == R::NavArrow && hr->arg == static_cast<int>(i);
      glyph(row.expanded ? G_CHEV_D : G_CHEV_R, x - 1, y + (h - 12) / 2.0, 12, ahot ? col_.accent : col_.text_dim);
      add_region({static_cast<int>(x - 2), static_cast<int>(y), 16, h}, R::NavArrow, static_cast<int>(i));
    }
    x += tree ? 14 : 6;
    if (row.header) {
      icon(row.icon, x, y + (h - 16) / 2.0, 16);
      text(row.label, x + 22, y + h / 2.0, font_px(), tc, true);
      add_region(rr, R::NavItem, static_cast<int>(i));
      y += h;
      continue;
    }
    const double iy = y + (rh - 16) / 2.0 + 0;
    icon(row.icon, x, iy, 16);
    const double tx = x + 22;
    const double avail = r.x + r.w - tx - (row.eject ? 26 : 8);
    text(fit(row.label, avail, font_px()), tx, y + rh / 2.0, font_px(), tc);
    if (row.has_volume && !row.sub.empty() && !tree && s_.show_free_space_bars) {
      const Rect bar{static_cast<int>(tx), static_cast<int>(y + rh - 1), static_cast<int>(std::max(20.0, avail)), 5};
      paint_progress(bar, row.used_fraction, row.used_fraction > 0.9);
    }
    if (row.eject) {
      const Rect er{r.x + r.w - 24, static_cast<int>(y + (rh - 16) / 2), 18, 16};
      const bool ehot = hr && hr->kind == R::NavEject && hr->arg == static_cast<int>(i);
      if (ehot) box(er.x, er.y, er.w, er.h, 3, col_.hot_top, col_.hot_bot, &col_.hot_border);
      glyph(G_EJECT, er.x + 3, er.y + 2, 12, ehot ? col_.accent : col_.text_dim);
      add_region(er, R::NavEject, static_cast<int>(i));
    }
    add_region(rr, R::NavItem, static_cast<int>(i));
    y += h;
  }
  nav_scroll_ = std::clamp(nav_scroll_, 0.0, std::max(0.0, content_h - r.h));
  cairo_restore(cr_);
  // splitter and edge
  cairo_move_to(cr_, r.x + r.w - 0.5, r.y);
  cairo_line_to(cr_, r.x + r.w - 0.5, r.y + r.h);
  kit::set_source(cr_, col_.chrome_border);
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  add_region({r.x + r.w - 3, r.y, 6, r.h}, R::Splitter);
}

void FmWindow::paint_progress(const Rect& r, double frac, bool warn) {
  frac = std::clamp(frac, 0.0, 1.0);
  box(r.x, r.y, r.w, r.h, r.h > 8 ? 2 : 1, col_.bar_track, col_.bar_track, &col_.bar_border);
  const int fw = static_cast<int>((r.w - 2) * frac);
  if (fw > 0) box(r.x + 1, r.y + 1, fw, r.h - 2, r.h > 8 ? 1 : 0, warn ? col_.bar_warn_top : col_.bar_top, warn ? col_.bar_warn_bot : col_.bar_bot, nullptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// content
// ---------------------------------------------------------------------------------------------------------------------

std::vector<ColumnSetting> FmWindow::active_columns() const {
  const Browser& b = tab();
  std::vector<ColumnSetting> cols;
  if (b.place == PlaceKind::Trash) return {{"name", 260, true}, {"origin", 280, true}, {"modified", 150, true}, {"size", 80, true}};
  if (b.place == PlaceKind::Recent) return {{"name", 260, true}, {"folder", 300, true}, {"modified", 150, true}, {"size", 80, true}};
  for (const ColumnSetting& c : s_.columns)
    if (c.visible) cols.push_back(c);
  if (cols.empty()) cols.push_back({"name", 260, true});
  if (!b.search.empty()) {
    bool has = false;
    for (const auto& c : cols) has = has || c.id == "path";
    if (!has) cols.push_back({"path", 220, true});
  } else {
    cols.erase(std::remove_if(cols.begin(), cols.end(), [](const ColumnSetting& c) { return c.id == "path"; }), cols.end());
  }
  return cols;
}

ViewMetrics FmWindow::metrics() const {
  const Browser& b = tab();
  int details_w = 0;
  if (b.mode == ViewMode::Details)
    for (const ColumnSetting& c : active_columns()) details_w += c.width;
  return compute_metrics(b.mode, static_cast<int>(b.shown.size()), std::max(1, lay_.content.w - 14), std::max(1, lay_.content.h), icon_px(), row_h(), font_px(), style_->header_row ? style_->header_h : 0, details_w, &b.group_starts);
}

std::string FmWindow::cell_text(const std::string& col, int i) const {
  const Browser& b = tab();
  const Entry& e = b.shown[i];
  const bool dir = b.raw.is_dir(e);
  if (col == "name") return s_.show_extensions || dir ? b.label_at(i) : [&] {
    std::string n = b.label_at(i);
    const std::string ext = extension_of(n);
    return ext.empty() ? n : n.substr(0, n.size() - ext.size() - 1);
  }();
  if (col == "modified") return e.mtime ? format_date(static_cast<time_t>(e.mtime), s_.date_style) : "";
  if (col == "type") return type_description(b.label_at(i), dir);
  if (col == "size") {
    if (dir) return "";
    switch (s_.size_format) {
      case SizeFormat::Exact: return format_size_exact(e.size);
      case SizeFormat::Auto: return format_size(e.size);
      default: return format_size_kb(e.size);
    }
  }
  if (col == "permissions") return mode_string(e.mode);
  if (col == "path" || col == "folder") {
    const std::string f = b.folder_at(i);
    return f.empty() ? b.path : (f[0] == '/' ? f : b.path + (b.path == "/" ? "" : "/") + f);
  }
  if (col == "origin") {
    for (const TrashItem& it : trash_items_)
      if (it.name == b.name_at(i)) return fs::path(it.original_path).parent_path().string();
  }
  return "";
}

void FmWindow::paint_empty_message(const Rect& area, const std::string& msg) {
  const double w = text_w(msg, font_px() + 1);
  text(msg, area.x + (area.w - w) / 2, area.y + 60, font_px() + 1, col_.text_dim);
}

void FmWindow::paint_content() {
  const Rect r = lay_.content;
  Browser& b = tab();
  cairo_save(cr_);
  use_clip(r);
  box(r.x, r.y, r.w, r.h, 0, col_.content, col_.content, nullptr);
  add_region(r, R::Content);
  if (b.place == PlaceKind::Computer) {
    paint_computer_view();
    cairo_restore(cr_);
    return;
  }
  if (b.place == PlaceKind::Network) {
    paint_network_view();
    cairo_restore(cr_);
    return;
  }
  if (!b.error.empty()) {
    paint_empty_message(r, b.error == "Permission denied" ? "Access is denied." : "This folder can't be opened: " + b.error);
    cairo_restore(cr_);
    return;
  }
  if (b.shown.empty()) {
    if (b.loading) paint_empty_message(r, b.search.empty() ? "" : "Searching...");
    else if (!b.search.empty() || !b.filter.empty()) paint_empty_message(r, "No items match your search.");
    else paint_empty_message(r, b.place == PlaceKind::Trash ? "The Trash is empty." : "This folder is empty.");
    if (style_->header_row && b.mode == ViewMode::Details) {
      const ViewMetrics m = metrics();
      paint_column_headers(m);
    }
    cairo_restore(cr_);
    return;
  }
  const ViewMetrics m = metrics();
  b.scroll_y = std::clamp(b.scroll_y, 0, max_scroll_y(m));
  b.scroll_x = std::clamp(b.scroll_x, 0, max_scroll_x(m));
  cairo_translate(cr_, r.x, r.y);
  // everything below is relative to the content area
  int first, last;
  visible_range(m, b.scroll_x, b.scroll_y, &first, &last);
  b.ensure_stat_range(first, last);
  cairo_save(cr_);
  cairo_rectangle(cr_, 0, m.header_h, r.w, r.h - m.header_h);
  cairo_clip(cr_);
  const Rect saved = lay_.content;
  hover_item_ = -1;
  if (mx_ >= r.x && my_ >= r.y && mx_ < r.x + r.w && my_ < r.y + r.h && !dialog_open() && !menu_open()) hover_item_ = index_at(m, static_cast<int>(mx_ - r.x), static_cast<int>(my_ - r.y), b.scroll_x, b.scroll_y);
  for (size_t g = 0; g < m.group_starts.size() && (m.mode == ViewMode::Details || m.grid_grouped()); ++g) {
    if (m.group_starts[g] < first - 1 || m.group_starts[g] > last + 1) {
      // A heading above its first item can be on screen with no item of its group in the range (icon views): test the strip itself.
      if (!m.grid_grouped()) continue;
      const ItemRect probe = group_header_rect(m, static_cast<int>(g), b.scroll_x, b.scroll_y);
      if (probe.y + probe.h < m.header_h || probe.y > r.h) continue;
    }
    const ItemRect hr = group_header_rect(m, static_cast<int>(g), b.scroll_x, b.scroll_y);
    const std::string lab = b.group_labels[g] + " (" + std::to_string((g + 1 < m.group_starts.size() ? m.group_starts[g + 1] : m.count) - m.group_starts[g]) + ")";
    text(lab, 8, hr.y + hr.h / 2.0, font_px() + 1, col_.accent, true);
    cairo_move_to(cr_, 8 + text_w(lab, font_px() + 1, true) + 10, hr.y + hr.h / 2.0 + 0.5);
    cairo_line_to(cr_, std::max(m.grid_grouped() ? m.view_w : m.cell_w, lay_.content.w) - 16, hr.y + hr.h / 2.0 + 0.5);
    kit::set_source(cr_, col_.head_sep);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
  }
  for (int i = first; i <= last; ++i) {
    const ItemRect ir = item_rect(m, i, b.scroll_x, b.scroll_y);
    paint_item(m, i, ir);
    // item regions are in window coordinates
    Rect wr{r.x + ir.x, r.y + ir.y, ir.w, ir.h};
    (void)wr;
  }
  (void)saved;
  cairo_restore(cr_);
  if (m.mode == ViewMode::Details && m.header_h) paint_column_headers(m);
  if (band_visible_) {
    cairo_rectangle(cr_, band_rect_.x - r.x + 0.5, band_rect_.y - r.y + 0.5, band_rect_.w, band_rect_.h);
    Color f = col_.accent;
    f.a = 0.2;
    kit::set_source(cr_, f);
    cairo_fill_preserve(cr_);
    kit::set_source(cr_, col_.sel_border);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
  }
  cairo_translate(cr_, -r.x, -r.y);
  paint_scrollbars(m);
  cairo_restore(cr_);
}

void FmWindow::paint_column_headers(const ViewMetrics& m) {
  const Browser& b = tab();
  const int hh = m.header_h;
  box(0, 0, lay_.content.w, hh, 0, col_.head_top, col_.head_bot, nullptr);
  cairo_move_to(cr_, 0, hh - 0.5);
  cairo_line_to(cr_, lay_.content.w, hh - 0.5);
  kit::set_source(cr_, col_.head_sep);
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  double x = -b.scroll_x;
  const std::vector<ColumnSetting> cols = active_columns();
  static const struct {
    const char* id;
    const char* label;
  } names[] = {{"name", "Name"}, {"modified", "Date modified"}, {"type", "Type"}, {"size", "Size"}, {"permissions", "Permissions"}, {"path", "In folder"}, {"folder", "Folder"}, {"origin", "Original location"}};
  const Region* hr = region_at(mx_, my_);
  for (size_t i = 0; i < cols.size(); ++i) {
    const ColumnSetting& c = cols[i];
    const char* label = c.id.c_str();
    for (const auto& n : names)
      if (c.id == n.id) label = n.label;
    const Rect cell{static_cast<int>(lay_.content.x + x), lay_.content.y, c.width, hh};
    const bool hot = hr && hr->kind == R::Head && hr->arg == static_cast<int>(i);
    if (hot) box(x, 0, c.width, hh - 1, 0, col_.hot_top, col_.hot_bot, nullptr);
    const bool sorted = (c.id == "name" && b.sort_key == SortKey::Name) || ((c.id == "modified") && b.sort_key == SortKey::Modified) || (c.id == "type" && b.sort_key == SortKey::Type) ||
                        (c.id == "size" && b.sort_key == SortKey::Size);
    const bool right = c.id == "size";
    const double tw = text_w(label, font_px());
    const double tx = right ? x + c.width - tw - 14 : x + 6;
    text(fit(label, c.width - 22, font_px()), tx, hh / 2.0, font_px(), col_.text_dim);
    if (sorted) glyph(b.ascending ? G_SORT_UP : G_SORT_DOWN, right ? x + c.width - 14 : x + std::min<double>(c.width - 14, 10 + tw), (hh - 8) / 2.0, 8, col_.text_dim);
    x += c.width;
    cairo_move_to(cr_, x - 0.5, 3);
    cairo_line_to(cr_, x - 0.5, hh - 3);
    kit::set_source(cr_, col_.head_sep);
    cairo_stroke(cr_);
    add_region(cell, R::Head, static_cast<int>(i));
    add_region({cell.x + cell.w - 3, cell.y, 6, cell.h}, R::HeadSep, static_cast<int>(i));
  }
}

void FmWindow::paint_item(const ViewMetrics& m, int i, const ItemRect& r) {
  Browser& b = tab();
  const Entry& e = b.shown[i];
  const bool selected = b.sel[i];
  const bool hot = i == hover_item_;
  const bool focus = i == b.focus && focused_ && !b.sel.empty();
  const bool dir = b.raw.is_dir(e);
  const std::string label = cell_text("name", i);
  const std::string base = b.label_at(i);
  IconKind ik = dir ? icon_for_folder_name(base) : icon_for_file(base, false);
  if (dir && b.place != PlaceKind::Trash && ik == IconKind::Folder) ik = IconKind::Folder;
  const bool renaming_this = renaming_ && rename_index_ == i;
  const bool drop_here = dir && !drop_hot_.empty() && b.place == PlaceKind::Local && b.path_at(i) == drop_hot_;

  auto paint_back = [&](double x, double y, double w, double h) {
    if (!selected && !hot) return;
    Color t = selected ? (focused_ ? col_.sel_top : col_.selx_top) : col_.hot_top;
    Color bt = selected ? (focused_ ? col_.sel_bot : col_.selx_bot) : col_.hot_bot;
    Color bd = selected ? (focused_ ? col_.sel_border : col_.selx_border) : col_.hot_border;
    if (selected && hot && focused_) {
      t = {t.r * 0.94, t.g * 0.96, t.b * 0.98, 1};
    }
    switch (style_->selection) {
      case SelectionPaint::Win7Glass:
        box(x, y, w, h, 3, t, bt, &bd);
        if (selected) {
          cairo_save(cr_);
          kit::rounded_rect(cr_, x + 1.5, y + 1.5, w - 3, h - 3, 2);
          cairo_set_source_rgba(cr_, 1, 1, 1, 0.55);
          cairo_set_line_width(cr_, 1);
          cairo_stroke(cr_);
          cairo_restore(cr_);
        }
        break;
      case SelectionPaint::Flat: box(x, y, w, h, 0, t, t, selected ? &bd : nullptr); break;
      case SelectionPaint::RoundedAccent: box(x, y, w, h, style_->corner, t, t, nullptr); break;
      case SelectionPaint::Outline: box(x, y, w, h, 0, t, t, &bd); break;
    }
  };
  const Color txt = selected && focused_ && style_->selection != SelectionPaint::Win7Glass && col_.sel_text.r > 0.9 ? col_.sel_text : col_.text;
  const Color txt2 = selected && focused_ && col_.sel_text.r > 0.9 ? col_.sel_text : col_.text_dim;
  const double px = font_px();

  if (drop_here) box(r.x + 1, r.y, r.w - 2, r.h, 3, col_.hot_top, col_.hot_bot, &col_.accent, 2);
  if (m.mode == ViewMode::Details) {
    const Rect area{0, r.y, std::max(m.cell_w, lay_.content.w), r.h};
    if (s_.row_stripes && i % 2 == 1 && !selected && !hot) box(0, r.y, area.w, r.h, 0, col_.content_alt, col_.content_alt, nullptr);
    paint_back(1, r.y, area.w - 2, r.h);
    double x = -b.scroll_x;
    for (const ColumnSetting& c : active_columns()) {
      cairo_save(cr_);
      cairo_rectangle(cr_, x + 1, r.y, c.width - 2, r.h);
      cairo_clip(cr_);
      if (c.id == "name") {
        double tx = x + 5;
        if (s_.use_checkboxes && (selected || hot)) {
          box(tx, r.y + (r.h - 13) / 2.0, 13, 13, 2, col_.field, col_.field, &col_.field_border);
          if (selected) glyph(G_CHECK, tx + 1, r.y + (r.h - 11) / 2.0, 11, col_.accent);
          tx += 18;
        }
        icon(ik, tx, r.y + (r.h - 16) / 2.0, 16);
        tx += 21;
        if (renaming_this) {
          const double fw = c.width - (tx - x) - 4;
          box(tx - 3, r.y + 1, fw + 3, r.h - 2, 0, col_.field, col_.field, &col_.accent);
          if (rename_edit_.has_selection()) {
            const double s0 = text_w(rename_edit_.text.substr(0, rename_edit_.sel_begin()), px), s1 = text_w(rename_edit_.text.substr(0, rename_edit_.sel_end()), px);
            box(tx + s0, r.y + 3, s1 - s0, r.h - 6, 0, col_.accent, col_.accent, nullptr);
          }
          text(rename_edit_.text, tx, r.y + r.h / 2.0, px, col_.text);
          const double cx = tx + text_w(rename_edit_.text.substr(0, rename_edit_.caret), px);
          cairo_move_to(cr_, std::round(cx) + 0.5, r.y + 3);
          cairo_line_to(cr_, std::round(cx) + 0.5, r.y + r.h - 3);
          kit::set_source(cr_, col_.text);
          cairo_set_line_width(cr_, 1);
          cairo_stroke(cr_);
        } else {
          text(fit(label, c.width - (tx - x) - 6, px), tx, r.y + r.h / 2.0, px, txt);
        }
      } else {
        const std::string t = cell_text(c.id, i);
        const bool right = c.id == "size";
        const std::string f = fit(t, c.width - 12, px);
        const double w = right ? text_w(f, px) : 0;
        text(f, right ? x + c.width - w - 8 : x + 6, r.y + r.h / 2.0, px, c.id == "modified" || c.id == "type" || c.id == "path" || c.id == "permissions" || c.id == "folder" || c.id == "origin" ? txt2 : txt);
      }
      cairo_restore(cr_);
      x += c.width;
    }
    if (focus && !selected) {
      cairo_rectangle(cr_, 1.5, r.y + 0.5, area.w - 3, r.h - 1);
      cairo_set_source_rgba(cr_, col_.text.r, col_.text.g, col_.text.b, 0.45);
      const double dash[] = {1, 2};
      cairo_set_dash(cr_, dash, 2, 0);
      cairo_set_line_width(cr_, 1);
      cairo_stroke(cr_);
      cairo_set_dash(cr_, nullptr, 0, 0);
    }
    return;
  }

  const int ipx = m.mode == ViewMode::List ? 16 : (m.mode == ViewMode::Tiles ? 48 : (m.mode == ViewMode::Content ? 32 : m.icon));
  const bool thumbs = s_.show_thumbnails && ipx >= 32 && !dir && icon_for_file(base, false) == IconKind::Image;
  cairo_surface_t* th = nullptr;
  if (thumbs) {
    th = thumb_for(b.path_at(i), e.mtime, ipx);
    if (!th) request_thumb(b.path_at(i), e.mtime, ipx);
  }
  auto draw_icon = [&](double ix, double iy, int px_) {
    if (th) {
      const int tw = cairo_image_surface_get_width(th), tz = cairo_image_surface_get_height(th);
      const double tx = ix + (px_ - tw) / 2.0, ty = iy + (px_ - tz) / 2.0;
      box(tx - 1, ty - 1, tw + 2, tz + 2, 0, col_.content, col_.content, &col_.field_border);
      cairo_set_source_surface(cr_, th, std::round(tx), std::round(ty));
      cairo_paint(cr_);
    } else {
      icon(ik, ix, iy, px_);
    }
  };

  switch (m.mode) {
    case ViewMode::List: {
      paint_back(r.x + 1, r.y, r.w - 4, r.h);
      icon(ik, r.x + 6, r.y + (r.h - 16) / 2.0, 16);
      text(fit(label, r.w - 40, px), r.x + 28, r.y + r.h / 2.0, px, txt);
      break;
    }
    case ViewMode::Tiles: {
      paint_back(r.x + 2, r.y + 1, r.w - 6, r.h - 2);
      draw_icon(r.x + 8, r.y + (r.h - ipx) / 2.0, ipx);
      const double tx = r.x + 8 + ipx + 8;
      text(fit(label, r.w - (tx - r.x) - 8, px), tx, r.y + r.h / 2.0 - px * 1.15, px, txt);
      text(fit(type_description(base, dir), r.w - (tx - r.x) - 8, px - 1), tx, r.y + r.h / 2.0, px - 1, txt2);
      if (!dir) text(format_size(e.size), tx, r.y + r.h / 2.0 + px * 1.15, px - 1, txt2);
      break;
    }
    case ViewMode::Content: {
      paint_back(r.x + 2, r.y + 1, r.w - 6, r.h - 2);
      draw_icon(r.x + 10, r.y + (r.h - ipx) / 2.0, ipx);
      const double tx = r.x + 10 + ipx + 10;
      text(fit(label, r.w - (tx - r.x) - 12, px), tx, r.y + r.h / 2.0 - px * 1.15, px, txt);
      std::string line2 = type_description(base, dir);
      if (!dir) line2 += "     Size: " + format_size(e.size);
      text(fit(line2, r.w - (tx - r.x) - 12, px - 1), tx, r.y + r.h / 2.0, px - 1, txt2);
      text("Date modified: " + format_date(static_cast<time_t>(e.mtime), s_.date_style), tx, r.y + r.h / 2.0 + px * 1.15, px - 1, txt2);
      break;
    }
    default: {  // icon grids
      paint_back(r.x + 3, r.y + 2, r.w - 6, r.h - 4);
      draw_icon(r.x + (r.w - ipx) / 2.0, r.y + 6, ipx);
      // two lines of label, centred
      std::string l1 = label, l2;
      const double maxw = r.w - 10;
      if (text_w(l1, px) > maxw) {
        size_t cut = l1.size();
        while (cut > 1 && text_w(l1.substr(0, cut), px) > maxw) {
          --cut;
          while (cut > 1 && (static_cast<unsigned char>(l1[cut]) & 0xC0) == 0x80) --cut;
        }
        size_t brk = cut;
        for (size_t k = cut; k > cut * 6 / 10; --k)
          if (l1[k - 1] == ' ' || l1[k - 1] == '-' || l1[k - 1] == '_' || l1[k - 1] == '.') {
            brk = k;
            break;
          }
        l2 = fit(l1.substr(brk), maxw, px);
        l1 = l1.substr(0, brk);
      }
      if (renaming_this) {
        box(r.x + 4, r.y + 6 + ipx + 2, r.w - 8, px * 1.5, 0, col_.field, col_.field, &col_.accent);
        text(fit(rename_edit_.text, r.w - 14, px), r.x + 8, r.y + 6 + ipx + 2 + px * 0.75, px, col_.text);
        const double cx = r.x + 8 + text_w(rename_edit_.text.substr(0, rename_edit_.caret), px);
        cairo_move_to(cr_, std::round(cx) + 0.5, r.y + 6 + ipx + 3);
        cairo_line_to(cr_, std::round(cx) + 0.5, r.y + 6 + ipx + px * 1.5);
        kit::set_source(cr_, col_.text);
        cairo_stroke(cr_);
      } else {
        const double w1 = text_w(l1, px);
        text(l1, r.x + (r.w - w1) / 2, r.y + 6 + ipx + 4 + px * 0.55, px, txt);
        if (!l2.empty()) {
          const double w2 = text_w(l2, px);
          text(l2, r.x + (r.w - w2) / 2, r.y + 6 + ipx + 4 + px * 1.75, px, txt);
        }
      }
      break;
    }
  }
  if (s_.use_checkboxes && (selected || hot) && m.mode != ViewMode::List) {
    box(r.x + 6, r.y + 6, 13, 13, 2, col_.field, col_.field, &col_.field_border);
    if (selected) glyph(G_CHECK, r.x + 7, r.y + 7, 11, col_.accent);
  }
}

void FmWindow::paint_scrollbars(const ViewMetrics& m) {
  const Browser& b = tab();
  const Rect r = lay_.content;
  if (!m.horizontal && max_scroll_y(m) > 0) {
    const Rect track{r.x + r.w - 14, r.y + m.header_h, 14, r.h - m.header_h};
    paint_scroll_thumb(track, m.content_h, r.h - m.header_h, b.scroll_y, true, 0);
  }
  if (m.horizontal && max_scroll_x(m) > 0) {
    const Rect track{r.x, r.y + r.h - 14, r.w, 14};
    paint_scroll_thumb(track, m.content_w, r.w, b.scroll_x, false, 1);
  }
}

void FmWindow::paint_scroll_thumb(const Rect& track, double content, double view, double offset, bool vertical, int arg) {
  box(track.x, track.y, track.w, track.h, 0, col_.scroll_track, col_.scroll_track, nullptr);
  const double len = vertical ? track.h : track.w;
  const double tl = std::max(24.0, len * view / content);
  const double pos = (len - tl) * (offset / std::max(1.0, content - view));
  const Region* hr = region_at(mx_, my_);
  const bool hot = hr && (hr->kind == R::ScrollV || hr->kind == R::ScrollH) && hr->arg == arg;
  Rect th = vertical ? Rect{track.x + 2, static_cast<int>(track.y + pos), track.w - 4, static_cast<int>(tl)} : Rect{static_cast<int>(track.x + pos), track.y + 2, static_cast<int>(tl), track.h - 4};
  box(th.x, th.y, th.w, th.h, 3, hot ? col_.btn_hot_top : col_.scroll_thumb, hot ? col_.btn_hot_bot : col_.scroll_thumb, hot ? &col_.btn_border : nullptr);
  add_region(track, vertical ? R::ScrollV : R::ScrollH, arg, static_cast<int>(pos));
}

// ---------------------------------------------------------------------------------------------------------------------
// This PC and Network
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::paint_computer_view() {
  const Rect r = lay_.content;
  const double px = font_px();
  double y = r.y + 10 - tab().scroll_y;
  struct Group {
    std::string title;
    std::vector<const Volume*> vols;
  };
  const bool w10 = s_.style == ViewStyle::Windows10 || style_->nav != NavKind::Tree;
  Group g1{w10 ? "Devices and drives" : "Hard Disk Drives", {}}, g2{"Devices with Removable Storage", {}}, g3{w10 ? "Network locations" : "Network Location", {}};
  for (const Volume& v : volumes_) {
    if (!v.mounted && !s_.show_unmounted_removable) continue;
    if (v.kind == DriveKind::Network) {
      if (s_.show_network_in_this_pc) g3.vols.push_back(&v);
    } else if (v.kind == DriveKind::Removable || v.kind == DriveKind::Optical) {
      (w10 ? g1 : g2).vols.push_back(&v);
    } else {
      g1.vols.push_back(&v);
    }
  }
  const Region* hr = region_at(mx_, my_);
  const int tile_w = w10 ? 270 : 300, tile_h = 62;
  const int per_row = std::max(1, (r.w - 24) / tile_w);
  auto paint_group = [&](const Group& g) {
    if (g.vols.empty() && !(g.title == g3.title && s_.show_network_in_this_pc && !places_.empty())) return;
    text(g.title + " (" + std::to_string(g.vols.size()) + ")", r.x + 12, y + 8, px + 1, w10 ? col_.accent : col_.nav_head);
    cairo_move_to(cr_, r.x + 12 + text_w(g.title + " (" + std::to_string(g.vols.size()) + ")", px + 1) + 8, y + 9.5);
    cairo_line_to(cr_, r.x + r.w - 12, y + 9.5);
    kit::set_source(cr_, col_.head_sep);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    y += 22;
    int col = 0;
    for (const Volume* v : g.vols) {
      const int idx = static_cast<int>(v - volumes_.data());
      const Rect tr{r.x + 12 + col * tile_w, static_cast<int>(y), tile_w - 6, tile_h};
      const bool hot = hr && hr->kind == R::Details && hr->arg == idx;
      const bool sel = tab().focus == idx && false;
      if (hot || sel) box(tr.x, tr.y, tr.w, tr.h, 3, col_.hot_top, col_.hot_bot, &col_.hot_border);
      IconKind ik = v->kind == DriveKind::Optical ? IconKind::DriveOptical : (v->kind == DriveKind::Removable ? (v->disk.compare(0, 11, "/dev/mmcblk") == 0 ? IconKind::DriveCard : IconKind::DriveUsb)
                                                                                                          : (v->kind == DriveKind::Network ? IconKind::DriveNetwork : IconKind::DriveInternal));
      icon(ik, tr.x + 6, tr.y + 7, 48);
      cairo_save(cr_);
      use_clip(tr);
      text(fit(volume_display_name(*v), tr.w - 70, px), tr.x + 62, tr.y + 14, px, col_.text);
      if (v->mounted && v->total) {
        if (s_.show_free_space_bars) paint_progress({tr.x + 62, tr.y + 25, tr.w - 74, 14}, v->used_fraction(), v->used_fraction() > 0.9);
        text(volume_space_text(*v), tr.x + 62, tr.y + 50, px - 1, col_.text_dim);
      } else if (!v->mounted) {
        text("Not mounted. Double-click to mount.", tr.x + 62, tr.y + 34, px - 1, col_.text_dim);
      } else {
        text(v->fstype, tr.x + 62, tr.y + 34, px - 1, col_.text_dim);
      }
      cairo_restore(cr_);
      add_region(tr, R::Details, idx, 0);
      if (++col >= per_row) {
        col = 0;
        y += tile_h + 4;
      }
    }
    if (col != 0) y += tile_h + 4;
    y += 8;
  };
  paint_group(g1);
  if (!w10) paint_group(g2);
  paint_group(g3);
  if (volumes_.empty()) paint_empty_message(r, "Looking for drives...");
}

void FmWindow::paint_network_view() {
  const Rect r = lay_.content;
  const double px = font_px();
  double y = r.y + 10;
  text("Network locations (" + std::to_string(places_.size()) + ")", r.x + 12, y + 8, px + 1, col_.nav_head);
  y += 26;
  const Region* hr = region_at(mx_, my_);
  const int tile_w = 250, tile_h = 54;
  const int per_row = std::max(1, (r.w - 24) / tile_w);
  int col = 0;
  auto tile = [&](IconKind ik, const std::string& title, const std::string& sub, int idx) {
    const Rect tr{r.x + 12 + col * tile_w, static_cast<int>(y), tile_w - 6, tile_h};
    const bool hot = hr && hr->kind == R::Details && hr->arg == idx;
    if (hot) box(tr.x, tr.y, tr.w, tr.h, 3, col_.hot_top, col_.hot_bot, &col_.hot_border);
    icon(ik, tr.x + 6, tr.y + 3, 48);
    cairo_save(cr_);
    use_clip(tr);
    text(fit(title, tr.w - 70, px), tr.x + 62, tr.y + 18, px, col_.text);
    text(fit(sub, tr.w - 70, px - 1), tr.x + 62, tr.y + 36, px - 1, col_.text_dim);
    cairo_restore(cr_);
    add_region(tr, R::Details, idx, 1);
    if (++col >= per_row) {
      col = 0;
      y += tile_h + 4;
    }
  };
  for (size_t i = 0; i < places_.size(); ++i) tile(IconKind::NetworkServer, places_[i].name.empty() ? places_[i].uri : places_[i].name, places_[i].uri, static_cast<int>(i));
  tile(IconKind::Network, "Connect to server...", "smb://, sftp://, ftp://, davs:// ...", 10000);
  for (size_t i = 0; i < lan_hosts_.size(); ++i) tile(IconKind::Computer, lan_hosts_[i].first, lan_hosts_[i].second, 20000 + static_cast<int>(i));
  tile(IconKind::Cloud, "Add a Nextcloud account...", "WebDAV, with your user name", 10001);
  if (col != 0) y += tile_h + 4;
  y += 14;
  text(lan_scanning_ ? "Looking for computers on the network..." : (lan_hosts_.empty() ? "No computers announced on the network (press F5 to look again)." : "Computers on the network are listed above."), r.x + 12, y - 6, px - 1, col_.text_dim);
  y += 18;
  const Rect info{r.x + 12, static_cast<int>(y), r.w - 24, 70};
  (void)info;
  text("Protocols: SMB (Samba, NAS), SFTP/SSH, FTP and FTPS, WebDAV (Nextcloud, ownCloud), NFS, AFP, MTP phones, iPhone (AFC), cameras.", r.x + 12, y + 10, px - 1, col_.text_dim);
  text("Mounted places also appear under Computer, with an eject button.", r.x + 12, y + 28, px - 1, col_.text_dim);
}

// ---------------------------------------------------------------------------------------------------------------------
// details pane and status bar
// ---------------------------------------------------------------------------------------------------------------------

void FmWindow::paint_pane_and_status() {
  const Browser& b = tab();
  if (lay_.details.h > 0) {
    const Rect r = lay_.details;
    box(r.x, r.y, r.w, r.h, 0, col_.status_top, col_.status_bot, nullptr);
    cairo_move_to(cr_, 0, r.y + 0.5);
    cairo_line_to(cr_, W_, r.y + 0.5);
    kit::set_source(cr_, col_.chrome_border);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    const double px = font_px();
    const int n = b.selected_count();
    if (n == 1) {
      const int i = b.selected()[0];
      tab().ensure_stat(i);
      const Entry& e = b.shown[i];
      const bool dir = b.raw.is_dir(e);
      const std::string name = b.label_at(i);
      IconKind ik = dir ? icon_for_folder_name(name) : icon_for_file(name, false);
      icon(ik, 12, r.y + 7, 48);
      text(fit(name, W_ / 2 - 90, px + 1, true), 70, r.y + 18, px + 1, col_.text, true);
      text(type_description(name, dir), 70, r.y + 36, px, col_.text_dim);
      text(dir ? "Date modified: " + format_date(static_cast<time_t>(e.mtime), s_.date_style) : "Size: " + format_size(e.size), 70, r.y + 52, px, col_.text_dim);
      if (!dir) text("Date modified: " + format_date(static_cast<time_t>(e.mtime), s_.date_style), W_ / 2, r.y + 52, px, col_.text_dim);
    } else if (n > 1) {
      icon(IconKind::File, 12, r.y + 7, 48);
      text(std::to_string(n) + " items selected", 70, r.y + 28, px + 1, col_.text, true);
      if (b.selected_bytes()) text("Total size: " + format_size(b.selected_bytes()), 70, r.y + 46, px, col_.text_dim);
    } else {
      icon(b.place == PlaceKind::Computer ? IconKind::Computer : IconKind::Folder, 12, r.y + 7, 48);
      const auto c = crumbs();
      text(fit(c.empty() ? "" : c.back().first, W_ / 2, px + 1, true), 70, r.y + 28, px + 1, col_.text, true);
      text(std::to_string(b.shown.size()) + (b.shown.size() == 1 ? " item" : " items"), 70, r.y + 46, px, col_.text_dim);
      if (b.loading) text("Loading...", 200, r.y + 46, px, col_.text_dim);
    }
    // free space of the volume, on the right
    if (const Volume* v = volume_for_path(b.path); v && v->total && (b.place == PlaceKind::Local)) {
      const std::string sp = volume_space_text(*v);
      const double w = text_w(sp, px);
      text(sp, W_ - w - 14, r.y + 52, px, col_.text_dim);
      if (s_.show_free_space_bars) paint_progress({W_ - 190, r.y + 20, 176, 12}, v->used_fraction(), v->used_fraction() > 0.9);
    }
  }
  if (lay_.status.h > 0) {
    const Rect r = lay_.status;
    box(r.x, r.y, r.w, r.h, 0, col_.status_top, col_.status_bot, nullptr);
    cairo_move_to(cr_, 0, r.y + 0.5);
    cairo_line_to(cr_, W_, r.y + 0.5);
    kit::set_source(cr_, col_.chrome_border);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    text(describe_selection(), 10, r.y + r.h / 2.0, font_px(), col_.text_dim);
    if (jobs_running() > 0) {
      const std::string t = std::to_string(jobs_running()) + (jobs_running() == 1 ? " transfer running" : " transfers running");
      text(t, W_ / 2 - text_w(t, font_px()) / 2, r.y + r.h / 2.0, font_px(), col_.accent);
    }
    // view buttons at the right edge of the status bar
    static const struct {
      int g;
      ViewMode m;
    } vb[] = {{G_DETAILS, ViewMode::Details}, {G_GRID, ViewMode::MediumIcons}};
    double x = W_ - 8;
    for (const auto& v : vb) {
      x -= 24;
      const bool on = tab().mode == v.m;
      const Rect br{static_cast<int>(x), r.y + 2, 22, r.h - 4};
      if (on) box(br.x, br.y, br.w, br.h, 3, col_.btn_down_top, col_.btn_down_bot, &col_.btn_border);
      glyph(v.g, br.x + 4, br.y + (br.h - 14) / 2.0, 14, col_.text_dim);
      add_region(br, R::Tool, static_cast<int>(Cmd::SetView), static_cast<int>(v.m));
    }
  }
}

}  // namespace fleetwm::fm

namespace fleetwm::fm {

// The preview pane: a picture at the size of the pane, the first lines of a text file, or the file's icon and facts.
void FmWindow::paint_preview() {
  const Rect r = lay_.preview;
  if (r.w <= 0) return;
  Browser& b = tab();
  cairo_save(cr_);
  use_clip(r);
  box(r.x, r.y, r.w, r.h, 0, col_.nav_bg, col_.nav_bg, nullptr);
  cairo_move_to(cr_, r.x + 0.5, r.y);
  cairo_line_to(cr_, r.x + 0.5, r.y + r.h);
  kit::set_source(cr_, col_.chrome_border);
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  const double px = font_px();
  if (b.selected_count() != 1 || (b.place != PlaceKind::Local && b.place != PlaceKind::Remote)) {
    const std::string m = b.selected_count() > 1 ? std::to_string(b.selected_count()) + " items selected" : "Select a file to preview it.";
    text(m, r.x + 16, r.y + 30, px, col_.text_dim);
    cairo_restore(cr_);
    return;
  }
  const int i = b.selected()[0];
  b.ensure_stat(i);
  const Entry& e = b.shown[i];
  const bool dir = b.raw.is_dir(e);
  const std::string name = b.label_at(i), path = b.path_at(i);
  const IconKind ik = dir ? icon_for_folder_name(name) : icon_for_file(name, false);
  const int box_px = std::min(r.w - 32, 320);
  double y = r.y + 16;
  cairo_surface_t* th = nullptr;
  if (!dir && ik == IconKind::Image && s_.show_thumbnails) {
    th = thumb_for(path, e.mtime, box_px);
    if (!th) request_thumb(path, e.mtime, box_px);
  }
  if (th) {
    const int tw = cairo_image_surface_get_width(th), tz = cairo_image_surface_get_height(th);
    cairo_set_source_surface(cr_, th, std::round(r.x + (r.w - tw) / 2.0), std::round(y));
    cairo_paint(cr_);
    y += tz + 12;
  } else if (!dir && ik == IconKind::Text) {
    if (preview_path_ != path || preview_mtime_ != e.mtime) {
      preview_path_ = path;
      preview_mtime_ = e.mtime;
      preview_lines_.clear();
      std::ifstream f(path, std::ios::binary);
      std::string chunk(4096, '\0');
      f.read(chunk.data(), 4096);
      chunk.resize(static_cast<size_t>(f.gcount()));
      if (chunk.find('\0') == std::string::npos) {
        std::stringstream ss(chunk);
        std::string line;
        while (preview_lines_.size() < 40 && std::getline(ss, line)) preview_lines_.push_back(line.substr(0, 200));
      } else {
        preview_lines_.push_back("(binary data)");
      }
    }
    box(r.x + 12, y, r.w - 24, std::min<double>(r.h - 150, 40 * (px + 3) + 12), 2, col_.field, col_.field, &col_.field_border);
    double ty = y + 14;
    for (const std::string& l : preview_lines_) {
      if (ty > r.y + r.h - 150) break;
      text(fit(l, r.w - 40, px - 1), r.x + 20, ty, px - 1, col_.text);
      ty += px + 3;
    }
    y = std::min<double>(ty + 16, r.y + r.h - 140);
  } else {
    icon(ik, r.x + (r.w - 96) / 2.0, y, 96);
    y += 112;
  }
  text(fit(name, r.w - 32, px + 1, true), r.x + 16, y + 8, px + 1, col_.text, true);
  text(type_description(name, dir), r.x + 16, y + 28, px, col_.text_dim);
  if (!dir) text("Size: " + format_size(e.size), r.x + 16, y + 46, px, col_.text_dim);
  text("Modified: " + format_date(static_cast<time_t>(e.mtime), s_.date_style), r.x + 16, y + (dir ? 46 : 64), px, col_.text_dim);
  cairo_restore(cr_);
}

}  // namespace fleetwm::fm
