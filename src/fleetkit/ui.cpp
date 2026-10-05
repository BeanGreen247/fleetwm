#include "ui.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

namespace fleetwm::kit {

namespace {

constexpr double kFont = 14.67;
constexpr uint32_t kBtnLeft = 0x110;
constexpr double kGap = 8;

Color mix(const Color& a, const Color& b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
}

Color with_alpha(Color c, double a) {
  c.a = a;
  return c;
}

size_t prev_len(const std::string& s, size_t pos) {
  size_t i = pos;
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
  return i > 0 ? pos - (i - 1) : 0;
}

size_t next_len(const std::string& s, size_t pos) {
  if (pos >= s.size()) return 0;
  size_t n = 1;
  while (pos + n < s.size() && (static_cast<unsigned char>(s[pos + n]) & 0xC0) == 0x80) ++n;
  return n;
}

bool printable(const std::string& u) {
  return !u.empty() && static_cast<unsigned char>(u[0]) >= 0x20 && u[0] != 0x7f;
}

double now_s() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

std::map<double*, double>& scroll_extents() {
  static std::map<double*, double> m;
  return m;
}

}  // namespace

// ------------------------------------------------------------------ colours --

void hsv_to_rgb(double h, double s, double v, double* r, double* g, double* b) {
  h = std::fmod(h, 360.0);
  if (h < 0) h += 360.0;
  const double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c;
  double rr = 0, gg = 0, bb = 0;
  if (h < 60) { rr = c; gg = x; }
  else if (h < 120) { rr = x; gg = c; }
  else if (h < 180) { gg = c; bb = x; }
  else if (h < 240) { gg = x; bb = c; }
  else if (h < 300) { rr = x; bb = c; }
  else { rr = c; bb = x; }
  *r = rr + m;
  *g = gg + m;
  *b = bb + m;
}

void rgb_to_hsv(double r, double g, double b, double* h, double* s, double* v) {
  const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  *v = mx;
  *s = mx <= 0 ? 0 : d / mx;
  double hh = 0;
  if (d > 0) {
    if (mx == r) hh = 60 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) hh = 60 * ((b - r) / d + 2);
    else hh = 60 * ((r - g) / d + 4);
  }
  if (hh < 0) hh += 360;
  *h = hh;
}

std::string color_to_hex(double r, double g, double b) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x", static_cast<int>(std::lround(std::clamp(r, 0.0, 1.0) * 255)),
                static_cast<int>(std::lround(std::clamp(g, 0.0, 1.0) * 255)),
                static_cast<int>(std::lround(std::clamp(b, 0.0, 1.0) * 255)));
  return buf;
}

// -------------------------------------------------------------------- frame --

void Ui::begin(cairo_t* cr, double w, double h) {
  cr_ = cr;
  w_ = w;
  h_ = h;
  id_ = 0;
  again_ = false;
  last_focusables_ = focusables_;
  focusables_.clear();
  modal_blocked_ = picker_open_ || file_open_;
  in_modal_ = false;
  scroll_stack_.clear();
  ox_ = oy_ = 0;
  clip_ = {0, 0, 1e9, 1e9};
  cx_ = left_;
  cy_ = top_;
  line_h_ = 0;
  want_same_line_ = false;

  // Tab / Shift+Tab focus navigation.
  const std::vector<int>& order = modal_blocked_ ? modal_focusables_ : last_focusables_;
  for (size_t i = 0; i < keys_.size();) {
    const KeyEvent& k = keys_[i];
    const bool tab = k.sym == XKB_KEY_Tab || k.sym == XKB_KEY_ISO_Left_Tab;
    if (tab && k.pressed) {
      if (!order.empty()) {
        auto it = std::find(order.begin(), order.end(), focus_id_);
        const bool back = (k.mods & kShift) || k.sym == XKB_KEY_ISO_Left_Tab;
        size_t idx = it == order.end() ? (back ? order.size() - 1 : 0)
                                       : (back ? (it - order.begin() + order.size() - 1) % order.size()
                                               : (it - order.begin() + 1) % order.size());
        focus_id_ = order[idx];
        edit_id_ = 0;
      }
      keys_.erase(keys_.begin() + static_cast<long>(i));
    } else {
      ++i;
    }
  }

  cairo_save(cr_);
  set_source(cr_, pal_.bg_primary);
  cairo_paint(cr_);
}

void Ui::end() {
  if (picker_open_) {
    modal_focusables_.clear();
    in_modal_ = true;
    const size_t first = focusables_.size();
    draw_color_picker();
    modal_focusables_.assign(focusables_.begin() + static_cast<long>(first), focusables_.end());
    in_modal_ = false;
  } else if (file_open_) {
    modal_focusables_.clear();
    in_modal_ = true;
    const size_t first = focusables_.size();
    draw_file_dialog();
    modal_focusables_.assign(focusables_.begin() + static_cast<long>(first), focusables_.end());
    in_modal_ = false;
  }
  cairo_restore(cr_);
  // Unclaimed events are dropped so they cannot fire a widget a frame later.
  press_pending_ = false;
  release_pending_ = false;
  wheel_ = 0;
  keys_.clear();
  pending_paste_.clear();
  if (!down_) active_id_ = 0;
  text_focus_ = edit_id_ != 0 && focus_id_ == edit_id_;
}

// -------------------------------------------------------------------- input --

void Ui::pointer_motion(double x, double y) {
  mx_ = x;
  my_ = y;
  dirty_ = true;
}

void Ui::pointer_button(double x, double y, uint32_t button, bool pressed) {
  if (button != kBtnLeft) return;
  mx_ = x;
  my_ = y;
  down_ = pressed;
  if (pressed) {
    press_pending_ = true;
    press_pos_ = {x, y};
  } else {
    release_pending_ = true;
    release_pos_ = {x, y};
  }
  dirty_ = true;
}

void Ui::pointer_leave() {
  mx_ = my_ = -1;
  dirty_ = true;
}

void Ui::scroll(double dy) {
  wheel_ += dy;
  dirty_ = true;
}

void Ui::key(const KeyEvent& ev) {
  if (ev.pressed) keys_.push_back(ev);
  dirty_ = true;
}

void Ui::paste(const std::string& text) {
  pending_paste_ = text;
  dirty_ = true;
}

// ------------------------------------------------------------------- layout --

bool Ui::visible(const UiRect& r) const {
  const double sy = r.y + oy_;
  return sy + r.h >= clip_.y && sy <= clip_.y + clip_.h;
}

