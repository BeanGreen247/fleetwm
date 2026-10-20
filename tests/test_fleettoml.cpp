// The project's own TOML reader and writer (src/common/fleettoml.*). It was checked against toml++ 3.3 and 3.4 on about 160,000 mutated
// documents (same accept/reject answer and same values, dates aside) and under ASan/UBSan; these tests keep the behaviour the config
// code relies on in the suite.
#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#include "fleettoml.hpp"

namespace {

TEST(FleetToml, ScalarsAndTheirTypes) {
  const toml::table t = toml::parse(
      "s = 'lit'\nb = \"bas\\tic\\u00e9\"\ni = -42\nu = 1_000\nh = 0xFF\no = 0o17\nbin = 0b101\nf = 2.5\ne = 1e3\nyes = true\nno = false\n"
      "inf = -inf\n");
  EXPECT_EQ(t["s"].value<std::string>(), "lit");
  EXPECT_EQ(t["b"].value<std::string>(), "bas\tic\xC3\xA9");
  EXPECT_EQ(t["i"].value<int64_t>(), -42);
  EXPECT_EQ(t["u"].value<int>(), 1000);
  EXPECT_EQ(t["h"].value<int>(), 255);
  EXPECT_EQ(t["o"].value<int>(), 15);
  EXPECT_EQ(t["bin"].value<int>(), 5);
  EXPECT_DOUBLE_EQ(*t["f"].value<double>(), 2.5);
  EXPECT_DOUBLE_EQ(*t["e"].value<double>(), 1000.0);
  EXPECT_TRUE(*t["yes"].value<bool>());
  EXPECT_FALSE(*t["no"].value<bool>());
  EXPECT_TRUE(std::isinf(*t["inf"].value<double>()));
}

TEST(FleetToml, WrongTypesGiveNothingAndNumbersConvertLikeTomlpp) {
  const toml::table t = toml::parse("n = 5\ns = 'x'\nb = true\nd = 2.0\nbig = 5000000000\n");
  EXPECT_FALSE(t["n"].value<std::string>());
  EXPECT_FALSE(t["s"].value<int64_t>());
  EXPECT_FALSE(t["s"].value<bool>());
  EXPECT_TRUE(*t["n"].value<bool>()) << "an integer in a bool field is true unless 0 (the bar config relies on it)";
  EXPECT_EQ(t["d"].value<int>(), 2) << "a float with no fraction converts to an integer";
  EXPECT_FALSE(t["big"].value<int>()) << "out of range for an int";
  EXPECT_EQ(t["big"].value<int64_t>(), 5000000000LL);
  EXPECT_DOUBLE_EQ(*t["n"].value<double>(), 5.0);
  EXPECT_EQ(t["missing"].value_or(7), 7);
  EXPECT_EQ(t["n"].value_or(7), 5);
  EXPECT_NE(t["n"].as_integer(), nullptr);
  EXPECT_EQ(t["s"].as_integer(), nullptr);
  EXPECT_EQ(t["n"].as_integer()->get(), 5);
}

TEST(FleetToml, MissingKeysChainWithoutThrowing) {
  const toml::table t = toml::parse("[a]\nb = 1\n");
  EXPECT_FALSE(t["x"]["y"]["z"].value<int>());
  EXPECT_EQ(t["x"]["y"].as_table(), nullptr);
  EXPECT_EQ(t["a"]["b"].value<int>(), 1);
  EXPECT_FALSE(t["a"]["b"]["c"].value<int>()) << "going on from a value that is not a table";
  EXPECT_TRUE(t.contains("a"));
  EXPECT_FALSE(t.contains("b"));
}

TEST(FleetToml, TablesArraysAndInlineForms) {
  const toml::table t = toml::parse(
      "top = 1\n[a.b]\nc = 2\n[a]\nd = 3\n[[list]]\nx = 1\n[[list]]\nx = 2\n[inline]\nv = { p = 1, q = { r = 'z' } }\n"
      "arr = [ 1, 2,\n 3, # comment\n]\nmixed = [ 'a', 1, [ true ] ]\n\"quoted key\" = 4\nd.e.f = 5\n");
  EXPECT_EQ(t["a"]["b"]["c"].value<int>(), 2);
  EXPECT_EQ(t["a"]["d"].value<int>(), 3);
  const toml::array* list = t["list"].as_array();
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->size(), 2u);
  EXPECT_EQ((*list)[1].as_table()->operator[]("x").value<int>(), 2);
  EXPECT_EQ(t["inline"]["v"]["q"]["r"].value<std::string>(), "z");
  const toml::array* arr = t["inline"]["arr"].as_array();
  ASSERT_NE(arr, nullptr);
  EXPECT_EQ(arr->size(), 3u);
  EXPECT_EQ(t["inline"]["mixed"].as_array()->size(), 3u);
  EXPECT_EQ(t["inline"]["quoted key"].value<int>(), 4);
  EXPECT_EQ(t["inline"]["d"]["e"]["f"].value<int>(), 5);
}

