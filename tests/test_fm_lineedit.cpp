#include <gtest/gtest.h>

#include "line_edit.hpp"

using namespace fleetwm::fm;

TEST(FmLineEdit, TypingInsertsAtTheCaretAndReplacesTheSelection) {
  LineEdit e;
  e.insert("hello");
  EXPECT_EQ(e.text, "hello");
  EXPECT_EQ(e.caret, 5u);
  e.left();
  e.left();
  e.insert("XY");
  EXPECT_EQ(e.text, "helXYlo");
  e.select_all();
  e.insert("new");
  EXPECT_EQ(e.text, "new");
  EXPECT_FALSE(e.has_selection());
}

TEST(FmLineEdit, BackspaceAndDeleteWorkOnWholeUtf8Characters) {
  LineEdit e;
  e.insert("aé€😀b");
  e.left();
  e.backspace();
  EXPECT_EQ(e.text, "aé€b");
  e.backspace();
  EXPECT_EQ(e.text, "aéb");
  e.left();
  e.del();
  EXPECT_EQ(e.text, "ab");
  e.home();
  e.backspace();
  EXPECT_EQ(e.text, "ab");
  e.end();
  e.del();
  EXPECT_EQ(e.text, "ab");
}

TEST(FmLineEdit, SelectionExtendsWithShiftAndCollapsesWithoutIt) {
  LineEdit e;
  e.set("hello world", false);
  e.home();
  e.right(true);
  e.right(true);
  EXPECT_EQ(e.selected(), "he");
  e.right();
  EXPECT_FALSE(e.has_selection());
  EXPECT_EQ(e.caret, 2u) << "plain right collapses the selection at its end";
  e.end(true);
  EXPECT_EQ(e.selected(), "llo world");
  e.left();
  EXPECT_EQ(e.caret, 2u) << "plain left collapses at the start";
}

TEST(FmLineEdit, WordJumpsAndWordDelete) {
  LineEdit e;
  e.set("one two  three", false);
  e.left(false, true);
  EXPECT_EQ(e.caret, 9u);
  e.left(false, true);
  EXPECT_EQ(e.caret, 4u);
  e.right(false, true);
  EXPECT_EQ(e.caret, 9u);
  e.backspace(true);
  EXPECT_EQ(e.text, "one three");
  e.home();
  e.del(true);
  EXPECT_EQ(e.text, "three");
}

TEST(FmLineEdit, CutReturnsTheSelection) {
  LineEdit e;
  e.set("abcdef", false);
  e.home();
  e.right(true);
  e.right(true);
  e.right(true);
  EXPECT_EQ(e.cut(), "abc");
  EXPECT_EQ(e.text, "def");
  EXPECT_EQ(e.cut(), "");
}

TEST(FmLineEdit, RenameSelectsTheNameWithoutItsExtension) {
  LineEdit e;
  e.set("report.final.txt");
  e.select_stem();
  EXPECT_EQ(e.selected(), "report.final");
  e.set(".bashrc");
  e.select_stem();
  EXPECT_EQ(e.selected(), ".bashrc");
  e.set("Makefile");
  e.select_stem();
  EXPECT_EQ(e.selected(), "Makefile");
  e.set("a.b");
  e.select_stem();
  e.insert("zip");
  EXPECT_EQ(e.text, "zip.b");
}

TEST(FmLineEdit, ClickPlacesTheCaretNearestTheX) {
  LineEdit e;
  e.set("abcd", false);
  auto measure = [](const std::string& s, void*) { return static_cast<double>(s.size()) * 10.0; };
  e.set_caret_from_x(24, measure, nullptr);
  EXPECT_EQ(e.caret, 2u);
  e.set_caret_from_x(26, measure, nullptr);
  EXPECT_EQ(e.caret, 3u);
  e.set_caret_from_x(-5, measure, nullptr);
  EXPECT_EQ(e.caret, 0u);
  e.set_caret_from_x(900, measure, nullptr);
  EXPECT_EQ(e.caret, 4u);
}