bool Ui::hovered(const UiRect& r) const {
  return input_ok() && clip_.hit(mx_, my_) && r.hit(mx_ - ox_, my_ - oy_);
}

UiRect Ui::place(double w, double h) {
  if (want_same_line_) want_same_line_ = false;
  UiRect r{cx_, cy_ + (row_h_ - h) / 2.0, w, h};
  cx_ += w + kGap;
  line_h_ = std::max(line_h_, row_h_);
  return r;
}

void Ui::newline(double extra) {
  cy_ += (line_h_ > 0 ? line_h_ : row_h_) + 4 + extra;
  cx_ = left_;
  line_h_ = 0;
}

void Ui::space(double h) {
  if (line_h_ > 0 || cx_ > left_) newline();
  cy_ += h;
}

void Ui::separator() {
  if (line_h_ > 0 || cx_ > left_) newline();
  cy_ += 2;
  if (visible({0, cy_, 1, 1})) {
    cairo_rectangle(cr_, left_, cy_, w_ - left_ - right_, 1);
    set_source(cr_, with_alpha(pal_.fg_secondary, 0.35));
    cairo_fill(cr_);
  }
  cy_ += 8;
}

void Ui::row(const std::string& text) {
  if (line_h_ > 0 || cx_ > left_) newline();
  control_x_ = left_ + label_w_;
  const TextExtents te = measure_text(cr_, text, kFont);
  if (visible({cx_, cy_, 1, row_h_})) {
    draw_text(cr_, text, cx_, cy_ + (row_h_ - te.height) / 2 + te.ascent, kFont, pal_.fg_primary);
  }
  cx_ = control_x_;
  line_h_ = row_h_;
}

// --------------------------------------------------------------------- text --

void Ui::heading(const std::string& text) {
  if (line_h_ > 0 || cx_ > left_) newline();
  const TextExtents te = measure_text(cr_, text, kFont, true);
  if (visible({cx_, cy_, 1, 24})) {
    draw_text(cr_, text, cx_, cy_ + (24 - te.height) / 2 + te.ascent, kFont, pal_.fg_primary, true);
  }
  cy_ += 28;
}

void Ui::label(const std::string& text, bool secondary) {
  const TextExtents te = measure_text(cr_, text, kFont);
  const UiRect r = place(te.width, row_h_);
  if (visible(r)) {
    draw_text(cr_, text, r.x, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
              secondary ? pal_.fg_secondary : pal_.fg_primary);
  }
}

void Ui::paragraph(const std::string& text, bool secondary) {
  if (line_h_ > 0 || cx_ > left_) newline();
  const double max_w = w_ - left_ - right_;
  const TextExtents lh = measure_text(cr_, "Ag", kFont);
  std::string line;
  size_t pos = 0;
  auto flush = [&]() {
    if (visible({left_, cy_, 1, lh.height})) {
      draw_text(cr_, line, left_, cy_ + lh.ascent, kFont, secondary ? pal_.fg_secondary : pal_.fg_primary);
    }
    cy_ += lh.height + 2;
    line.clear();
  };
  while (pos < text.size()) {
    size_t e = text.find(' ', pos);
    if (e == std::string::npos) e = text.size();
    const std::string word = text.substr(pos, e - pos);
    const std::string trial = line.empty() ? word : line + " " + word;
    if (!line.empty() && measure_text(cr_, trial, kFont).width > max_w) flush();
    line = line.empty() ? word : line + " " + word;
    pos = e + 1;
  }
  if (!line.empty()) flush();
  cy_ += 4;
}

bool Ui::link(const std::string& text) {
  const int id = next_id();
  register_focusable_if(id, true);
  const TextExtents te = measure_text(cr_, text, kFont);
  const UiRect r = place(te.width, row_h_);
  const bool clicked = click_widget(id, r) || key_activate(id);
  if (visible(r)) {
    const double by = r.y + (r.h - te.height) / 2 + te.ascent;
    draw_text(cr_, text, r.x, by, kFont, pal_.accent);
    cairo_rectangle(cr_, r.x, by + 2, te.width, 1);
    set_source(cr_, pal_.accent);
    cairo_fill(cr_);
    if (focused(id)) draw_focus_ring({r.x - 2, r.y, r.w + 4, r.h}, 4);
  }
  return mark(clicked);
}

// ------------------------------------------------------------------ widgets --

void Ui::register_focusable_if(int id, bool enabled) {
  if (enabled) focusables_.push_back(id);
}

void Ui::draw_focus_ring(const UiRect& r, double radius) {
  rounded_rect(cr_, r.x - 1.5, r.y - 1.5, r.w + 3, r.h + 3, radius + 1.5);
  set_source(cr_, with_alpha(pal_.accent, 0.9));
  cairo_set_line_width(cr_, 1.5);
  cairo_stroke(cr_);
}

bool Ui::click_widget(int id, const UiRect& r) {
  bool clicked = false;
  if (press_pending_ && input_ok() && clip_.hit(press_pos_.x, press_pos_.y) &&
      r.hit(press_pos_.x - ox_, press_pos_.y - oy_)) {
    active_id_ = id;
    focus_id_ = id;
    edit_id_ = 0;
    press_pending_ = false;
  }
  if (release_pending_ && active_id_ == id) {
    if (input_ok() && r.hit(release_pos_.x - ox_, release_pos_.y - oy_)) clicked = true;
    release_pending_ = false;
    active_id_ = 0;
  }
  return mark(clicked);
}

