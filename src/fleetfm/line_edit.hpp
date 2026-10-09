#pragma once

#include <cstddef>
#include <string>

// One-line text editing for the address bar, the search box and in-place rename: a UTF-8 string, a caret and a selection.
// The window feeds it key events; it knows nothing about drawing.

namespace fleetwm::fm {

struct LineEdit {
  std::string text;
  size_t caret = 0;       // byte offset, always on a character boundary
  size_t anchor = 0;      // selection runs between anchor and caret

  void set(const std::string& s, bool select_all = true);
  bool has_selection() const { return anchor != caret; }
  std::string selected() const;
  size_t sel_begin() const { return anchor < caret ? anchor : caret; }
  size_t sel_end() const { return anchor < caret ? caret : anchor; }

  void insert(const std::string& utf8);       // replaces the selection
  void backspace(bool word = false);
  void del(bool word = false);
  void left(bool extend = false, bool word = false);
  void right(bool extend = false, bool word = false);
  void home(bool extend = false);
  void end(bool extend = false);
  void select_all();
  std::string cut();                           // removes and returns the selection
  void set_caret_from_x(double x, double (*measure)(const std::string&, void*), void* ctx);  // click placement
  // Rename: the part of "report.final.txt" before the last dot is selected first, like Explorer.
  void select_stem();

 private:
  void erase_selection();
  size_t prev_boundary(size_t i, bool word) const;
  size_t next_boundary(size_t i, bool word) const;
};

}  // namespace fleetwm::fm
