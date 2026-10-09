#include "line_edit.hpp"

#include <algorithm>

namespace fleetwm::fm {

namespace {
bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }
bool is_word(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80 || c == '_'; }
}  // namespace

void LineEdit::set(const std::string& s, bool sel) {
  text = s;
  caret = text.size();
  anchor = sel ? 0 : caret;
}

std::string LineEdit::selected() const { return text.substr(sel_begin(), sel_end() - sel_begin()); }

void LineEdit::erase_selection() {
  if (!has_selection()) return;
  text.erase(sel_begin(), sel_end() - sel_begin());
  caret = anchor = std::min(caret, anchor);
}

void LineEdit::insert(const std::string& s) {
  erase_selection();
  text.insert(caret, s);
  caret += s.size();
  anchor = caret;
}

size_t LineEdit::prev_boundary(size_t i, bool word) const {
  if (i == 0) return 0;
  size_t j = i - 1;
  while (j > 0 && is_cont(static_cast<unsigned char>(text[j]))) --j;
  if (!word) return j;
  while (j > 0 && !is_word(static_cast<unsigned char>(text[j]))) j = prev_boundary(j, false);
  while (j > 0 && is_word(static_cast<unsigned char>(text[j - 1]))) --j;
  return j;
}

size_t LineEdit::next_boundary(size_t i, bool word) const {
  if (i >= text.size()) return text.size();
  size_t j = i + 1;
  while (j < text.size() && is_cont(static_cast<unsigned char>(text[j]))) ++j;
  if (!word) return j;
  while (j < text.size() && is_word(static_cast<unsigned char>(text[j]))) ++j;
  while (j < text.size() && !is_word(static_cast<unsigned char>(text[j]))) ++j;
  return j;
}

void LineEdit::backspace(bool word) {
  if (has_selection()) return erase_selection();
  const size_t p = prev_boundary(caret, word);
  text.erase(p, caret - p);
  caret = anchor = p;
}

void LineEdit::del(bool word) {
  if (has_selection()) return erase_selection();
  const size_t n = next_boundary(caret, word);
  text.erase(caret, n - caret);
  anchor = caret;
}

void LineEdit::left(bool extend, bool word) {
  if (!extend && has_selection()) {
    caret = anchor = sel_begin();
    return;
  }
  caret = prev_boundary(caret, word);
  if (!extend) anchor = caret;
}

void LineEdit::right(bool extend, bool word) {
  if (!extend && has_selection()) {
    caret = anchor = sel_end();
    return;
  }
  caret = next_boundary(caret, word);
  if (!extend) anchor = caret;
}

void LineEdit::home(bool extend) {
  caret = 0;
  if (!extend) anchor = 0;
}

void LineEdit::end(bool extend) {
  caret = text.size();
  if (!extend) anchor = caret;
}

void LineEdit::select_all() {
  anchor = 0;
  caret = text.size();
}

std::string LineEdit::cut() {
  std::string s = selected();
  erase_selection();
  return s;
}

void LineEdit::select_stem() {
  const size_t dot = text.rfind('.');
  anchor = 0;
  caret = dot == std::string::npos || dot == 0 ? text.size() : dot;
}

void LineEdit::set_caret_from_x(double x, double (*measure)(const std::string&, void*), void* ctx) {
  size_t best = 0;
  double best_d = 1e18;
  for (size_t i = 0; i <= text.size(); i = next_boundary(i, false)) {
    const double w = measure(text.substr(0, i), ctx);
    const double d = std::abs(w - x);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
    if (i == text.size()) break;
  }
  caret = best;
}

}  // namespace fleetwm::fm