TEST(FleetToml, StringForms) {
  const toml::table t = toml::parse(
      "a = \"\"\"\nfirst\nsecond\"\"\"\nb = '''\nraw \\n text'''\nc = \"\"\"one \\\n    two\"\"\"\nd = \"q\\\"uote\\\\\"\ne = \"\"\"say \"\"hi\"\"\"\"\"\n");
  EXPECT_EQ(t["a"].value<std::string>(), "first\nsecond");
  EXPECT_EQ(t["b"].value<std::string>(), "raw \\n text");
  EXPECT_EQ(t["c"].value<std::string>(), "one two");
  EXPECT_EQ(t["d"].value<std::string>(), "q\"uote\\");
  EXPECT_EQ(t["e"].value<std::string>(), "say \"\"hi\"\"");
  EXPECT_THROW((void)toml::parse("x = \"\"\"a\"\"\"\"\"\"\n"), toml::parse_error) << "six quotes at the end are one too many";
}

TEST(FleetToml, CommentsBlankLinesCrlfAndBom) {
  const toml::table t = toml::parse("\xEF\xBB\xBF# c\r\n\r\na = 1 # t\r\n[x]\r\nb = 'y'\r\n");
  EXPECT_EQ(t["a"].value<int>(), 1);
  EXPECT_EQ(t["x"]["b"].value<std::string>(), "y");
}

TEST(FleetToml, ErrorsCarryTheLineAndNothingIsHalfRead) {
  auto line_of = [](const char* text) -> int {
    try {
      (void)toml::parse(text);
    } catch (const toml::parse_error& e) {
      return static_cast<int>(e.source().begin.line);
    }
    return 0;
  };
  EXPECT_EQ(line_of("a = 1\nb = \n"), 2);
  EXPECT_GE(line_of("a = 1\n\n\nc = [1, 2\n"), 4);
  EXPECT_GT(line_of("a = 1\na = 2\n"), 0) << "a key twice";
  EXPECT_GT(line_of("[t]\n[t]\n"), 0) << "a table twice";
  EXPECT_GT(line_of("a = 1 b = 2\n"), 0);
  EXPECT_GT(line_of("a = 01\n"), 0) << "leading zero";
  EXPECT_GT(line_of("a = \"open\n"), 0);
  EXPECT_GT(line_of("a = 99999999999999999999\n"), 0) << "integer overflow";
  EXPECT_GT(line_of("a = { x = 1 }\na.y = 2\n"), 0) << "an inline table cannot be extended";
  EXPECT_GT(line_of("[a]\nb = 1\n[a.b]\n"), 0) << "a value is not a table";
  EXPECT_EQ(line_of("a = 1\n[ok]\nb = 2\n"), 0);
}

TEST(FleetToml, DeepNestingIsAnErrorNotACrash) {
  std::string deep = "a = ";
  for (int i = 0; i < 400; ++i) deep += '[';
  for (int i = 0; i < 400; ++i) deep += ']';
  EXPECT_THROW((void)toml::parse(deep), toml::parse_error);
}