bool Ui::key_activate(int id) {
  if (!focused(id) || !input_ok()) return false;
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i].sym == XKB_KEY_space || keys_[i].sym == XKB_KEY_Return || keys_[i].sym == XKB_KEY_KP_Enter) {
      keys_.erase(keys_.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

bool Ui::checkbox(const std::string& text, bool* value, bool enabled) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  const TextExtents te = measure_text(cr_, text, kFont);
  const UiRect r = place(22 + te.width, 24);
  bool changed = false;
  if (enabled && (click_widget(id, r) || key_activate(id))) {
    *value = !*value;
    changed = true;
  }
  if (visible(r)) {
    const UiRect box{r.x, r.y + 4, 16, 16};
    rounded_rect(cr_, box.x, box.y, box.w, box.h, 4);
    if (*value) {
      set_source(cr_, enabled ? pal_.accent : with_alpha(pal_.fg_secondary, 0.5));
      cairo_fill(cr_);
      set_source(cr_, pal_.bg_primary);
      cairo_set_line_width(cr_, 2);
      cairo_move_to(cr_, box.x + 3.5, box.y + 8.5);
      cairo_line_to(cr_, box.x + 6.8, box.y + 11.8);
      cairo_line_to(cr_, box.x + 12.5, box.y + 4.8);
      cairo_stroke(cr_);
    } else {
      set_source(cr_, pal_.bg_secondary);
      cairo_fill_preserve(cr_);
      set_source(cr_, with_alpha(pal_.fg_secondary, enabled ? 0.8 : 0.35));
      cairo_set_line_width(cr_, 1.2);
      cairo_stroke(cr_);
    }
    draw_text(cr_, text, r.x + 22, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
              enabled ? pal_.fg_primary : with_alpha(pal_.fg_secondary, 0.6));
    if (focused(id)) draw_focus_ring(box, 4);
  }
  return mark(changed);
}

bool Ui::radio(const std::string& text, int* value, int index, bool enabled) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  const TextExtents te = measure_text(cr_, text, kFont);
  const UiRect r = place(22 + te.width, 24);
  bool changed = false;
  if (enabled && (click_widget(id, r) || key_activate(id)) && *value != index) {
    *value = index;
    changed = true;
  }
  if (visible(r)) {
    const double cx = r.x + 8, cy = r.y + 12;
    cairo_arc(cr_, cx, cy, 8, 0, 2 * M_PI);
    set_source(cr_, pal_.bg_secondary);
    cairo_fill_preserve(cr_);
    set_source(cr_, with_alpha(pal_.fg_secondary, enabled ? 0.8 : 0.35));
    cairo_set_line_width(cr_, 1.2);
    cairo_stroke(cr_);
    if (*value == index) {
      cairo_arc(cr_, cx, cy, 4.5, 0, 2 * M_PI);
      set_source(cr_, enabled ? pal_.accent : with_alpha(pal_.fg_secondary, 0.5));
      cairo_fill(cr_);
    }
    draw_text(cr_, text, r.x + 22, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
              enabled ? pal_.fg_primary : with_alpha(pal_.fg_secondary, 0.6));
    if (focused(id)) draw_focus_ring({cx - 8, cy - 8, 16, 16}, 8);
  }
  return mark(changed);
}

bool Ui::radio_group(const std::vector<std::string>& options, int* value, bool enabled) {
  bool changed = false;
  for (size_t i = 0; i < options.size(); ++i) {
    const double w = 22 + measure_text(cr_, options[i], kFont).width;
    if (cx_ + w > w_ - right_ && cx_ > control_x_) {
      newline(-2);
      cx_ = control_x_;
    }
    if (radio(options[i], value, static_cast<int>(i), enabled)) changed = true;
    cx_ += 6;
  }
  return mark(changed);
}

bool Ui::button(const std::string& text, bool enabled, bool accent) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  const TextExtents te = measure_text(cr_, text, kFont);
  const UiRect r = place(std::max(64.0, te.width + 28), 28);
  const bool clicked = enabled && (click_widget(id, r) || key_activate(id));
  if (visible(r)) {
    const bool hot = enabled && hovered(r);
    const bool down = enabled && active_id_ == id && down_;
    Color bg = accent ? pal_.accent : pal_.bg_secondary;
    if (hot) bg = mix(bg, accent ? pal_.fg_primary : pal_.accent, accent ? 0.18 : 0.22);
    if (down) bg = mix(bg, pal_.fg_primary, 0.15);
    rounded_rect(cr_, r.x, r.y, r.w, r.h, pal_.rounded ? 6 : 0);
    set_source(cr_, enabled ? bg : with_alpha(bg, 0.5));
    cairo_fill_preserve(cr_);
    set_source(cr_, with_alpha(pal_.fg_secondary, 0.3));
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    draw_text(cr_, text, r.x + (r.w - te.width) / 2, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
              !enabled ? with_alpha(pal_.fg_secondary, 0.6) : accent ? pal_.bg_primary : pal_.fg_primary);
    if (focused(id)) draw_focus_ring(r, pal_.rounded ? 6 : 0);
  }
  return mark(clicked);
}

