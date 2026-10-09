#include <algorithm>
#include <cmath>
#include <filesystem>

#include "file_ops.hpp"
#include "window.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

void FmWindow::paint_overlays() {
  paint_transfers();
  {  // a tooltip with the facts of the item the pointer rests on
    Browser& b = tab();
    if (hover_item_ != tip_item_) {
      tip_item_ = hover_item_;
      tip_since_ = now_();
      if (hover_item_ >= 0 && host_.after) {
        auto alive = alive_;
        host_.after(650, [this, alive] {
          if (*alive) schedule_redraw();
        });
      }
    }
    if (tip_item_ >= 0 && tip_item_ < static_cast<int>(b.shown.size()) && now_() - tip_since_ > 0.6 && !menu_open() && dlg_ == Dlg::None && !item_drag_ && !renaming_) {
      b.ensure_stat(tip_item_);
      const Entry& e = b.shown[tip_item_];
      const bool dir = b.raw.is_dir(e);
      std::vector<std::string> lines = {b.label_at(tip_item_), "Type: " + type_description(b.label_at(tip_item_), dir)};
      if (!dir) lines.push_back("Size: " + format_size(e.size));
      lines.push_back("Date modified: " + format_date(static_cast<time_t>(e.mtime), s_.date_style));
      if (!b.folder_at(tip_item_).empty()) lines.push_back("In: " + b.folder_at(tip_item_));
      double w = 0;
      for (const std::string& l : lines) w = std::max(w, text_w(l, font_px()));
      w += 20;
      const double h = 10 + lines.size() * (font_px() + 5);
      double x = std::min<double>(mx_ + 14, W_ - w - 4), y = std::min<double>(my_ + 18, H_ - h - 4);
      box(x + 2, y + 2, w, h, 3, col_.shadow, col_.shadow, nullptr);
      box(x, y, w, h, 3, col_.menu_bg, col_.menu_bg, &col_.menu_border);
      double ty = y + 5 + font_px() / 2.0 + 1;
      for (size_t k = 0; k < lines.size(); ++k, ty += font_px() + 5) text(lines[k], x + 10, ty, font_px(), k == 0 ? col_.text : col_.text_dim, k == 0);
    }
  }
  if (item_drag_ && !drag_paths_.empty()) {  // the ghost that follows the pointer when the compositor does not draw one
    const std::string label = drag_paths_.size() == 1 ? fs::path(drag_paths_[0]).filename().string() : std::to_string(drag_paths_.size()) + " items";
    const double w = text_w(label, font_px()) + 34;
    box(mx_ + 12, my_ + 8, w, 24, 4, col_.menu_bg, col_.menu_bg, &col_.accent);
    icon(drag_paths_.size() == 1 ? IconKind::File : IconKind::Library, mx_ + 17, my_ + 12, 16);
    text(label, mx_ + 38, my_ + 20, font_px(), col_.text);
  }
  if (!toast_.empty() && now_() < toast_until_) {
    const double w = text_w(toast_, font_px()) + 28;
    const Rect r{static_cast<int>((W_ - w) / 2), H_ - 70, static_cast<int>(w), 30};
    box(r.x, r.y, r.w, r.h, 4, col_.menu_bg, col_.menu_bg, &col_.menu_border);
    text(toast_, r.x + 14, r.y + r.h / 2.0, font_px(), col_.text);
    add_region(r, R::Toast);
  }
  if (menu_open()) paint_menu();
  if (dlg_ != Dlg::None) paint_dialog();
}

