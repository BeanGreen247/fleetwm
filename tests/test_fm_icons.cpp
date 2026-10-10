#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <set>

#include "icons.hpp"

using namespace fleetwm::fm;

namespace {
// FNV-1a over the pixels, so two renderings can be compared without keeping both.
uint64_t pixel_hash(cairo_surface_t* s) {
  cairo_surface_flush(s);
  const unsigned char* d = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s), h = cairo_image_surface_get_height(s), w = cairo_image_surface_get_width(s);
  uint64_t x = 1469598103934665603ull;
  for (int y = 0; y < h; ++y)
    for (int i = 0; i < w * 4; ++i) x = (x ^ d[y * stride + i]) * 1099511628211ull;
  return x;
}
uint32_t pixel(cairo_surface_t* s, int x, int y) {
  cairo_surface_flush(s);
  return *reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + x * 4);
}
double coverage(cairo_surface_t* s) {
  const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
  int on = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if ((pixel(s, x, y) >> 24) > 16) ++on;
  return static_cast<double>(on) / (w * h);
}
}  // namespace

TEST(FmIcons, EveryKindDrawsSomethingAtEverySizeAndLeavesTheCornersClear) {
  IconCache cache;
  for (int k = 0; k < static_cast<int>(IconKind::Count); ++k)
    for (int size : {16, 24, 32, 48, 96, 256}) {
      cairo_surface_t* s = cache.get(static_cast<IconKind>(k), size);
      ASSERT_EQ(cairo_surface_status(s), CAIRO_STATUS_SUCCESS);
      const double c = coverage(s);
      EXPECT_GT(c, 0.12) << icon_kind_name(static_cast<IconKind>(k)) << " @" << size;
      EXPECT_LT(c, 0.97) << icon_kind_name(static_cast<IconKind>(k)) << " @" << size;
      EXPECT_EQ(pixel(s, 0, 0) >> 24, 0u) << icon_kind_name(static_cast<IconKind>(k)) << " top-left corner";
      EXPECT_EQ(pixel(s, size - 1, size - 1) >> 24, 0u) << icon_kind_name(static_cast<IconKind>(k)) << " bottom-right corner";
    }
}

TEST(FmIcons, KindsLookDifferentFromEachOther) {
  std::set<uint64_t> hashes;
  IconCache cache;
  for (int k = 0; k < static_cast<int>(IconKind::Count); ++k) hashes.insert(pixel_hash(cache.get(static_cast<IconKind>(k), 48)));
  EXPECT_EQ(hashes.size(), static_cast<size_t>(IconKind::Count));
}

TEST(FmIcons, DrawingIsDeterministic) {
  IconCache a, b;
  for (IconKind k : {IconKind::App, IconKind::Folder, IconKind::Image, IconKind::DriveUsb})
    EXPECT_EQ(pixel_hash(a.get(k, 64)), pixel_hash(b.get(k, 64)));
}

TEST(FmIcons, TheApplicationIconCombinesFolderWindowsAndFinderCues) {
  IconCache cache;
  cairo_surface_t* s = cache.get(IconKind::App, 256);
  EXPECT_GT(coverage(s), 0.55);
  // A light pane from the four-pane Windows reference is visible in the folder face.
  const uint32_t pane = pixel(s, 256 * 25 / 100, 256 * 47 / 100);
  EXPECT_GT((pane >> 16) & 255, 180);
  EXPECT_GT((pane >> 8) & 255, 180);
  EXPECT_GT(pane & 255, 180);
  // The folder face remains blue and the mark is not the stock amber folder.
  const uint32_t q = pixel(s, 40, 200);
  EXPECT_GT(q & 255, (q >> 16) & 255) << "front panel is blue";
}

TEST(FmIcons, TheAppIconIsNotTheWindowsFolder) {
  IconCache cache;
  // the standard folder is amber (red > blue); ours is blue (blue > red)
  cairo_surface_t* app = cache.get(IconKind::App, 128);
  cairo_surface_t* fold = cache.get(IconKind::Folder, 128);
  const uint32_t a = pixel(app, 20, 100), f = pixel(fold, 20, 90);
  EXPECT_GT(a & 255, (a >> 16) & 255);
  EXPECT_GT((f >> 16) & 255, f & 255);
}