bool Ui::spin(int* value, int min, int max, int step, bool enabled) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  const UiRect r = place(116, 28);
  const UiRect val{r.x, r.y, 66, r.h}, minus{r.x + 68, r.y, 23, r.h}, plus{r.x + 93, r.y, 23, r.h};
  bool changed = false;
  auto set = [&](int v) {
    v = std::clamp(v, min, max);
    if (v != *value) {
      *value = v;
      changed = true;
    }
  };

  // Clicks: value area starts text editing, -/+ step the value.
  if (enabled && press_pending_ && input_ok() && clip_.hit(press_pos_.x, press_pos_.y)) {
    const double px = press_pos_.x - ox_, py = press_pos_.y - oy_;
    if (val.hit(px, py)) {
      focus_id_ = id;
      edit_id_ = id;
      edit_buf_ = std::to_string(*value);
      edit_cursor_ = edit_buf_.size();
      edit_fresh_ = true;  // like a selected field: the first digit replaces the old value
      press_pending_ = false;
      active_id_ = 0;
    } else if (minus.hit(px, py) || plus.hit(px, py)) {
      focus_id_ = id;
      edit_id_ = 0;
      set(*value + (plus.hit(px, py) ? step : -step));
      press_pending_ = false;
      active_id_ = 0;
    }
  }
  if (enabled && hovered(r) && wheel_ != 0) {
    set(*value + (wheel_ < 0 ? step : -step));
    wheel_ = 0;
  }
  // Keyboard.
  if (enabled && focused(id) && input_ok()) {
    for (size_t i = 0; i < keys_.size();) {
      const KeyEvent& k = keys_[i];
      bool eaten = true;
      if (k.sym == XKB_KEY_Up) set(*value + step);
      else if (k.sym == XKB_KEY_Down) set(*value - step);
      else if (k.sym == XKB_KEY_Page_Up) set(*value + 10 * step);
      else if (k.sym == XKB_KEY_Page_Down) set(*value - 10 * step);
      else if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) {
        if (edit_id_ == id) {
          set(std::atoi(edit_buf_.c_str()));
          edit_id_ = 0;
        }
      } else if (k.sym == XKB_KEY_Escape) {
        edit_id_ = 0;
      } else if (k.sym == XKB_KEY_BackSpace) {
        if (edit_id_ != id) {
          edit_id_ = id;
          edit_buf_ = std::to_string(*value);
        }
        edit_fresh_ = false;
        if (!edit_buf_.empty()) edit_buf_.pop_back();
      } else if (k.utf8.size() == 1 && std::isdigit(static_cast<unsigned char>(k.utf8[0])) &&
                 !(k.mods & (kCtrl | kAlt))) {
        if (edit_id_ != id || edit_fresh_) {
          edit_id_ = id;
          edit_buf_.clear();
          edit_fresh_ = false;
        }
        if (edit_buf_.size() < 6) edit_buf_ += k.utf8;
      } else {
        eaten = false;
      }
      if (eaten) keys_.erase(keys_.begin() + static_cast<long>(i));
      else ++i;
    }
  }
  // Losing focus commits what was typed.
  if (edit_id_ == id && focus_id_ != id) {
    set(std::atoi(edit_buf_.c_str()));
    edit_id_ = 0;
  }
  if (visible(r)) {
    const bool editing = edit_id_ == id;
    rounded_rect(cr_, val.x + 0.5, val.y + 0.5, val.w - 1, val.h - 1, pal_.rounded ? 6 : 0);
    set_source(cr_, pal_.bg_secondary);
    cairo_fill_preserve(cr_);
    set_source(cr_, focused(id) ? pal_.accent : with_alpha(pal_.fg_secondary, 0.3));
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    const std::string txt = editing ? edit_buf_ : std::to_string(*value);
    const TextExtents te = measure_text(cr_, txt, kFont);
    draw_text(cr_, txt, val.x + 8, val.y + (val.h - te.height) / 2 + te.ascent, kFont,
              enabled ? pal_.fg_primary : with_alpha(pal_.fg_secondary, 0.6));
    if (editing) {
      cairo_rectangle(cr_, val.x + 8 + te.width + 1, val.y + 6, 1.2, val.h - 12);
      set_source(cr_, pal_.fg_primary);
      cairo_fill(cr_);
    }
    for (int b = 0; b < 2; ++b) {
      const UiRect& br = b == 0 ? minus : plus;
      const bool hot = enabled && hovered(br);
      rounded_rect(cr_, br.x, br.y, br.w, br.h, pal_.rounded ? 6 : 0);
      set_source(cr_, hot ? mix(pal_.bg_secondary, pal_.accent, 0.3) : pal_.bg_secondary);
      cairo_fill(cr_);
      set_source(cr_, enabled ? pal_.fg_primary : with_alpha(pal_.fg_secondary, 0.5));
      cairo_set_line_width(cr_, 1.6);
      cairo_move_to(cr_, br.x + 6.5, br.y + br.h / 2);
      cairo_line_to(cr_, br.x + br.w - 6.5, br.y + br.h / 2);
      if (b == 1) {
        cairo_move_to(cr_, br.x + br.w / 2, br.y + 8);
        cairo_line_to(cr_, br.x + br.w / 2, br.y + br.h - 8);
      }
      cairo_stroke(cr_);
    }
  }
  return mark(changed);
}

bool Ui::slider(int* value, int min, int max, double width, bool enabled) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  if (width <= 0) width = std::max(100.0, w_ - right_ - cx_ - 4);
  const UiRect r = place(width, 24);
  const double pad = 8, val_w = 36, tx = r.x + pad, tw = r.w - 2 * pad - val_w;
  bool changed = false;
  auto set = [&](int v) {
    v = std::clamp(v, min, max);
    if (v != *value) {
      *value = v;
      changed = true;
    }
  };
  auto from_x = [&](double px) {
    const double f = std::clamp((px - tx) / tw, 0.0, 1.0);
    set(min + static_cast<int>(std::lround(f * (max - min))));
  };
  if (enabled && press_pending_ && input_ok() && clip_.hit(press_pos_.x, press_pos_.y) &&
      r.hit(press_pos_.x - ox_, press_pos_.y - oy_)) {
    active_id_ = id;
    focus_id_ = id;
    press_pending_ = false;
    from_x(press_pos_.x - ox_);
  }
  if (enabled && active_id_ == id) {
    if (release_pending_) {
      from_x(release_pos_.x - ox_);
      release_pending_ = false;
      active_id_ = 0;
    } else if (down_) {
      from_x(mx_ - ox_);
    }
  }
  if (enabled && hovered(r) && wheel_ != 0) {
    set(*value + (wheel_ < 0 ? 5 : -5));
    wheel_ = 0;
  }
  if (enabled && focused(id) && input_ok()) {
    for (size_t i = 0; i < keys_.size();) {
      const KeyEvent& k = keys_[i];
      bool eaten = true;
      if (k.sym == XKB_KEY_Left || k.sym == XKB_KEY_Down) set(*value - 1);
      else if (k.sym == XKB_KEY_Right || k.sym == XKB_KEY_Up) set(*value + 1);
      else if (k.sym == XKB_KEY_Page_Down) set(*value - 10);
      else if (k.sym == XKB_KEY_Page_Up) set(*value + 10);
      else if (k.sym == XKB_KEY_Home) set(min);
      else if (k.sym == XKB_KEY_End) set(max);
      else eaten = false;
      if (eaten) keys_.erase(keys_.begin() + static_cast<long>(i));
      else ++i;
    }
  }
  if (visible(r)) {
    const double f = max > min ? static_cast<double>(*value - min) / (max - min) : 0;
    const double ty = r.y + 12 - 2;
    rounded_rect(cr_, tx, ty, tw, 4, 2);
    set_source(cr_, with_alpha(pal_.fg_secondary, 0.3));
    cairo_fill(cr_);
    if (f > 0) {
      rounded_rect(cr_, tx, ty, tw * f, 4, 2);
      set_source(cr_, enabled ? pal_.accent : with_alpha(pal_.fg_secondary, 0.5));
      cairo_fill(cr_);
    }
    cairo_arc(cr_, tx + tw * f, ty + 2, 7, 0, 2 * M_PI);
    set_source(cr_, pal_.fg_primary);
    cairo_fill_preserve(cr_);
    if (focused(id)) {
      set_source(cr_, pal_.accent);
      cairo_set_line_width(cr_, 2);
      cairo_stroke(cr_);
    } else {
      cairo_new_path(cr_);
    }
    const std::string vtxt = std::to_string(*value);
    const TextExtents vt = measure_text(cr_, vtxt, 13);
    draw_text(cr_, vtxt, r.x + r.w - val_w + (val_w - vt.width) / 2, r.y + (r.h - vt.height) / 2 + vt.ascent, 13,
              enabled ? pal_.fg_secondary : with_alpha(pal_.fg_secondary, 0.5));
  }
  return mark(changed);
}