// One menu level. Returns its size and the row rectangles (for placing a submenu beside the row it belongs to).
void FmWindow::paint_menu_level(const std::vector<MenuItem>& items, double* px_x, double* px_y, int hot, R kind, double* out_w, double* out_h, std::vector<Rect>* rows) {
  const double px = font_px();
  double w = 140;
  for (const MenuItem& it : items) {
    if (it.separator) continue;
    w = std::max(w, text_w(it.label, px, it.bold) + (it.shortcut.empty() ? 0 : text_w(it.shortcut, px) + 36) + 64);
  }
  double h = 8;
  for (const MenuItem& it : items) h += it.separator ? 9 : 25;
  double x = *px_x, y = *px_y;
  if (x + w > W_ - 4) x = std::max(4.0, W_ - w - 4);
  if (y + h > H_ - 4) y = std::max(4.0, H_ - h - 4);
  *px_x = x;
  *px_y = y;
  *out_w = w;
  *out_h = h;
  box(x + 3, y + 3, w, h, 3, col_.shadow, col_.shadow, nullptr);
  box(x, y, w, h, s_.style == ViewStyle::Windows7 ? 3 : (style_->corner > 4 ? 8 : 2), col_.menu_bg, col_.menu_bg, &col_.menu_border);
  // the Windows 7 icon gutter: a slightly different strip at the left, with a thin line
  if (s_.style == ViewStyle::Windows7) {
    box(x + 1, y + 1, 26, h - 2, 2, col_.nav_bg, col_.nav_bg, nullptr);
    cairo_move_to(cr_, x + 27.5, y + 2);
    cairo_line_to(cr_, x + 27.5, y + h - 2);
    kit::set_source(cr_, col_.head_sep);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
  }
  add_region({static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)}, kind, -1);
  double cy = y + 4;
  rows->assign(items.size(), Rect{});
  for (size_t i = 0; i < items.size(); ++i) {
    const MenuItem& it = items[i];
    if (it.separator) {
      cairo_move_to(cr_, x + 32, cy + 4.5);
      cairo_line_to(cr_, x + w - 6, cy + 4.5);
      kit::set_source(cr_, col_.head_sep);
      cairo_set_line_width(cr_, 1);
      cairo_stroke(cr_);
      cy += 9;
      continue;
    }
    const Rect rr{static_cast<int>(x + 3), static_cast<int>(cy), static_cast<int>(w - 6), 25};
    (*rows)[i] = rr;
    const bool is_hot = static_cast<int>(i) == hot && it.enabled;
    const bool flat_hot = col_.hot_top.r == col_.hot_bot.r && col_.hot_top.g == col_.hot_bot.g && col_.hot_top.b == col_.hot_bot.b;
    if (is_hot) box(rr.x, rr.y, rr.w, rr.h, 2, flat_hot ? col_.menu_hot : col_.hot_top, flat_hot ? col_.menu_hot : col_.hot_bot, &col_.hot_border);
    const Color tc = !it.enabled ? col_.text_off : (is_hot && col_.menu_hot.r < 0.6 && !col_.dark && s_.style != ViewStyle::Windows7 && s_.style != ViewStyle::Windows10 ? col_.accent_text : col_.text);
    if (it.checked) {
      if (it.radio) {
        cairo_arc(cr_, x + 16, cy + 12.5, 3.5, 0, 2 * M_PI);
        kit::set_source(cr_, tc);
        cairo_fill(cr_);
      } else {
        glyph(25, x + 9, cy + 6, 13, tc);
      }
    }
    text(it.label, x + 34, cy + 12.5, px, tc, it.bold);
    if (!it.sub.empty()) glyph(19, x + w - 20, cy + 6, 12, tc);
    else if (!it.shortcut.empty()) text(it.shortcut, x + w - 12 - text_w(it.shortcut, px), cy + 12.5, px, col_.text_dim);
    add_region(rr, kind, static_cast<int>(i));
    cy += 25;
  }
}