TEST(FmIcons, CacheHitsMissesAndEviction) {
  IconCache cache(3);
  cairo_surface_t* a = cache.get(IconKind::Folder, 16);
  EXPECT_EQ(cache.get(IconKind::Folder, 16), a);
  EXPECT_EQ(cache.hits(), 1u);
  EXPECT_EQ(cache.misses(), 1u);
  cache.get(IconKind::File, 16);
  cache.get(IconKind::Text, 16);
  cache.get(IconKind::Folder, 16);  // refresh: File is now the oldest
  cache.get(IconKind::Image, 16);
  EXPECT_EQ(cache.size(), 3u);
  const uint64_t before = cache.misses();
  cache.get(IconKind::Folder, 16);
  EXPECT_EQ(cache.misses(), before) << "the recently used icon survived";
  cache.get(IconKind::File, 16);
  EXPECT_EQ(cache.misses(), before + 1) << "the least recently used one was evicted";
  cache.clear();
  EXPECT_EQ(cache.size(), 0u);
}

TEST(FmIcons, FilesGetTheIconOfTheirType) {
  EXPECT_EQ(icon_for_file("photo.JPG", false), IconKind::Image);
  EXPECT_EQ(icon_for_file("song.flac", false), IconKind::Audio);
  EXPECT_EQ(icon_for_file("movie.mkv", false), IconKind::Video);
  EXPECT_EQ(icon_for_file("backup.tar.gz", false), IconKind::Archive);
  EXPECT_EQ(icon_for_file("main.cpp", false), IconKind::Code);
  EXPECT_EQ(icon_for_file("Makefile", false), IconKind::Code);
  EXPECT_EQ(icon_for_file("report.pdf", false), IconKind::Pdf);
  EXPECT_EQ(icon_for_file("notes.txt", false), IconKind::Text);
  EXPECT_EQ(icon_for_file("sheet.xlsx", false), IconKind::Spreadsheet);
  EXPECT_EQ(icon_for_file("setup.AppImage", false), IconKind::Executable);
  EXPECT_EQ(icon_for_file("disk.iso", false), IconKind::Disc);
  EXPECT_EQ(icon_for_file("unknown.zzz", false), IconKind::File);
  EXPECT_EQ(icon_for_file("noext", false), IconKind::File);
  EXPECT_EQ(icon_for_file("anything.jpg", true), IconKind::Folder);
  EXPECT_EQ(icon_for_folder_name("Downloads"), IconKind::Downloads);
  EXPECT_EQ(icon_for_folder_name("Pictures"), IconKind::Pictures);
  EXPECT_EQ(icon_for_folder_name("src"), IconKind::Folder);
}

TEST(FmIcons, TypeColumnReadsLikeExplorer) {
  EXPECT_EQ(type_description("a.txt", false), "Text Document");
  EXPECT_EQ(type_description("a.jpg", false), "JPEG image");
  EXPECT_EQ(type_description("dir", true), "File folder");
  EXPECT_EQ(type_description("a.zzz", false), "ZZZ File");
  EXPECT_EQ(type_description("noext", false), "File");
  EXPECT_EQ(type_description("a.zip", false), "Compressed (zipped) Folder");
}

TEST(FmIcons, NamesAreUniqueAndComplete) {
  std::set<std::string> names;
  for (int k = 0; k < static_cast<int>(IconKind::Count); ++k) names.insert(icon_kind_name(static_cast<IconKind>(k)));
  EXPECT_EQ(names.size(), static_cast<size_t>(IconKind::Count));
}

TEST(FmIcons, BadgesDrawInTheirCornerOnly) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
  cairo_t* cr = cairo_create(s);
  draw_link_badge(cr, 64);
  cairo_destroy(cr);
  EXPECT_GT(pixel(s, 14, 52) >> 24, 100u);
  EXPECT_EQ(pixel(s, 56, 8) >> 24, 0u);
  cairo_surface_destroy(s);
  s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
  cr = cairo_create(s);
  draw_lock_badge(cr, 64);
  cairo_destroy(cr);
  EXPECT_GT(pixel(s, 51, 52) >> 24, 100u);
  EXPECT_EQ(pixel(s, 8, 8) >> 24, 0u);
  cairo_surface_destroy(s);
}

TEST(FmIcons, WritesAPngForTheDesktopEntry) {
  const std::string path = (std::filesystem::temp_directory_path() / "fm-icon-test.png").string();
  ASSERT_TRUE(write_icon_png(IconKind::App, 128, path));
  EXPECT_GT(std::filesystem::file_size(path), 500u);
  cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
  EXPECT_EQ(cairo_surface_status(s), CAIRO_STATUS_SUCCESS);
  EXPECT_EQ(cairo_image_surface_get_width(s), 128);
  cairo_surface_destroy(s);
  std::filesystem::remove(path);
  EXPECT_FALSE(write_icon_png(IconKind::App, 128, "/nonexistent-dir/x.png"));
}