bool Ui::color_button(std::string* hex, bool enabled) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  const UiRect r = place(48, 24);
  bool changed = false;
  if (picker_done_ && picker_result_target_ == id) {
    *hex = picker_result_;
    picker_done_ = false;
    changed = true;
  }
  if (enabled && (click_widget(id, r) || key_activate(id))) open_picker(*hex, id);
  if (visible(r)) {
    const Color c = parse_color(*hex, {0.5, 0.5, 0.5});
    rounded_rect(cr_, r.x, r.y, r.w, r.h, pal_.rounded ? 6 : 0);
    set_source(cr_, enabled ? c : mix(c, pal_.bg_primary, 0.6));
    cairo_fill_preserve(cr_);
    set_source(cr_, with_alpha(pal_.fg_secondary, hovered(r) && enabled ? 0.9 : 0.4));
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    if (focused(id)) draw_focus_ring(r, pal_.rounded ? 6 : 0);
  }
  return mark(changed);
}

bool Ui::tabs(const std::vector<std::string>& names, int* current) {
  const int id = next_id();
  register_focusable_if(id, true);
  if (line_h_ > 0 || cx_ > left_) newline();
  bool changed = false;
  double x = left_;
  const double h = 32;
  // keyboard: Left/Right switch tabs while focused
  if (focused(id) && input_ok()) {
    for (size_t i = 0; i < keys_.size();) {
      const KeyEvent& k = keys_[i];
      bool eaten = true;
      if (k.sym == XKB_KEY_Left && *current > 0) {
        --*current;
        changed = true;
      } else if (k.sym == XKB_KEY_Right && *current + 1 < static_cast<int>(names.size())) {
        ++*current;
        changed = true;
      } else {
        eaten = false;
      }
      if (eaten) keys_.erase(keys_.begin() + static_cast<long>(i));
      else ++i;
    }
  }
  for (size_t i = 0; i < names.size(); ++i) {
    const TextExtents te = measure_text(cr_, names[i], kFont, static_cast<int>(i) == *current);
    const UiRect r{x, cy_, te.width + 24, h};
    const bool active = static_cast<int>(i) == *current;
    // clicking a tab
    if (press_pending_ && input_ok() && r.hit(press_pos_.x - ox_, press_pos_.y - oy_)) {
      press_pending_ = false;
      focus_id_ = id;
      if (!active) {
        *current = static_cast<int>(i);
        changed = true;
      }
    }
    if (visible(r)) {
      const bool hot = hovered(r);
      if (hot && !active) {
        rounded_rect(cr_, r.x, r.y + 2, r.w, r.h - 4, pal_.rounded ? 6 : 0);
        set_source(cr_, with_alpha(pal_.accent, 0.12));
        cairo_fill(cr_);
      }
      draw_text(cr_, names[i], r.x + 12, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
                active ? pal_.fg_primary : pal_.fg_secondary, active);
      if (active) {
        cairo_rectangle(cr_, r.x + 6, r.y + r.h - 3, r.w - 12, 2.5);
        set_source(cr_, pal_.accent);
        cairo_fill(cr_);
      }
    }
    x += r.w + 2;
  }
  if (visible({left_, cy_ + h, 1, 1})) {
    cairo_rectangle(cr_, left_, cy_ + h, w_ - left_ - right_, 1);
    set_source(cr_, with_alpha(pal_.fg_secondary, 0.35));
    cairo_fill(cr_);
  }
  cy_ += h + 8;
  cx_ = left_;
  line_h_ = 0;
  return mark(changed);
}

bool Ui::text_entry(std::string* text, double width, bool enabled) {
  const int id = next_id();
  register_focusable_if(id, enabled);
  const UiRect r = place(width, 28);
  bool changed = false;
  if (enabled && press_pending_ && input_ok() && clip_.hit(press_pos_.x, press_pos_.y) &&
      r.hit(press_pos_.x - ox_, press_pos_.y - oy_)) {
    focus_id_ = id;
    edit_id_ = id;
    edit_cursor_ = text->size();
    press_pending_ = false;
  }
  if (enabled && focused(id) && input_ok()) {
    if (edit_id_ != id) {
      edit_id_ = id;
      edit_cursor_ = text->size();
    }
    edit_cursor_ = std::min(edit_cursor_, text->size());
    if (!pending_paste_.empty()) {
      std::string p = pending_paste_;
      p.erase(std::remove_if(p.begin(), p.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }), p.end());
      text->insert(edit_cursor_, p);
      edit_cursor_ += p.size();
      pending_paste_.clear();
      changed = true;
    }
    for (size_t i = 0; i < keys_.size();) {
      const KeyEvent& k = keys_[i];
      bool eaten = true;
      if (k.sym == XKB_KEY_BackSpace) {
        const size_t n = prev_len(*text, edit_cursor_);
        if (n) {
          text->erase(edit_cursor_ - n, n);
          edit_cursor_ -= n;
          changed = true;
        }
      } else if (k.sym == XKB_KEY_Delete) {
        const size_t n = next_len(*text, edit_cursor_);
        if (n) {
          text->erase(edit_cursor_, n);
          changed = true;
        }
      } else if (k.sym == XKB_KEY_Left) edit_cursor_ -= prev_len(*text, edit_cursor_);
      else if (k.sym == XKB_KEY_Right) edit_cursor_ += next_len(*text, edit_cursor_);
      else if (k.sym == XKB_KEY_Home) edit_cursor_ = 0;
      else if (k.sym == XKB_KEY_End) edit_cursor_ = text->size();
      else if ((k.mods & kCtrl) && k.sym == XKB_KEY_u) {
        text->erase(0, edit_cursor_);
        edit_cursor_ = 0;
        changed = true;
      } else if (!(k.mods & (kCtrl | kAlt | kSuper)) && printable(k.utf8)) {
        text->insert(edit_cursor_, k.utf8);
        edit_cursor_ += k.utf8.size();
        changed = true;
      } else {
        eaten = false;
      }
      if (eaten) keys_.erase(keys_.begin() + static_cast<long>(i));
      else ++i;
    }
  }
  if (visible(r)) {
    rounded_rect(cr_, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, pal_.rounded ? 6 : 0);
    set_source(cr_, pal_.bg_secondary);
    cairo_fill_preserve(cr_);
    set_source(cr_, focused(id) ? pal_.accent : with_alpha(pal_.fg_secondary, 0.3));
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    cairo_save(cr_);
    cairo_rectangle(cr_, r.x + 4, r.y, r.w - 8, r.h);
    cairo_clip(cr_);
    const TextExtents te = measure_text(cr_, *text, kFont);
    const double caret = measure_text(cr_, text->substr(0, std::min(edit_cursor_, text->size())), kFont).width;
    const double shift = std::max(0.0, caret - (r.w - 20));
    draw_text(cr_, *text, r.x + 8 - shift, r.y + (r.h - te.height) / 2 + te.ascent, kFont,
              enabled ? pal_.fg_primary : with_alpha(pal_.fg_secondary, 0.6));
    if (focused(id) && edit_id_ == id) {
      cairo_rectangle(cr_, r.x + 8 + caret - shift, r.y + 6, 1.2, r.h - 12);
      set_source(cr_, pal_.fg_primary);
      cairo_fill(cr_);
    }
    cairo_restore(cr_);
  }
  return mark(changed);
}