void FmWindow::paint_menu() {
  double w, h;
  menu_.rows.clear();
  paint_menu_level(menu_.items, &menu_.x, &menu_.y, menu_.hot, R::MenuItem, &w, &h, &menu_.rows);
  menu_.w = w;
  menu_.h = h;
  if (sub_.parent >= 0 && sub_.parent < static_cast<int>(menu_.rows.size()) && !sub_.items.empty()) {
    const Rect pr = menu_.rows[sub_.parent];
    sub_.x = menu_.x + menu_.w - 2;
    sub_.y = pr.y - 4;
    double sw, sh;
    std::vector<Rect> srows;
    // not enough room at the right: open to the left of the parent menu
    double need = 140;
    for (const MenuItem& it : sub_.items) need = std::max(need, text_w(it.label, font_px()) + 80);
    if (sub_.x + need > W_) sub_.x = std::max(4.0, menu_.x - need + 2);
    paint_menu_level(sub_.items, &sub_.x, &sub_.y, sub_.hot, R::SubItem, &sw, &sh, &srows);
    sub_.w = sw;
    sub_.h = sh;
  }
}

void FmWindow::paint_transfers() {
  const int cw = 392;
  double y = H_ - 12;
  int idx = static_cast<int>(jobs_.size());
  for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it, --idx) {
    Job& j = **it;
    const bool finished = j.done;
    const int ch = finished ? 112 : (j.details ? 238 : 138);
    Rect r{W_ - cw - 12, static_cast<int>(y - ch), cw, ch};
    if (r.y < 4) break;
    paint_job_card(j, r, idx - 1);
    y -= ch + 8;
  }
}

