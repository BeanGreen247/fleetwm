#include <gtest/gtest.h>

#include "window_list.hpp"

using namespace fleetwm;

namespace {
WindowEntry win(uint32_t id, bool focused, bool minimized, bool pinned, int ws, const char* app,
                const char* title) {
  WindowEntry w;
  w.id = id;
  w.focused = focused;
  w.minimized = minimized;
  w.pinned = pinned;
  w.workspace = ws;
  w.app_id = app;
  w.title = title;
  return w;
}
}  // namespace

TEST(WindowList, EmptyListRoundTrips) {
  std::vector<WindowEntry> out{win(9, true, false, false, 3, "x", "y")};
  EXPECT_EQ(format_window_list({}), "WINDOWS");
  ASSERT_TRUE(parse_window_list("WINDOWS", &out));
  EXPECT_TRUE(out.empty());
}

TEST(WindowList, RoundTripsEntriesFlagsAndWorkspaces) {
  const std::vector<WindowEntry> in = {
      win(1, true, false, false, 0, "foot", "dev@host: ~"),
      win(2, false, true, false, 3, "dev.fleetwm.Settings", "Fleetwm Settings"),
      win(7, true, true, true, 9, "", ""),
      win(8, false, false, true, 1, "org.gnome.Nautilus", "Files"),
  };
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list(in), &out));
  EXPECT_EQ(out, in);
}

TEST(WindowList, FlagsAndWorkspaceAreCompact) {
  EXPECT_EQ(format_window_list({win(3, true, true, false, 2, "a", "b")}), "WINDOWS\t3\tFM\t2\ta\tb");
  EXPECT_EQ(format_window_list({win(3, false, false, false, 0, "a", "b")}), "WINDOWS\t3\t-\t0\ta\tb");
  EXPECT_EQ(format_window_list({win(3, false, false, true, 5, "a", "b")}), "WINDOWS\t3\tP\t5\ta\tb");
}

TEST(WindowList, TheScreenRidesAlongTheWorkspaceField) {
  WindowEntry w = win(5, false, false, false, 2, "foot", "t");
  w.output = "DP-1";
  EXPECT_EQ(format_window_list({w}), "WINDOWS\t5\t-\t2@DP-1\tfoot\tt");
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list({w}), &out));
  EXPECT_EQ(out[0].output, "DP-1");
  EXPECT_EQ(out[0].workspace, 2);
  ASSERT_TRUE(parse_window_list("WINDOWS\t5\t-\t2\tfoot\tt", &out));  // the old form still parses
  EXPECT_EQ(out[0].output, "");
  EXPECT_FALSE(parse_window_list("WINDOWS\t5\t-\t2@\tfoot\tt", &out));
  EXPECT_FALSE(parse_window_list("WINDOWS\t5\t-\t2DP-1\tfoot\tt", &out));
}

TEST(WindowList, PinnedIsParsedSeparatelyFromFocus) {
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list("WINDOWS\t4\tFP\t6\tfoot\tx", &out));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].focused);
  EXPECT_TRUE(out[0].pinned);
  EXPECT_FALSE(out[0].minimized);
  EXPECT_EQ(out[0].workspace, 6);
}

TEST(WindowList, TabsAndNewlinesInTextAreFlattened) {
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list({win(1, false, false, false, 0, "a\tb", "line1\nline2\r")}), &out));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].app_id, "a b");
  EXPECT_EQ(out[0].title, "line1 line2 ");
}

TEST(WindowList, TitlesMayContainSpacesAndUnicode) {
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list({win(4, false, false, false, 0, "app", "Zoë — résumé.pdf")}), &out));
  EXPECT_EQ(out[0].title, "Zoë — résumé.pdf");
}

TEST(WindowList, RejectsOtherLinesAndMalformedInput) {
  std::vector<WindowEntry> out{win(1, false, false, false, 0, "a", "b")};
  EXPECT_FALSE(parse_window_list("WORKSPACE_CHANGED 2", &out));
  EXPECT_TRUE(out.empty());
  EXPECT_FALSE(parse_window_list("", &out));
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\tF\t0\tapp", &out));           // missing title field
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\tF\tapp\ttitle", &out));       // old four-field format
  EXPECT_FALSE(parse_window_list("WINDOWS\tabc\tF\t0\tapp\ttitle", &out));  // non-numeric id
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\tQ\t0\tapp\ttitle", &out));    // bad flag
  EXPECT_FALSE(parse_window_list("WINDOWS\t\tF\t0\tapp\ttitle", &out));     // empty id
  EXPECT_FALSE(parse_window_list("WINDOWS\t99999999999\tF\t0\tapp\ttitle", &out));
  EXPECT_TRUE(out.empty());
}

TEST(WindowList, RejectsABadWorkspace) {
  std::vector<WindowEntry> out;
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\t-\tx\tapp\ttitle", &out));
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\t-\t10\tapp\ttitle", &out));  // 0-9 only
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\t-\t\tapp\ttitle", &out));
  EXPECT_TRUE(out.empty());
}

TEST(WindowList, ManyWindowsKeepOrder) {
  std::vector<WindowEntry> in;
  for (uint32_t i = 1; i <= 40; ++i)
    in.push_back(win(i, i == 17, i % 5 == 0, i % 7 == 0, static_cast<int>(i % 10), ("app" + std::to_string(i)).c_str(),
                     ("t" + std::to_string(i)).c_str()));
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list(in), &out));
  EXPECT_EQ(out, in);
}