// ------------------------------------------------------------------- scroll --

void Ui::begin_scroll(UiRect view, double* offset) {
  if (line_h_ > 0 || cx_ > left_) newline();
  auto& ext = scroll_extents();
  const double content = ext.count(offset) ? ext[offset] : 0;
  const double max_off = std::max(0.0, content - view.h);
  const UiRect screen{ox_ + view.x, oy_ + view.y, view.w, view.h};  // pointer space
  if (wheel_ != 0 && input_ok() && screen.hit(mx_, my_)) {
    *offset += wheel_ * 3.0;
    wheel_ = 0;
  }
  *offset = std::clamp(*offset, 0.0, max_off);

  scroll_stack_.push_back({view, offset, cy_, ox_, oy_, w_, left_, cx_, clip_});
  cairo_save(cr_);
  cairo_rectangle(cr_, view.x, view.y, view.w, view.h);
  cairo_clip(cr_);
  cairo_translate(cr_, view.x, view.y - *offset);
  clip_ = screen;
  ox_ += view.x;
  oy_ += view.y - *offset;
  w_ = view.w;
  cx_ = left_;
  cy_ = 4;
  line_h_ = 0;
}

void Ui::end_scroll() {
  if (scroll_stack_.empty()) return;
  if (line_h_ > 0 || cx_ > left_) newline();
  ScrollFrame f = scroll_stack_.back();
  scroll_stack_.pop_back();
  const double content = cy_ + 4;
  scroll_extents()[f.offset] = content;
  cairo_restore(cr_);
  ox_ = f.ox;
  oy_ = f.oy;
  w_ = f.saved_w;
  left_ = f.saved_left;
  clip_ = f.saved_clip;
  cx_ = f.saved_cx;
  cy_ = f.start_cy + f.view.h;
  line_h_ = 0;
  if (content > f.view.h) {  // scrollbar
    const double track = f.view.h, th = std::max(24.0, track * f.view.h / content);
    const double max_off = content - f.view.h;
    const double ty = f.view.y + (track - th) * (max_off > 0 ? *f.offset / max_off : 0);
    rounded_rect(cr_, f.view.x + f.view.w - 6, ty, 3.5, th, 1.75);
    set_source(cr_, with_alpha(pal_.fg_secondary, 0.55));
    cairo_fill(cr_);
  }
}

// ------------------------------------------------------------------- modals --

void Ui::open_picker(const std::string& hex, int target) {
  const Color c = parse_color(hex, {0.5, 0.5, 0.5});
  rgb_to_hsv(c.r, c.g, c.b, &ph_, &ps_, &pv_);
  picker_open_ = true;
  picker_target_ = target;
  picker_hex_ = color_to_hex(c.r, c.g, c.b);
  picker_entry_ = picker_hex_;
  picker_drag_ = 0;
  sv_hue_ = -1;
  press_pending_ = release_pending_ = false;
  focus_id_ = 0;
  dirty_ = again_ = true;
}

void Ui::picker_rebuild_sv() {
  const int w = 200, h = 140;
  if (!sv_surface_) sv_surface_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
  cairo_surface_flush(sv_surface_);
  unsigned char* data = cairo_image_surface_get_data(sv_surface_);
  const int stride = cairo_image_surface_get_stride(sv_surface_);
  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(data + static_cast<size_t>(y) * stride);
    for (int x = 0; x < w; ++x) {
      double r, g, b;
      hsv_to_rgb(ph_, static_cast<double>(x) / (w - 1), 1.0 - static_cast<double>(y) / (h - 1), &r, &g, &b);
      row[x] = (static_cast<uint32_t>(r * 255 + 0.5) << 16) | (static_cast<uint32_t>(g * 255 + 0.5) << 8) |
               static_cast<uint32_t>(b * 255 + 0.5);
    }
  }
  cairo_surface_mark_dirty(sv_surface_);
  sv_hue_ = ph_;
}