void FmWindow::paint_job_card(Job& j, Rect r, int index) {
  const double px = font_px();
  box(r.x + 3, r.y + 3, r.w, r.h, 4, col_.shadow, col_.shadow, nullptr);
  box(r.x, r.y, r.w, r.h, s_.style == ViewStyle::Windows7 ? 3 : (style_->corner > 4 ? 10 : 2), col_.window, col_.window, &col_.chrome_border);
  add_region(r, R::JobBtn, index, -1);
  const Progress p = j.t->progress();
  const Region* hr = region_at(mx_, my_);
  auto btn = [&](Rect br, const std::string& label, int id, bool accent = false) {
    const bool hot = hr && hr->kind == R::JobBtn && hr->arg == index && hr->arg2 == id;
    button(br, label, hot, false, true, accent);
    add_region(br, R::JobBtn, index, id);
  };
  if (j.done) {
    const bool ok = j.result.ok();
    icon(ok ? IconKind::FolderOpen : IconKind::Folder, r.x + 10, r.y + 10, 32);
    glyph(ok ? 25 : 21, r.x + 30, r.y + 30, 14, ok ? col_.ok : col_.danger);
    std::string head = j.result.cancelled ? "Canceled" : (ok ? (j.move ? "Move complete" : "Copy complete") : "Finished with problems");
    text(head, r.x + 54, r.y + 22, px + 1, col_.text, true);
    std::string sub;
    if (j.result.cancelled) sub = "Nothing was changed after the point of cancellation.";
    else if (ok && !j.result.verified.empty()) sub = std::to_string(j.result.verified.size()) + (j.result.verified.size() == 1 ? " file" : " files") + " checked against the original (" + hash_label(resolve_algo(j.verify_algo)) + ").";
    else if (ok) sub = std::to_string(j.result.files_copied) + (j.result.files_copied == 1 ? " item" : " items") + " done.";
    else sub = std::to_string(j.result.errors.size() + j.result.mismatches) + " problems. " + (j.result.mismatches ? std::to_string(j.result.mismatches) + " copies did not match and were discarded." : "");
    text(fit(sub, r.w - 66, px - 1), r.x + 54, r.y + 42, px - 1, col_.text_dim);
    text(fit("to " + j.to, r.w - 66, px - 1), r.x + 54, r.y + 60, px - 1, col_.text_dim);
    int bx = r.x + r.w - 12;
    bx -= 70;
    btn({bx, r.y + r.h - 34, 70, 26}, "OK", 3, true);
    if (!j.result.verified.empty()) {
      bx -= 132;
      btn({bx, r.y + r.h - 34, 126, 26}, "Show checksums", 4);
    }
    if (!j.result.errors.empty()) {
      bx -= 100;
      btn({bx, r.y + r.h - 34, 94, 26}, "Show errors", 5);
    }
    return;
  }
  const char* verb = j.move ? "Moving" : "Copying";
  std::string title;
  switch (p.phase) {
    case Phase::Discovering: title = "Calculating..."; break;
    case Phase::Verifying: title = "Verifying"; break;
    case Phase::Finishing: title = "Writing to the drive"; break;
    default: title = std::string(verb) + " " + std::to_string(p.files_total) + (p.files_total == 1 ? " item" : " items"); break;
  }
  if (p.paused) title += " (paused)";
  const int pct = static_cast<int>(p.fraction() * 100);
  text(title + (p.phase == Phase::Discovering ? "" : " - " + std::to_string(pct) + "% complete"), r.x + 12, r.y + 18, px + 1, col_.text, true);
  text(fit("from " + j.from + " to " + j.to, r.w - 24, px - 1), r.x + 12, r.y + 38, px - 1, col_.text_dim);
  paint_progress({r.x + 12, r.y + 50, r.w - 24, 18}, p.fraction(), false);
  std::string line;
  const std::string name = fs::path(p.current).filename().string();
  if (p.phase == Phase::Verifying) line = "Checking '" + name + "' by reading it back from the drive";
  else if (p.phase == Phase::Finishing) line = "Flushing '" + name + "' to the drive";
  else if (!name.empty()) line = "Name: " + name;
  text(fit(line, r.w - 24, px - 1), r.x + 12, r.y + 80, px - 1, col_.text);
  text(format_remaining(p.eta), r.x + 12, r.y + 98, px - 1, col_.text_dim);
  const std::string sp = p.speed > 0 ? "Speed: " + format_speed(p.speed) : "";
  text(sp, r.x + r.w - 12 - text_w(sp, px - 1), r.y + 98, px - 1, col_.text_dim);
  if (j.details) {
    // speed history as a graph, like the newer Explorer's "More details"
    const Rect g{r.x + 12, r.y + 110, r.w - 24, 74};
    box(g.x, g.y, g.w, g.h, 0, col_.content, col_.content, &col_.field_border);
    double mx = 1;
    for (double v : j.speeds) mx = std::max(mx, v);
    if (!j.speeds.empty()) {
      cairo_save(cr_);
      use_clip(g);
      cairo_move_to(cr_, g.x, g.y + g.h);
      const double step = static_cast<double>(g.w) / 60.0;
      double x = g.x + g.w - step * static_cast<double>(j.speeds.size());
      for (double v : j.speeds) {
        cairo_line_to(cr_, x, g.y + g.h - (g.h - 4) * v / mx);
        x += step;
      }
      cairo_line_to(cr_, x - step, g.y + g.h);
      cairo_close_path(cr_);
      Color f = col_.bar_top;
      f.a = 0.35;
      kit::set_source(cr_, f);
      cairo_fill_preserve(cr_);
      kit::set_source(cr_, col_.bar_bot);
      cairo_set_line_width(cr_, 1.2);
      cairo_stroke(cr_);
      cairo_restore(cr_);
    }
    text("Peak " + format_speed(mx), g.x + 6, g.y + 12, px - 2, col_.text_dim);
    text("Items remaining: " + std::to_string(p.files_total - std::min(p.files_total, p.files_done)) + " (" + format_size(p.bytes_total - std::min(p.bytes_total, p.bytes_done)) + ")", r.x + 12, r.y + 198, px - 1, col_.text_dim);
  }
  int bx = r.x + r.w - 12 - 70;
  btn({bx, r.y + r.h - 34, 70, 26}, "Cancel", 1);
  bx -= 76;
  btn({bx, r.y + r.h - 34, 70, 26}, p.paused ? "Resume" : "Pause", 0);
  const Rect lr{r.x + 12, r.y + r.h - 30, 110, 20};
  const bool lhot = hr && hr->kind == R::JobBtn && hr->arg == index && hr->arg2 == 2;
  text(j.details ? "Fewer details" : "More details", lr.x, lr.y + 10, px - 1, lhot ? col_.accent : col_.text_dim);
  add_region(lr, R::JobBtn, index, 2);
}

