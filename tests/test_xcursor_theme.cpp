#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "xcursor_theme.hpp"

namespace fleetwm {
namespace {
namespace fs = std::filesystem;

uint32_t rd(const std::vector<unsigned char>& b, size_t at) { return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24); }

TEST(XcursorFile, HasTheHeaderTableAndImageChunks) {
  XcursorFrame a;
  a.nominal_size = 32; a.width = 2; a.height = 2; a.xhot = 1; a.yhot = 0; a.argb = {1, 2, 3, 4};
  XcursorFrame b;
  b.nominal_size = 64; b.width = 3; b.height = 1; b.delay_ms = 83; b.argb = {5, 6, 7};
  const auto f = xcursor_file({a, b});
  EXPECT_EQ(rd(f, 0), 0x72756358u);  // "Xcur"
  EXPECT_EQ(rd(f, 4), 16u);
  EXPECT_EQ(rd(f, 12), 2u);          // two images
  EXPECT_EQ(rd(f, 16), 0xfffd0002u); // table entry: image, nominal size, position
  EXPECT_EQ(rd(f, 20), 32u);
  const uint32_t pos_a = rd(f, 24), pos_b = rd(f, 36);
  EXPECT_EQ(pos_a, 16u + 24u);
  EXPECT_EQ(pos_b, pos_a + 36u + 16u);
  EXPECT_EQ(rd(f, pos_a + 16), 2u);  // width
  EXPECT_EQ(rd(f, pos_a + 24), 1u);  // xhot
  EXPECT_EQ(rd(f, pos_a + 36), 1u);  // first pixel
  EXPECT_EQ(rd(f, pos_b + 32), 83u); // delay
  EXPECT_EQ(f.size(), pos_b + 36u + 12u);
}

TEST(XcursorTheme, WritesEveryNamedCursorAndReusesIt) {
  const fs::path dir = fs::temp_directory_path() / ("fleetwm-xcursor-" + std::to_string(::getpid()));
  fs::create_directories(dir);
  setenv("XDG_DATA_HOME", dir.c_str(), 1);
  EXPECT_EQ(install_xcursor_theme(true, "breeze_cursors"), "fleetwm-aero");
  for (const char* n : {"left_ptr", "text", "wait", "pointer", "ew-resize", "not-allowed"}) EXPECT_TRUE(fs::exists(dir / "icons/fleetwm-aero/cursors" / n)) << n;
  std::ifstream idx(dir / "icons/fleetwm-aero/index.theme");
  std::string all((std::istreambuf_iterator<char>(idx)), {});
  EXPECT_NE(all.find("Inherits=breeze_cursors"), std::string::npos);
  const auto t = fs::last_write_time(dir / "icons/fleetwm-aero/cursors/left_ptr");
  EXPECT_EQ(install_xcursor_theme(true, "breeze_cursors"), "fleetwm-aero");  // same stamp: nothing rewritten
  EXPECT_EQ(fs::last_write_time(dir / "icons/fleetwm-aero/cursors/left_ptr"), t);
  EXPECT_EQ(install_xcursor_theme(false, ""), "fleetwm-flat");
  fs::remove_all(dir);
}

}  // namespace
}  // namespace fleetwm