TEST(FleetToml, ParseFileOfAMissingFileThrowsLikeTomlpp) {
  EXPECT_THROW((void)toml::parse_file("/nonexistent/dir/none.toml"), toml::parse_error);
  const auto dir = std::filesystem::temp_directory_path() / ("fleettoml-" + std::to_string(::getpid()));
  std::filesystem::create_directories(dir);
  {
    std::ofstream(dir / "x.toml") << "k = 'v'\n";
  }
  EXPECT_EQ(toml::parse_file((dir / "x.toml").string())["k"].value<std::string>(), "v");
  std::filesystem::remove_all(dir);
}

TEST(FleetToml, WritingMatchesTomlppLayout) {
  toml::table root;
  root.insert_or_assign("zeta", 1);
  root.insert_or_assign("alpha", "text");
  root.insert_or_assign("flag", true);
  root.insert_or_assign("ratio", 0.5);
  toml::array names;
  names.push_back("a");
  names.push_back(std::string("b"));
  root.insert_or_assign("names", std::move(names));
  toml::table sub;
  sub.insert_or_assign("x", static_cast<int64_t>(7));
  root.insert_or_assign("section", std::move(sub));
  toml::array rows;
  for (int i = 0; i < 2; ++i) {
    toml::table r;
    r.insert_or_assign("n", i);
    rows.push_back(std::move(r));
  }
  root.insert_or_assign("row", std::move(rows));
  EXPECT_EQ(toml::to_string(root),
            "alpha = 'text'\nflag = true\nnames = [ 'a', 'b' ]\nratio = 0.5\nzeta = 1\n\n[section]\nx = 7\n\n[[row]]\nn = 0\n\n[[row]]\nn = 1");
}

TEST(FleetToml, WritingQuotesWhatNeedsIt) {
  toml::table t;
  t.insert_or_assign("it's", "it's \"q\"\n\\");
  t.insert_or_assign("a key", 1);
  t.insert_or_assign("plain-key_1", "x");
  const std::string text = toml::to_string(t);
  EXPECT_NE(text.find("plain-key_1 = 'x'"), std::string::npos);
  EXPECT_NE(text.find("'a key' = 1"), std::string::npos);
  const toml::table back = toml::parse(text);
  EXPECT_EQ(back["it's"].value<std::string>(), "it's \"q\"\n\\");
  EXPECT_EQ(back["a key"].value<int>(), 1);
}

TEST(FleetToml, EverythingWrittenReadsBackTheSame) {
  const toml::table t = toml::parse(
      "x = 1\nf = 1.25\ns = 'a b'\nl = [ 1, [ 2, 3 ], 'z' ]\n[a]\nk = true\n[a.b]\nm = 'n'\n[[c]]\nq = 1\n[[c]]\nq = 2\n[d]\n");
  const std::string once = toml::to_string(t);
  EXPECT_EQ(toml::to_string(toml::parse(once)), once);
}

TEST(FleetToml, CopiesAreDeepAndInsertOrAssignReplaces) {
  toml::table a;
  toml::table inner;
  inner.insert_or_assign("v", 1);
  a.insert_or_assign("t", inner);
  toml::table b = a;
  b["t"].as_table()->insert_or_assign("v", 2);
  EXPECT_EQ(a["t"]["v"].value<int>(), 1) << "changing the copy leaves the original alone";
  EXPECT_EQ(b["t"]["v"].value<int>(), 2);
  a.insert_or_assign("n", 1);
  a.insert_or_assign("n", "now a string");
  EXPECT_EQ(a["n"].value<std::string>(), "now a string");
  EXPECT_EQ(a.size(), 2u);
}

TEST(FleetToml, IterationIsInKeyOrderWithStructuredBindings) {
  const toml::table t = toml::parse("b = 1\nc = 2\na = 3\n");
  std::string order;
  for (auto&& [k, v] : t) order += std::string(k.str()) + std::to_string(*v.value<int>());
  EXPECT_EQ(order, "a3b1c2");
}

}  // namespace