void FmWindow::paint_dialog() {
  // dim everything behind
  cairo_save(cr_);
  cairo_set_source_rgba(cr_, 0, 0, 0, col_.dark ? 0.5 : 0.35);
  cairo_paint(cr_);
  cairo_restore(cr_);
  add_region({0, 0, W_, H_}, R::DlgButton, -1);
  int dw = 440, dh = 220;
  switch (dlg_) {
    case Dlg::About: dw = 460; dh = 360; break;
    case Dlg::Properties: dw = 420; dh = 500; break;
    case Dlg::Connect: dw = 520; dh = 420; break;
    case Dlg::Nextcloud: dw = 500; dh = 340; break;
    case Dlg::Conflict: dw = 520; dh = 250; break;
    case Dlg::Checksums: dw = 700; dh = 420; break;
    case Dlg::Errors: dw = 640; dh = 380; break;
    case Dlg::ConfirmDelete: dw = 460; dh = 200; break;
    case Dlg::ConfirmEmptyTrash: dw = 440; dh = 180; break;
    default: break;
  }
  dw = std::min(dw, W_ - 24);
  dh = std::min(dh, H_ - 24);
  dlg_rect_ = {(W_ - dw) / 2, (H_ - dh) / 2, dw, dh};
  box(dlg_rect_.x + 4, dlg_rect_.y + 5, dw, dh, 6, col_.shadow, col_.shadow, nullptr);
  box(dlg_rect_.x, dlg_rect_.y, dw, dh, s_.style == ViewStyle::Windows7 ? 5 : 8, col_.window, col_.window, &col_.chrome_border);
  add_region(dlg_rect_, R::DlgButton, -2);
  cairo_save(cr_);
  kit::Palette pal;
  pal.bg_primary = col_.window;
  pal.bg_secondary = col_.nav_bg;
  pal.fg_primary = col_.text;
  pal.fg_secondary = col_.text_dim;
  pal.accent = col_.accent;
  pal.rounded = style_->corner > 0;
  pal.window_alpha = 1.0;
  if (!ui_) ui_ = std::make_unique<kit::Ui>(pal);
  else ui_->set_palette(pal);
  cairo_rectangle(cr_, dlg_rect_.x + 1, dlg_rect_.y + 1, dw - 2, dh - 2);
  cairo_clip(cr_);
  cairo_translate(cr_, dlg_rect_.x, dlg_rect_.y);
  ui_->begin(cr_, dw, dh);
  switch (dlg_) {
    case Dlg::About: dialog_about(*ui_, dw, dh); break;
    case Dlg::Properties: dialog_properties(*ui_, dw, dh); break;
    case Dlg::Connect: dialog_connect(*ui_, dw, dh); break;
    case Dlg::Nextcloud: dialog_nextcloud(*ui_, dw, dh); break;
    case Dlg::Conflict: dialog_conflict(*ui_, dw, dh); break;
    case Dlg::ConfirmDelete: dialog_confirm_delete(*ui_, dw, dh); break;
    case Dlg::Message: dialog_message(*ui_, dw, dh); break;
    case Dlg::Checksums: dialog_checksums(*ui_, dw, dh); break;
    case Dlg::Errors: dialog_errors(*ui_, dw, dh); break;
    case Dlg::ConfirmEmptyTrash: dialog_confirm_empty_trash(*ui_, dw, dh); break;
    default: break;
  }
  ui_->end();
  cairo_restore(cr_);
  if (ui_->wants_another_frame()) schedule_redraw();
}

}  // namespace fleetwm::fm
