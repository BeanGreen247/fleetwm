#include <gtest/gtest.h>

#include "window_list.hpp"

using namespace fleetwm;

TEST(WindowList, EmptyListRoundTrips) {
  std::vector<WindowEntry> out{{9, true, false, "x", "y"}};
  EXPECT_EQ(format_window_list({}), "WINDOWS");
  ASSERT_TRUE(parse_window_list("WINDOWS", &out));
  EXPECT_TRUE(out.empty());
}

TEST(WindowList, RoundTripsEntriesAndFlags) {
  const std::vector<WindowEntry> in = {
      {1, true, false, "foot", "dev@host: ~"},
      {2, false, true, "dev.fleetwm.Settings", "Fleetwm Settings"},
      {7, true, true, "", ""},
      {8, false, false, "org.gnome.Nautilus", "Files"},
  };
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list(in), &out));
  EXPECT_EQ(out, in);
}

TEST(WindowList, FlagsAreCompact) {
  EXPECT_EQ(format_window_list({{3, true, true, "a", "b"}}), "WINDOWS\t3\tFM\ta\tb");
  EXPECT_EQ(format_window_list({{3, false, false, "a", "b"}}), "WINDOWS\t3\t-\ta\tb");
}

TEST(WindowList, TabsAndNewlinesInTextAreFlattened) {
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list({{1, false, false, "a\tb", "line1\nline2\r"}}), &out));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].app_id, "a b");
  EXPECT_EQ(out[0].title, "line1 line2 ");
}

TEST(WindowList, TitlesMayContainSpacesAndUnicode) {
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list({{4, false, false, "app", "Zoë — résumé.pdf"}}), &out));
  EXPECT_EQ(out[0].title, "Zoë — résumé.pdf");
}

TEST(WindowList, RejectsOtherLinesAndMalformedInput) {
  std::vector<WindowEntry> out{{1, false, false, "a", "b"}};
  EXPECT_FALSE(parse_window_list("WORKSPACE_CHANGED 2", &out));
  EXPECT_TRUE(out.empty());
  EXPECT_FALSE(parse_window_list("", &out));
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\tF\tapp", &out));        // missing title field
  EXPECT_FALSE(parse_window_list("WINDOWS\tabc\tF\tapp\ttitle", &out));  // non-numeric id
  EXPECT_FALSE(parse_window_list("WINDOWS\t1\tQ\tapp\ttitle", &out));    // bad flag
  EXPECT_FALSE(parse_window_list("WINDOWS\t\tF\tapp\ttitle", &out));     // empty id
  EXPECT_FALSE(parse_window_list("WINDOWS\t99999999999\tF\tapp\ttitle", &out));  // overlong id
  EXPECT_TRUE(out.empty());
}

TEST(WindowList, ManyWindowsKeepOrder) {
  std::vector<WindowEntry> in;
  for (uint32_t i = 1; i <= 40; ++i) in.push_back({i, i == 17, i % 5 == 0, "app" + std::to_string(i), "t" + std::to_string(i)});
  std::vector<WindowEntry> out;
  ASSERT_TRUE(parse_window_list(format_window_list(in), &out));
  EXPECT_EQ(out, in);
}