void Ui::draw_color_picker() {
  // Dim everything behind the dialog and swallow pointer input outside it.
  cairo_rectangle(cr_, 0, 0, w_, h_);
  set_source(cr_, {0, 0, 0, 0.5});
  cairo_fill(cr_);
  const double pw = 332, ph = 336;
  const double px = std::max(0.0, (w_ - pw) / 2), py = std::max(0.0, (h_ - ph) / 2);
  rounded_rect(cr_, px, py, pw, ph, pal_.rounded ? 10 : 0);
  set_source(cr_, pal_.bg_secondary);
  cairo_fill_preserve(cr_);
  set_source(cr_, with_alpha(pal_.fg_secondary, 0.4));
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  draw_text(cr_, "Choose a color", px + 16, py + 28, kFont, pal_.fg_primary, true);

  const UiRect sv{px + 16, py + 42, 200, 140}, hue{px + 226, py + 42, 20, 140};
  if (sv_hue_ != ph_ || !sv_surface_) picker_rebuild_sv();
  cairo_save(cr_);
  rounded_rect(cr_, sv.x, sv.y, sv.w, sv.h, 4);
  cairo_clip(cr_);
  cairo_set_source_surface(cr_, sv_surface_, sv.x, sv.y);
  cairo_paint(cr_);
  cairo_restore(cr_);
  // hue bar
  cairo_pattern_t* grad = cairo_pattern_create_linear(0, hue.y, 0, hue.y + hue.h);
  for (int i = 0; i <= 6; ++i) {
    double r, g, b;
    hsv_to_rgb(i * 60.0, 1, 1, &r, &g, &b);
    cairo_pattern_add_color_stop_rgb(grad, i / 6.0, r, g, b);
  }
  rounded_rect(cr_, hue.x, hue.y, hue.w, hue.h, 4);
  cairo_set_source(cr_, grad);
  cairo_fill(cr_);
  cairo_pattern_destroy(grad);
  // markers
  const double mx = sv.x + ps_ * (sv.w - 1), my = sv.y + (1 - pv_) * (sv.h - 1);
  cairo_arc(cr_, mx, my, 5, 0, 2 * M_PI);
  set_source(cr_, {1, 1, 1, 1});
  cairo_set_line_width(cr_, 2);
  cairo_stroke(cr_);
  cairo_arc(cr_, mx, my, 6.5, 0, 2 * M_PI);
  set_source(cr_, {0, 0, 0, 0.6});
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  const double hy = hue.y + ph_ / 360.0 * (hue.h - 1);
  cairo_rectangle(cr_, hue.x - 2, hy - 2, hue.w + 4, 4);
  set_source(cr_, {1, 1, 1, 1});
  cairo_set_line_width(cr_, 1.5);
  cairo_stroke(cr_);

  // dragging
  if (press_pending_ && sv.hit(press_pos_.x, press_pos_.y)) {
    picker_drag_ = 1;
    press_pending_ = false;
  } else if (press_pending_ && UiRect{hue.x - 4, hue.y - 4, hue.w + 8, hue.h + 8}.hit(press_pos_.x, press_pos_.y)) {
    picker_drag_ = 2;
    press_pending_ = false;
  }
  if (picker_drag_ && !down_ && !release_pending_) picker_drag_ = 0;
  if (picker_drag_) {
    double x = mx_, y = my_;
    if (release_pending_) {
      x = release_pos_.x;
      y = release_pos_.y;
      release_pending_ = false;
    }
    if (picker_drag_ == 1) {
      ps_ = std::clamp((x - sv.x) / (sv.w - 1), 0.0, 1.0);
      pv_ = 1 - std::clamp((y - sv.y) / (sv.h - 1), 0.0, 1.0);
    } else {
      ph_ = std::clamp((y - hue.y) / (hue.h - 1), 0.0, 1.0) * 360.0;
    }
    double r, g, b;
    hsv_to_rgb(ph_, ps_, pv_, &r, &g, &b);
    picker_hex_ = color_to_hex(r, g, b);
    picker_entry_ = picker_hex_;
    if (release_pending_ == false && !down_) picker_drag_ = 0;
  }

  // preview + presets
  double r, g, b;
  hsv_to_rgb(ph_, ps_, pv_, &r, &g, &b);
  rounded_rect(cr_, px + 258, py + 42, 58, 58, 6);
  cairo_set_source_rgb(cr_, r, g, b);
  cairo_fill(cr_);
  static const char* kPresets[] = {"#e53935", "#fb8c00", "#fdd835", "#43a047", "#00acc1",
                                   "#1e88e5", "#8e24aa", "#ec407a", "#ffffff", "#000000"};
  for (int i = 0; i < 10; ++i) {
    const UiRect sw{px + 16 + i * 30.0, py + 194, 24, 24};
    rounded_rect(cr_, sw.x, sw.y, sw.w, sw.h, 5);
    set_source(cr_, parse_color(kPresets[i]));
    cairo_fill_preserve(cr_);
    set_source(cr_, with_alpha(pal_.fg_secondary, hovered(sw) ? 0.9 : 0.35));
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    if (press_pending_ && sw.hit(press_pos_.x, press_pos_.y)) {
      const Color c = parse_color(kPresets[i]);
      rgb_to_hsv(c.r, c.g, c.b, &ph_, &ps_, &pv_);
      picker_hex_ = kPresets[i];
      picker_entry_ = picker_hex_;
      press_pending_ = false;
    }
  }

  // hex entry + buttons using normal widgets positioned manually
  const double old_left = left_;
  left_ = px + 16;
  cx_ = left_;
  cy_ = py + 230;
  line_h_ = 0;
  label("Hex");
  if (text_entry(&picker_entry_, 110)) {
    const Color c = parse_color(picker_entry_.size() == 6 ? "#" + picker_entry_ : picker_entry_, {-1, 0, 0, 1});
    if (c.r >= 0) {
      rgb_to_hsv(c.r, c.g, c.b, &ph_, &ps_, &pv_);
      picker_hex_ = color_to_hex(c.r, c.g, c.b);
    }
  }
  cx_ = left_;
  cy_ = py + 282;
  line_h_ = 0;
  cx_ = px + pw - 24 - 64 - 8 - 78;
  const bool cancel = button("Cancel");
  bool select = button("Select", true, true);
  left_ = old_left;
  for (size_t i = 0; i < keys_.size();) {
    if (keys_[i].sym == XKB_KEY_Escape) {
      keys_.erase(keys_.begin() + static_cast<long>(i));
      picker_open_ = false;
      dirty_ = again_ = true;
      return;
    }
    if (keys_[i].sym == XKB_KEY_Return || keys_[i].sym == XKB_KEY_KP_Enter) {
      select = true;
    }
    ++i;
  }
  if (cancel) {
    picker_open_ = false;
    dirty_ = again_ = true;
  } else if (select) {
    picker_open_ = false;
    picker_done_ = true;
    picker_result_target_ = picker_target_;
    picker_result_ = picker_hex_;
    dirty_ = again_ = true;
  }
}

void Ui::open_file_dialog(const std::string& title, const std::string& start_path,
                          const std::vector<std::string>& extensions) {
  file_title_ = title;
  file_exts_ = extensions;
  std::string dir = start_path;
  struct stat st;
  if (dir.empty() || stat(dir.c_str(), &st) != 0) {
    const char* home = std::getenv("HOME");
    dir = home ? home : "/";
  } else if (!S_ISDIR(st.st_mode)) {
    const size_t slash = dir.rfind('/');
    file_selected_ = dir.substr(slash == std::string::npos ? 0 : slash + 1);
    dir = slash == std::string::npos || slash == 0 ? "/" : dir.substr(0, slash);
  }
  file_open_ = true;
  file_done_ = false;
  file_scroll_ = 0;
  last_click_row_ = -1;
  press_pending_ = release_pending_ = false;
  focus_id_ = 0;
  file_load_dir(dir);
  dirty_ = again_ = true;
}

bool Ui::take_file_result(std::string* path) {
  if (!file_done_) return false;
  file_done_ = false;
  *path = file_result_;
  return true;
}

