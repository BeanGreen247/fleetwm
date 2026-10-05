#pragma once

// A small immediate-mode widget layer on top of kit::Surface/cairo, used by
// fleetwm-settings (and available to any fleetkit client). Widgets are functions
// called every frame from the Surface's draw callback; they draw themselves,
// hit-test against the events queued since the last frame, and return true
// when the user changed something. Nothing is retained except focus, drag and
// modal-dialog state, so idle cost is zero.
//
// Typical use:
//   surface->on_draw = [&](cairo_t* cr, int w, int h) {
//     ui.begin(cr, w, h);
//     ui.heading("Title");
//     if (ui.checkbox("Enabled", &enabled)) save();
//     ui.end();
//   };
//   surface->on_button = [&](...) { ui.pointer_button(...); surface->queue_draw(); };

#include <cairo.h>

#include <functional>
#include <string>
#include <vector>

#include "fleetkit.hpp"

namespace fleetwm::kit {

struct UiRect {
  double x = 0, y = 0, w = 0, h = 0;
  bool hit(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

class Ui {
 public:
  explicit Ui(const Palette& pal) : pal_(pal) {}
  void set_palette(const Palette& pal) { pal_ = pal; }
  const Palette& palette() const { return pal_; }

  // ---- frame ----
  void begin(cairo_t* cr, double w, double h);
  void end();  // draws any open modal dialog on top and finishes the frame
  // True when the last input changed state that needs another frame (e.g. a
  // hover change or a modal opening). The app should queue_draw() after every
  // input event anyway; this is for animation-free hover tracking.
  bool take_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
  }

  // True when this frame changed state that the next frame must reflect (a
  // widget fired, a dialog opened/closed, a dialog result is waiting). The app
  // should queue_draw() again when it is set, at the end of its draw callback.
  bool wants_another_frame() const { return again_; }

  // ---- input (feed from Surface callbacks) ----
  void pointer_motion(double x, double y);
  void pointer_button(double x, double y, uint32_t button, bool pressed);
  void pointer_leave();
  void scroll(double dy);
  void key(const KeyEvent& ev);
  // Pastes text into the focused text entry (call with App::paste_text results).
  void paste(const std::string& text);
  bool wants_paste() const { return text_focus_; }

  // ---- layout ----
  void set_margins(double left, double top, double right) {
    left_ = left;
    top_ = top;
    right_ = right;
  }
  void set_label_width(double w) { label_w_ = w; }
  double content_bottom() const { return cy_; }  // y after the last row (for scroll extents)
  double width() const { return w_; }
  void newline(double extra = 0);  // finish the current line
  void space(double h);            // vertical gap
  void separator();
  void same_line() { want_same_line_ = true; }
  // Starts a labeled row: draws `label` and positions the cursor in the control column.
  void row(const std::string& label);

  // ---- text ----
  void heading(const std::string& text);
  void label(const std::string& text, bool secondary = false);
  // Wrapped paragraph in the secondary colour.
  void paragraph(const std::string& text, bool secondary = true);
  bool link(const std::string& text);

  // ---- controls (return true when changed / clicked) ----
  bool checkbox(const std::string& label, bool* value, bool enabled = true);
  // One radio button; `value` is the selected index of its group.
  bool radio(const std::string& label, int* value, int index, bool enabled = true);
  // A whole group, wrapping onto new lines at the control column when needed.
  bool radio_group(const std::vector<std::string>& options, int* value, bool enabled = true);
  bool button(const std::string& label, bool enabled = true, bool accent = false);
  bool spin(int* value, int min, int max, int step = 1, bool enabled = true);
  bool slider(int* value, int min, int max, double width = 0, bool enabled = true);
  bool color_button(std::string* hex, bool enabled = true);
  bool tabs(const std::vector<std::string>& names, int* current);
  bool text_entry(std::string* text, double width, bool enabled = true);

  // ---- scroll regions ----
  // Everything between begin_scroll() and end_scroll() is clipped to `view`
  // and offset by *offset (wheel scrolls it while the pointer is inside).
  void begin_scroll(UiRect view, double* offset);
  void end_scroll();

  // ---- modal dialogs ----
  void open_file_dialog(const std::string& title, const std::string& start_path,
                        const std::vector<std::string>& extensions);
  bool take_file_result(std::string* path);  // true once after the user picked a file
  bool modal_open() const { return picker_open_ || file_open_; }

 private:
  struct Pt {
    double x, y;
  };
  // frame helpers
  UiRect place(double w, double h);
  bool click_widget(int id, const UiRect& r);  // press+release inside r; also sets focus
  bool key_activate(int id);                   // Space/Enter on the focused widget
  bool hovered(const UiRect& r) const;
  int next_id() { return ++id_; }
  bool focused(int id) const { return focus_id_ == id; }
  void register_focusable_if(int id, bool enabled);
  void draw_focus_ring(const UiRect& r, double radius);
  bool visible(const UiRect& r) const;
  bool input_ok() const { return !modal_blocked_ || in_modal_; }

  // modals
  void draw_color_picker();
  void draw_file_dialog();
  void open_picker(const std::string& hex, int target);
  void picker_rebuild_sv();
  void file_load_dir(const std::string& dir);

  Palette pal_;
  cairo_t* cr_ = nullptr;
  double w_ = 0, h_ = 0;
  double left_ = 16, top_ = 16, right_ = 16, label_w_ = 150, control_x_ = 166;
  double cx_ = 0, cy_ = 0, row_h_ = 28, line_h_ = 0;
  bool want_same_line_ = false;
  int id_ = 0;

  // pointer / keyboard state
  double mx_ = -1, my_ = -1;
  bool down_ = false;
  bool press_pending_ = false, release_pending_ = false;
  Pt press_pos_{0, 0}, release_pos_{0, 0};
  int active_id_ = 0;  // widget that received the press
  double wheel_ = 0;
  std::vector<KeyEvent> keys_;
  int focus_id_ = 0;
  std::vector<int> focusables_, last_focusables_, modal_focusables_;
  bool dirty_ = false;
  bool again_ = false;
  bool mark(bool v) {
    if (v) again_ = true;
    return v;
  }
  bool text_focus_ = false;
  std::string pending_paste_;

  // spin / entry editing
  int edit_id_ = 0;
  std::string edit_buf_;
  size_t edit_cursor_ = 0;
  bool edit_fresh_ = false;

  // scrolling
  struct ScrollFrame {
    UiRect view;
    double* offset;
    double start_cy;
    double ox, oy;
    double saved_w, saved_left, saved_cx;
    UiRect saved_clip;
  };
  std::vector<ScrollFrame> scroll_stack_;
  double ox_ = 0, oy_ = 0;  // current content translation
  UiRect clip_{0, 0, 1e9, 1e9};

  // modal: colour picker
  bool picker_open_ = false;
  int picker_target_ = 0;
  double ph_ = 0, ps_ = 1, pv_ = 1;  // hue 0..360, sat/val 0..1
  std::string picker_hex_;
  bool picker_done_ = false;
  int picker_result_target_ = 0;
  std::string picker_result_;
  cairo_surface_t* sv_surface_ = nullptr;
  double sv_hue_ = -1;
  std::string picker_entry_;
  int picker_drag_ = 0;  // 1 = SV square, 2 = hue bar

  // modal: file dialog
  bool file_open_ = false;
  std::string file_title_, file_dir_, file_selected_;
  std::vector<std::string> file_exts_;
  struct FileEntry {
    std::string name;
    bool dir;
  };
  std::vector<FileEntry> file_entries_;
  double file_scroll_ = 0;
  bool file_done_ = false;
  std::string file_result_;
  double last_click_t_ = 0;
  int last_click_row_ = -1;

  bool modal_blocked_ = false;  // a modal was open at begin(): ordinary widgets ignore input
  bool in_modal_ = false;
};

// HSV helpers shared with the picker.
void hsv_to_rgb(double h, double s, double v, double* r, double* g, double* b);
void rgb_to_hsv(double r, double g, double b, double* h, double* s, double* v);
std::string color_to_hex(double r, double g, double b);

}  // namespace fleetwm::kit