void Ui::file_load_dir(const std::string& dir_in) {
  std::string dir = dir_in;
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  file_dir_ = dir;
  file_entries_.clear();
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      const std::string name = e->d_name;
      if (name == "." || name == ".." || name[0] == '.') continue;
      struct stat st;
      const std::string full = dir + (dir == "/" ? "" : "/") + name;
      if (stat(full.c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) {
        file_entries_.push_back({name, true});
      } else if (S_ISREG(st.st_mode)) {
        std::string low = name;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
        for (const auto& ext : file_exts_)
          if (low.size() > ext.size() && low.compare(low.size() - ext.size(), ext.size(), ext) == 0) {
            file_entries_.push_back({name, false});
            break;
          }
      }
    }
    closedir(d);
  }
  std::sort(file_entries_.begin(), file_entries_.end(), [](const FileEntry& a, const FileEntry& b) {
    if (a.dir != b.dir) return a.dir;
    std::string x = a.name, y = b.name;
    std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(y.begin(), y.end(), y.begin(), [](unsigned char c) { return std::tolower(c); });
    return x < y;
  });
  file_scroll_ = 0;
}

void Ui::draw_file_dialog() {
  cairo_rectangle(cr_, 0, 0, w_, h_);
  set_source(cr_, {0, 0, 0, 0.5});
  cairo_fill(cr_);
  const double pw = std::min(w_ - 24, 480.0), ph = std::min(h_ - 24, 400.0);
  const double px = (w_ - pw) / 2, py = (h_ - ph) / 2;
  rounded_rect(cr_, px, py, pw, ph, pal_.rounded ? 10 : 0);
  set_source(cr_, pal_.bg_secondary);
  cairo_fill_preserve(cr_);
  set_source(cr_, with_alpha(pal_.fg_secondary, 0.4));
  cairo_set_line_width(cr_, 1);
  cairo_stroke(cr_);
  draw_text(cr_, file_title_, px + 16, py + 28, kFont, pal_.fg_primary, true);

  // path row with an "Up" button
  const double old_left = left_;
  left_ = px + 16;
  cx_ = left_;
  cy_ = py + 38;
  line_h_ = 0;
  const bool up = button("Up");
  cairo_save(cr_);
  cairo_rectangle(cr_, cx_, cy_, px + pw - 16 - cx_, 32);
  cairo_clip(cr_);
  {
    // show the tail of long paths
    const TextExtents te = measure_text(cr_, file_dir_, kFont);
    const double avail = px + pw - 16 - cx_;
    draw_text(cr_, file_dir_, cx_ + std::min(0.0, avail - te.width), cy_ + (28 - te.height) / 2 + te.ascent + 0, kFont,
              pal_.fg_secondary);
  }
  cairo_restore(cr_);
  if (up) {
    const size_t slash = file_dir_.rfind('/');
    file_load_dir(slash == std::string::npos || slash == 0 ? "/" : file_dir_.substr(0, slash));
  }

  // list
  const double row_h = 26, list_y = py + 76, list_h = ph - 76 - 56;
  const UiRect list{px + 16, list_y, pw - 32, list_h};
  rounded_rect(cr_, list.x, list.y, list.w, list.h, 6);
  set_source(cr_, pal_.bg_primary);
  cairo_fill(cr_);
  const double content = file_entries_.size() * row_h;
  const double max_off = std::max(0.0, content - list.h);
  if (wheel_ != 0 && list.hit(mx_, my_)) {
    file_scroll_ += wheel_ * 3.0;
    wheel_ = 0;
  }
  file_scroll_ = std::clamp(file_scroll_, 0.0, max_off);
  cairo_save(cr_);
  cairo_rectangle(cr_, list.x, list.y, list.w, list.h);
  cairo_clip(cr_);
  bool open_selected = false;
  for (size_t i = 0; i < file_entries_.size(); ++i) {
    const UiRect row{list.x, list.y + i * row_h - file_scroll_, list.w - 8, row_h};
    if (row.y + row.h < list.y || row.y > list.y + list.h) continue;
    const FileEntry& fe = file_entries_[i];
    const bool sel = !fe.dir && fe.name == file_selected_;
    if (sel) {
      rounded_rect(cr_, row.x + 2, row.y + 1, row.w - 4, row.h - 2, 5);
      set_source(cr_, pal_.accent);
      cairo_fill(cr_);
    } else if (hovered(row) && list.hit(mx_, my_)) {
      rounded_rect(cr_, row.x + 2, row.y + 1, row.w - 4, row.h - 2, 5);
      set_source(cr_, with_alpha(pal_.accent, 0.18));
      cairo_fill(cr_);
    }
    const TextExtents te = measure_text(cr_, fe.name, kFont);
    const std::string shown = fe.dir ? fe.name + "/" : fe.name;
    draw_text(cr_, shown, row.x + 10, row.y + (row.h - te.height) / 2 + te.ascent, kFont,
              sel ? pal_.bg_primary : fe.dir ? pal_.fg_primary : pal_.fg_secondary, fe.dir);
    if (press_pending_ && list.hit(press_pos_.x, press_pos_.y) && row.hit(press_pos_.x, press_pos_.y)) {
      press_pending_ = false;
      const double t = now_s();
      const bool dbl = last_click_row_ == static_cast<int>(i) && t - last_click_t_ < 0.45;
      last_click_row_ = static_cast<int>(i);
      last_click_t_ = t;
      if (fe.dir) {  // a single click enters a directory
        file_load_dir(file_dir_ + (file_dir_ == "/" ? "" : "/") + fe.name);
        break;
      } else {
        file_selected_ = fe.name;
        if (dbl) open_selected = true;
      }
    }
  }
  cairo_restore(cr_);
  if (content > list.h) {
    const double th = std::max(24.0, list.h * list.h / content);
    rounded_rect(cr_, list.x + list.w - 6, list.y + (list.h - th) * (max_off > 0 ? file_scroll_ / max_off : 0), 3.5, th, 1.75);
    set_source(cr_, with_alpha(pal_.fg_secondary, 0.55));
    cairo_fill(cr_);
  }
  if (file_entries_.empty()) {
    draw_text(cr_, "No images here", list.x + 10, list.y + 24, kFont, pal_.fg_secondary);
  }

  // buttons
  cx_ = px + pw - 24 - 64 - 8 - 78;
  cy_ = py + ph - 46;
  line_h_ = 0;
  const bool cancel = button("Cancel");
  const bool open = button("Open", !file_selected_.empty(), true);
  left_ = old_left;
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i].sym == XKB_KEY_Escape) {
      file_open_ = false;
      keys_.erase(keys_.begin() + static_cast<long>(i));
      dirty_ = again_ = true;
      return;
    }
  }
  if (cancel) {
    file_open_ = false;
    dirty_ = again_ = true;
  } else if ((open || open_selected) && !file_selected_.empty()) {
    file_open_ = false;
    file_done_ = true;
    file_result_ = file_dir_ + (file_dir_ == "/" ? "" : "/") + file_selected_;
    dirty_ = again_ = true;
  }
}

}  // namespace fleetwm::kit
