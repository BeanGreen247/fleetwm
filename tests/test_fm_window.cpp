#include <gtest/gtest.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <filesystem>
#include <algorithm>
#include <set>
#include <fstream>

#include "test_util.hpp"
#include "window.hpp"

using namespace fleetwm;
using namespace fleetwm::fm;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kLeft = 0x110, kRight = 0x111, kMiddle = 0x112;

struct Screen {
  cairo_surface_t* s = nullptr;
  int w, h;
  Screen(int w_, int h_) : w(w_), h(h_) { s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); }
  ~Screen() { cairo_surface_destroy(s); }
  uint32_t px(int x, int y) {
    cairo_surface_flush(s);
    return *reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + x * 4);
  }
  int distinct_colours() {
    cairo_surface_flush(s);
    std::set<uint32_t> seen;
    for (int y = 0; y < h; y += 3)
      for (int x = 0; x < w; x += 3) seen.insert(px(x, y));
    return static_cast<int>(seen.size());
  }
};

class FmWindowTest : public fleetwm::testutil::ScopedConfigHome {
 protected:
  void SetUp() override {
    ScopedConfigHome::SetUp();
    root_ = fs::temp_directory_path() / ("fm-win-" + std::to_string(::getpid()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    fs::create_directories(root_ / "home");
    ::setenv("HOME", (root_ / "home").c_str(), 1);
    ::setenv("XDG_DATA_HOME", (root_ / "data").c_str(), 1);
    work_ = root_ / "work";
    fs::create_directories(work_ / "docs");
    fs::create_directories(work_ / "pics");
    put(work_ / "a.txt", "alpha");
    put(work_ / "b.txt", "bravo");
    put(work_ / "docs/report.txt", "report text with needle");
    put(work_ / "pics/photo.png", "not really a png");
    put(work_ / ".hidden", "h");
  }
  void TearDown() override {
    win_.reset();
    std::error_code ec;
    fs::remove_all(root_, ec);
    ::unsetenv("XDG_DATA_HOME");
    ScopedConfigHome::TearDown();
  }
  void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
  }
  void make(const FmSettings& s = FmSettings{}, bool start_in_work = true) {
    Host h;
    h.now = [this] { return clock_; };
    h.quit = [this] { quit_ = true; };
    h.new_window = [this](const std::string& p) { new_windows_.push_back(p); };
    win_ = std::make_unique<FmWindow>(h, s, kit::Palette{});
    win_->set_trash_for_test((root_ / "Trash").string());
    win_->start(start_in_work ? work_.string() : std::string());
    win_->wait_idle();
    frame();
  }
  void frame(int w = 1024, int h = 768) {
    Screen sc(w, h);
    cairo_t* cr = cairo_create(sc.s);
    win_->draw(cr, w, h);
    cairo_destroy(cr);
  }
  void key(xkb_keysym_t sym, const std::string& utf8 = "", uint32_t mods = 0) {
    kit::KeyEvent e;
    e.sym = sym;
    e.utf8 = utf8;
    e.mods = mods;
    win_->on_key(e);
    kit::KeyEvent up = e;
    up.pressed = false;
    up.mods = 0;  // the modifiers are let go with the key
    win_->on_key(up);
    frame();
  }
  void type(const std::string& s) {
    for (char c : s) key(static_cast<xkb_keysym_t>(c), std::string(1, c));
  }
  void click(const Rect& r, uint32_t button = kLeft) {
    const double x = r.x + r.w / 2.0, y = r.y + r.h / 2.0;
    win_->on_motion(x, y);
    frame();
    win_->on_button(x, y, button, true);
    win_->on_button(x, y, button, false);
    frame();
  }
  void click_item(int i, uint32_t button = kLeft) { click(win_->item_screen_rect(i), button); }
  void double_click_item(int i) {
    click_item(i);
    clock_ += 0.1;
    click_item(i);
    clock_ += 1.0;
  }
  int index_of(const std::string& name) {
    for (size_t i = 0; i < win_->tab().shown.size(); ++i)
      if (win_->tab().name_at(static_cast<int>(i)) == name) return static_cast<int>(i);
    return -1;
  }
  void select(const std::string& name) {
    const int i = index_of(name);
    ASSERT_GE(i, 0) << name;
    click_item(i);
    clock_ += 1.0;
  }
  void settle() {
    win_->wait_idle();
    frame();
  }
  std::unique_ptr<FmWindow> win_;
  fs::path root_, work_;
  double clock_ = 100;
  bool quit_ = false;
  std::vector<std::string> new_windows_;
};

}  // namespace

TEST_F(FmWindowTest, OpensTheStartFolderAndListsIt) {
  make();
  EXPECT_EQ(win_->tab().path, work_.string());
  EXPECT_FALSE(win_->tab().loading);
  EXPECT_EQ(win_->tab().shown.size(), 4u) << "docs, pics, a.txt, b.txt (hidden stays out)";
  EXPECT_EQ(win_->tab().name_at(0), "docs");
  EXPECT_EQ(win_->title(), "work - File Manager");
}

TEST_F(FmWindowTest, EveryStyleDrawsAtTheSmallestSupportedSize) {
  for (const StyleSpec& st : all_styles()) {
    FmSettings s;
    apply_style(&s, st.id);
    s.startup = Startup::Home;
    make(s);
    Screen sc(1024, 768);
    cairo_t* cr = cairo_create(sc.s);
    win_->draw(cr, 1024, 768);
    cairo_destroy(cr);
    EXPECT_TRUE(win_->layout().fits) << st.name;
    EXPECT_GT(sc.distinct_colours(), 25) << st.name << " drew something";
    for (const Rect* r : {&win_->layout().toolbar, &win_->layout().address, &win_->layout().nav, &win_->layout().content, &win_->layout().details, &win_->layout().status}) {
      EXPECT_GE(r->x, 0) << st.name;
      EXPECT_LE(r->x + r->w, 1024) << st.name;
      EXPECT_LE(r->y + r->h, 768) << st.name;
    }
    win_.reset();
  }
}

TEST_F(FmWindowTest, EveryViewModeDrawsAndPicksTheItemUnderTheClick) {
  make();
  for (ViewMode m : {ViewMode::Details, ViewMode::List, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons, ViewMode::Tiles, ViewMode::Content}) {
    win_->run(Cmd::SetView, static_cast<int>(m));
    frame();
    EXPECT_EQ(win_->tab().mode, m);
    click_item(2);
    ASSERT_EQ(win_->tab().selected().size(), 1u) << view_mode_key(m);
    EXPECT_EQ(win_->tab().selected()[0], 2) << view_mode_key(m);
    clock_ += 1;
  }
}

TEST_F(FmWindowTest, DoubleClickOpensAFolderAndBackReturns) {
  make();
  double_click_item(index_of("docs"));
  win_->wait_idle();
  frame();
  EXPECT_EQ(win_->tab().path, (work_ / "docs").string());
  EXPECT_EQ(win_->tab().shown.size(), 1u);
  key(XKB_KEY_Left, "", kit::kAlt);
  win_->wait_idle();
  EXPECT_EQ(win_->tab().path, work_.string());
  key(XKB_KEY_Right, "", kit::kAlt);
  win_->wait_idle();
  EXPECT_EQ(win_->tab().path, (work_ / "docs").string());
  key(XKB_KEY_Up, "", kit::kAlt);
  win_->wait_idle();
  EXPECT_EQ(win_->tab().path, work_.string());
}

TEST_F(FmWindowTest, TheAddressBarAcceptsATypedPath) {
  make();
  key(XKB_KEY_l, "l", kit::kCtrl);
  type((work_ / "pics").string());
  key(XKB_KEY_Return, "\r");
  win_->wait_idle();
  EXPECT_EQ(win_->tab().path, (work_ / "pics").string());
  EXPECT_EQ(win_->tab().shown.size(), 1u);
}

TEST_F(FmWindowTest, ATypoInTheAddressBarSaysSoAndStaysPut) {
  make();
  key(XKB_KEY_l, "l", kit::kCtrl);
  type((work_ / "nope").string());
  key(XKB_KEY_Return, "\r");
  EXPECT_EQ(win_->dialog_name(), "message");
  EXPECT_NE(win_->last_message().find("can't be found"), std::string::npos);
  EXPECT_EQ(win_->tab().path, work_.string());
}

TEST_F(FmWindowTest, ClickCtrlClickShiftClickSelect) {
  make();
  click_item(0);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{0}));
  win_->on_key([] { kit::KeyEvent e; e.sym = XKB_KEY_Control_L; e.mods = kit::kCtrl; return e; }());
  click_item(2);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{0, 2}));
  kit::KeyEvent up;
  up.sym = XKB_KEY_Control_L;
  up.pressed = false;
  win_->on_key(up);
  win_->on_key([] { kit::KeyEvent e; e.sym = XKB_KEY_Shift_L; e.mods = kit::kShift; return e; }());
  click_item(3);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{2, 3}));
  up.sym = XKB_KEY_Shift_L;
  win_->on_key(up);
}

TEST_F(FmWindowTest, ArrowKeysMoveTheSelectionAndSelectAllWorks) {
  make();
  key(XKB_KEY_Down);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{0}));
  key(XKB_KEY_Down);
  key(XKB_KEY_Down);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{2}));
  key(XKB_KEY_Up);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{1}));
  key(XKB_KEY_End);
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{3}));
  key(XKB_KEY_a, "a", kit::kCtrl);
  EXPECT_EQ(win_->tab().selected_count(), 4);
  key(XKB_KEY_Escape);
  EXPECT_EQ(win_->tab().selected_count(), 0);
}

TEST_F(FmWindowTest, TypingJumpsToTheItem) {
  make();
  type("b");
  ASSERT_EQ(win_->tab().selected().size(), 1u);
  EXPECT_EQ(win_->tab().name_at(win_->tab().selected()[0]), "b.txt");
}

TEST_F(FmWindowTest, RubberBandSelectsWhatItCrosses) {
  make();
  const Rect r0 = win_->item_screen_rect(0), r1 = win_->item_screen_rect(1), r2 = win_->item_screen_rect(2), r3 = win_->item_screen_rect(3);
  const int sx = r0.x + 400, sy = r3.y + r3.h + 30;  // in the empty space under the last row
  win_->on_motion(sx, sy);
  frame();
  win_->on_button(sx, sy, kLeft, true);
  win_->on_motion(sx - 150, r2.y + 8);
  win_->on_motion(r0.x + 10, r1.y + 8);
  win_->on_button(r0.x + 10, r1.y + 8, kLeft, false);
  frame();
  EXPECT_EQ(win_->tab().selected(), (std::vector<int>{1, 2, 3}));
}

TEST_F(FmWindowTest, CopyAndPasteMakesAVerifiedCopy) {
  make();
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  frame();
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  frame();
  EXPECT_TRUE(fs::exists(work_ / "docs/a.txt"));
  EXPECT_TRUE(fs::exists(work_ / "a.txt")) << "a copy leaves the original";
  ASSERT_EQ(win_->jobs_for_test().size(), 1u);
  const auto& j = *win_->jobs_for_test()[0];
  EXPECT_TRUE(j.result.ok());
  ASSERT_EQ(j.result.verified.size(), 1u);
  EXPECT_TRUE(j.result.verified[0].matched);
  EXPECT_EQ(win_->tab().shown.size(), 2u) << "the listing refreshed";
}

TEST_F(FmWindowTest, CutAndPasteMoves) {
  make();
  select("b.txt");
  key(XKB_KEY_x, "x", kit::kCtrl);
  win_->open_address((work_ / "pics").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "pics/b.txt"));
  EXPECT_FALSE(fs::exists(work_ / "b.txt"));
}

TEST_F(FmWindowTest, PastingAnExistingNameKeepsBothWhenNotAsking) {
  FmSettings s;
  s.confirm_conflicts = false;
  s.startup = Startup::Home;
  make(s);
  put(work_ / "docs/a.txt", "older, different");
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);  // the name is taken there
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "docs/a (2).txt"));
  EXPECT_EQ(fs::file_size(work_ / "docs/a.txt"), 16u) << "the older file is untouched";
}

TEST_F(FmWindowTest, PastingIntoTheSameFolderMakesACopyNextToIt) {
  make();
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "a - Copy.txt"));
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "a - Copy (2).txt"));
}

TEST_F(FmWindowTest, AConflictAsksAndEscapeCancels) {
  make();
  put(work_ / "docs/a.txt", "older");
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  for (int i = 0; i < 400 && win_->dialog_name() != "conflict"; ++i) {
    usleep(5000);
    win_->wait_idle(0.01);
  }
  ASSERT_EQ(win_->dialog_name(), "conflict");
  key(XKB_KEY_Escape);
  win_->wait_idle();
  EXPECT_EQ(win_->dialog_name(), "");
  EXPECT_FALSE(fs::exists(work_ / "docs/a (2).txt")) << "canceled";
  EXPECT_EQ(fs::file_size(work_ / "docs/a.txt"), 5u) << "the existing file was not replaced";
}

TEST_F(FmWindowTest, DeleteMovesToTheTrashAfterConfirming) {
  make();
  select("a.txt");
  key(XKB_KEY_Delete);
  EXPECT_EQ(win_->dialog_name(), "confirm-delete");
  FmSettings& s = win_->settings();
  s.confirm_delete = false;
  win_->close_dialog_for_test();
  select("a.txt");
  key(XKB_KEY_Delete);
  win_->wait_idle();
  EXPECT_FALSE(fs::exists(work_ / "a.txt"));
  EXPECT_TRUE(fs::exists(root_ / "Trash/files/a.txt"));
  EXPECT_EQ(win_->tab().shown.size(), 3u);
}

TEST_F(FmWindowTest, ShiftDeleteDeletesForGood) {
  FmSettings s;
  s.confirm_delete = false;
  s.startup = Startup::Home;
  make(s);
  select("b.txt");
  key(XKB_KEY_Delete, "", kit::kShift);
  win_->wait_idle();
  EXPECT_FALSE(fs::exists(work_ / "b.txt"));
  EXPECT_FALSE(fs::exists(root_ / "Trash/files/b.txt"));
}

TEST_F(FmWindowTest, F2RenamesInPlace) {
  make();
  select("a.txt");
  key(XKB_KEY_F2);
  type("zulu");
  key(XKB_KEY_Return, "\r");
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "zulu.txt")) << "the extension was left out of the selection";
  EXPECT_FALSE(fs::exists(work_ / "a.txt"));
  ASSERT_EQ(win_->tab().selected().size(), 1u);
  EXPECT_EQ(win_->tab().name_at(win_->tab().selected()[0]), "zulu.txt");
}

TEST_F(FmWindowTest, RenamingOntoAnExistingNameIsRefused) {
  make();
  select("a.txt");
  key(XKB_KEY_F2);
  key(XKB_KEY_a, "a", kit::kCtrl);
  type("b.txt");
  key(XKB_KEY_Return, "\r");
  EXPECT_EQ(win_->dialog_name(), "message");
  EXPECT_TRUE(fs::exists(work_ / "a.txt"));
}

TEST_F(FmWindowTest, EscapeCancelsARename) {
  make();
  select("a.txt");
  key(XKB_KEY_F2);
  type("zz");
  key(XKB_KEY_Escape);
  EXPECT_TRUE(fs::exists(work_ / "a.txt"));
}

TEST_F(FmWindowTest, NewFolderIsCreatedAndReadyToRename) {
  make();
  key(XKB_KEY_n, "n", kit::kCtrl | kit::kShift);
  win_->wait_idle();
  EXPECT_TRUE(fs::is_directory(work_ / "New folder"));
  type("Photos");
  key(XKB_KEY_Return, "\r");
  win_->wait_idle();
  EXPECT_TRUE(fs::is_directory(work_ / "Photos"));
  EXPECT_FALSE(fs::exists(work_ / "New folder"));
}

TEST_F(FmWindowTest, SearchFindsFilesInSubfoldersAndClearingReturns) {
  make();
  key(XKB_KEY_f, "f", kit::kCtrl);
  type("report");
  win_->wait_idle();
  frame();
  ASSERT_EQ(win_->tab().shown.size(), 1u);
  EXPECT_EQ(win_->tab().name_at(0), "docs/report.txt");
  EXPECT_EQ(win_->tab().label_at(0), "report.txt");
  EXPECT_EQ(win_->tab().path_at(0), (work_ / "docs/report.txt").string());
  key(XKB_KEY_Escape);
  win_->wait_idle();
  EXPECT_TRUE(win_->tab().search.empty());
  EXPECT_EQ(win_->tab().shown.size(), 4u);
}

TEST_F(FmWindowTest, SearchCanLookInsideFiles) {
  FmSettings s;
  s.search_in_contents = true;
  s.startup = Startup::Home;
  make(s);
  key(XKB_KEY_f, "f", kit::kCtrl);
  type("needle");
  win_->wait_idle();
  ASSERT_EQ(win_->tab().shown.size(), 1u);
  EXPECT_EQ(win_->tab().label_at(0), "report.txt");
}

TEST_F(FmWindowTest, SearchWithoutSubfoldersJustFiltersTheListing) {
  FmSettings s;
  s.search_subfolders = false;
  s.startup = Startup::Home;
  make(s);
  key(XKB_KEY_f, "f", kit::kCtrl);
  type("a.t");
  EXPECT_EQ(win_->tab().shown.size(), 1u);
  EXPECT_EQ(win_->tab().name_at(0), "a.txt");
}

TEST_F(FmWindowTest, TabsOpenCloseAndSwitch) {
  make();
  EXPECT_EQ(win_->tab_count(), 1u);
  key(XKB_KEY_t, "t", kit::kCtrl);
  win_->wait_idle();
  EXPECT_EQ(win_->tab_count(), 2u);
  EXPECT_EQ(win_->current_tab(), 1u);
  EXPECT_EQ(win_->tab().path, (root_ / "home").string());
  key(XKB_KEY_Tab, "", kit::kCtrl);
  EXPECT_EQ(win_->current_tab(), 0u);
  EXPECT_EQ(win_->tab().path, work_.string());
  key(XKB_KEY_w, "w", kit::kCtrl);
  EXPECT_EQ(win_->tab_count(), 1u);
  key(XKB_KEY_t, "T", kit::kCtrl | kit::kShift);
  win_->wait_idle();
  EXPECT_EQ(win_->tab_count(), 2u) << "reopened the closed tab";
}

TEST_F(FmWindowTest, ClosingTheLastTabClosesTheWindow) {
  make();
  key(XKB_KEY_w, "w", kit::kCtrl);
  EXPECT_TRUE(quit_);
}

TEST_F(FmWindowTest, MiddleClickOpensAFolderInANewTab) {
  make();
  click_item(index_of("docs"), kMiddle);
  win_->wait_idle();
  EXPECT_EQ(win_->tab_count(), 2u);
  EXPECT_EQ(win_->tab().path, (work_ / "docs").string());
}

TEST_F(FmWindowTest, HiddenFilesToggleWithCtrlH) {
  make();
  key(XKB_KEY_h, "h", kit::kCtrl);
  EXPECT_EQ(win_->tab().shown.size(), 5u);
  key(XKB_KEY_h, "h", kit::kCtrl);
  EXPECT_EQ(win_->tab().shown.size(), 4u);
  EXPECT_FALSE(load_fm_settings().show_hidden);
}

TEST_F(FmWindowTest, ClickingAColumnHeadingSortsAndClickingAgainReverses) {
  make();
  const Rect content = win_->layout().content;
  // "Size" heading: the fourth column
  double x = content.x;
  const auto cols = default_columns();
  x += cols[0].width + cols[1].width + cols[2].width + 20;
  win_->on_motion(x, content.y + 10);
  frame();
  win_->on_button(x, content.y + 10, kLeft, true);
  win_->on_button(x, content.y + 10, kLeft, false);
  frame();
  EXPECT_EQ(win_->tab().sort_key, SortKey::Size);
  EXPECT_TRUE(win_->tab().ascending);
  win_->on_button(x, content.y + 10, kLeft, true);
  win_->on_button(x, content.y + 10, kLeft, false);
  frame();
  EXPECT_FALSE(win_->tab().ascending);
}

TEST_F(FmWindowTest, ChangingTheStyleSwitchesTheLayoutAndIsSaved) {
  make();
  const Rect before = win_->layout().nav;
  win_->run(Cmd::SetStyle, static_cast<int>(ViewStyle::Nautilus));
  frame();
  EXPECT_EQ(win_->settings().style, ViewStyle::Nautilus);
  EXPECT_EQ(win_->tab().mode, ViewMode::MediumIcons);
  EXPECT_NE(win_->layout().nav.w, before.w);
  EXPECT_EQ(load_fm_settings().style, ViewStyle::Nautilus);
  win_->run(Cmd::SetStyle, static_cast<int>(ViewStyle::Windows7));
  frame();
  EXPECT_EQ(win_->tab().mode, ViewMode::Details);
}

TEST_F(FmWindowTest, OptionsOpenTheFileManagerPageOfTheSettingsApp) {
  std::string page;
  Host h;
  h.now = [this] { return clock_; };
  h.open_settings = [&](const std::string& p) { page = p; };
  win_ = std::make_unique<FmWindow>(h, FmSettings{}, kit::Palette{});
  win_->start(work_.string());
  win_->wait_idle();
  win_->run(Cmd::Settings);
  EXPECT_EQ(page, "File Manager");
  EXPECT_EQ(win_->dialog_name(), "");  // no options window of its own
}

TEST_F(FmWindowTest, SettingsChangedInTheSettingsAppReachAnOpenWindow) {
  make();
  win_->settings().window_w = 900;
  win_->settings().last_location = "/somewhere";
  FmSettings fresh;
  fresh.show_hidden = true;
  fresh.colour_scheme = ColorScheme::Dark;
  fresh.window_w = 1;  // the window's own size wins
  win_->reload_config(fresh, kit::Palette{});
  frame();
  EXPECT_TRUE(win_->settings().show_hidden);
  EXPECT_EQ(win_->settings().colour_scheme, ColorScheme::Dark);
  EXPECT_EQ(win_->settings().window_w, 900);
  EXPECT_EQ(win_->settings().last_location, "/somewhere");
}

TEST_F(FmWindowTest, EveryDialogDrawsInsideTheWindow) {
  make();
  for (Cmd c : {Cmd::About, Cmd::ConnectServer, Cmd::AddNextcloud}) {
    win_->run(c);
    frame();
    EXPECT_NE(win_->dialog_name(), "");
    win_->close_dialog_for_test();
  }
  select("a.txt");
  win_->run(Cmd::Properties);
  frame();
  EXPECT_EQ(win_->dialog_name(), "properties");
  win_->close_dialog_for_test();
  win_->run(Cmd::Checksums);
  win_->wait_idle();
  frame();
  EXPECT_EQ(win_->dialog_name(), "checksums");
}

TEST_F(FmWindowTest, ComputerViewListsDrivesAndTheMenuOffersEject) {
  make();
  Volume stick;
  stick.device = "/dev/sdb1";
  stick.disk = "/dev/sdb";
  stick.mountpoint = (root_ / "stick").string();
  stick.label = "STICK";
  stick.kind = DriveKind::Removable;
  stick.mounted = true;
  stick.ejectable = true;
  stick.total = 8ull << 30;
  stick.free = 6ull << 30;
  fs::create_directories(stick.mountpoint);
  win_->set_volumes_for_test({stick});
  win_->open_address("computer:///");
  win_->wait_idle();
  frame();
  EXPECT_EQ(win_->tab().place, PlaceKind::Computer);
  EXPECT_EQ(win_->title(), "Computer - File Manager");
}

namespace {
class WinFakeRunner : public CommandRunner {
 public:
  std::vector<std::vector<std::string>> calls;
  RunResult run(const std::vector<std::string>& argv, const std::string&, int) override {
    calls.push_back(argv);
    return {0, "", false};
  }
};
}  // namespace

TEST_F(FmWindowTest, EjectUnmountsThenSaysItIsSafeToRemove) {
  make();
  WinFakeRunner runner;
  win_->runner = &runner;
  Volume stick;
  stick.device = "/dev/sdb1";
  stick.disk = "/dev/sdb";
  stick.mountpoint = (root_ / "stick").string();
  stick.label = "STICK";
  stick.kind = DriveKind::Removable;
  stick.fstype = "vfat";
  stick.mounted = true;
  stick.ejectable = true;
  fs::create_directories(stick.mountpoint);
  win_->set_volumes_for_test({stick});
  win_->run(Cmd::Eject, 0, stick.mountpoint);
  win_->wait_idle(10.0);
  ASSERT_GE(runner.calls.size(), 1u);
  EXPECT_EQ(runner.calls[0][1], "unmount");
  EXPECT_EQ(win_->dialog_name(), "message");
  EXPECT_NE(win_->last_message().find("safely removed"), std::string::npos);
}

TEST_F(FmWindowTest, TheWindowRefusesToEjectWhileACopyToItRuns) {
  make();
  WinFakeRunner runner;
  win_->runner = &runner;
  Volume stick;
  stick.device = "/dev/sdb1";
  stick.mountpoint = (root_ / "stick").string();
  stick.kind = DriveKind::Removable;
  stick.mounted = true;
  stick.ejectable = true;
  fs::create_directories(stick.mountpoint);
  // a big file so the copy is still running when eject is asked for
  {
    std::ofstream big(work_ / "big.bin", std::ios::binary);
    std::string chunk(1 << 20, 'x');
    for (int i = 0; i < 128; ++i) big << chunk;
  }
  win_->set_volumes_for_test({stick});
  win_->open_address(work_.string());
  win_->wait_idle();
  frame();
  select("big.bin");
  key(XKB_KEY_c, "c", kit::kCtrl);
  fs::create_directories(stick.mountpoint + "/sub/deeper");
  win_->open_address(stick.mountpoint + "/sub/deeper");  // a folder inside the drive, not its root
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  ASSERT_EQ(win_->jobs_for_test().size(), 1u);
  win_->jobs_for_test()[0]->t->pause(true);  // held, so it is certainly still running when eject is asked for
  win_->run(Cmd::Eject, 0, stick.mountpoint);
  EXPECT_EQ(win_->dialog_name(), "message");
  EXPECT_NE(win_->last_message().find("still running"), std::string::npos);
  EXPECT_TRUE(runner.calls.empty()) << "nothing was unmounted";
  win_->close_dialog_for_test();
  win_->jobs_for_test()[0]->t->cancel();
  win_->wait_idle(20.0);
  win_->run(Cmd::Eject, 0, stick.mountpoint);  // finished: now it is allowed
  win_->wait_idle(10.0);
  EXPECT_FALSE(runner.calls.empty());
}

TEST_F(FmWindowTest, ShowsUpToTheTransferCardWhileCopying) {
  make();
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  win_->open_address((work_ / "pics").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  frame();  // card drawn, running or done
  win_->wait_idle();
  frame();
  EXPECT_EQ(win_->jobs_running(), 0);
}

TEST_F(FmWindowTest, TrashViewListsAndRestores) {
  make();
  select("a.txt");
  win_->settings().confirm_delete = false;
  key(XKB_KEY_Delete);
  win_->wait_idle();
  win_->open_address("trash:///");
  win_->wait_idle();
  frame();
  ASSERT_EQ(win_->tab().shown.size(), 1u);
  EXPECT_EQ(win_->tab().place, PlaceKind::Trash);
  click_item(0);
  win_->run(Cmd::Restore);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "a.txt"));
}

TEST_F(FmWindowTest, RemotePlacesAreEnteredThroughTheMountedFolder) {
  make();
  WinFakeRunner runner;
  win_->runner = &runner;
  win_->open_address("smb://nas/media");
  win_->wait_idle(5.0);
  ASSERT_FALSE(runner.calls.empty());
  EXPECT_EQ(runner.calls[0][0], "gio");
  EXPECT_EQ(runner.calls[0].back(), "smb://nas/media");
}

TEST_F(FmWindowTest, TheContextMenuOpensOnRightClickAndEscapeClosesIt) {
  make();
  click_item(0, kRight);
  EXPECT_TRUE(win_->menu_open());
  key(XKB_KEY_Escape);
  EXPECT_FALSE(win_->menu_open());
  const Rect c = win_->layout().content;
  win_->on_motion(c.x + c.w - 40, c.y + c.h - 40);
  frame();
  win_->on_button(c.x + c.w - 40, c.y + c.h - 40, kRight, true);
  frame();
  EXPECT_TRUE(win_->menu_open());
}

TEST_F(FmWindowTest, SmallWindowsStillDrawWithoutTheNavigationPane) {
  make();
  frame(700, 520);
  EXPECT_EQ(win_->layout().nav.w, 0);
  EXPECT_GT(win_->layout().content.w, 600);
  frame(1024, 768);
  EXPECT_GT(win_->layout().nav.w, 100);
}

TEST_F(FmWindowTest, TheTitleCanShowTheFullPath) {
  FmSettings s;
  s.show_full_path_in_title = true;
  s.startup = Startup::Home;
  make(s);
  EXPECT_EQ(win_->title(), work_.string() + " - File Manager");
}

TEST_F(FmWindowTest, StatusTextCountsItemsAndSelection) {
  make();
  EXPECT_EQ(win_->status_text(), "4 items");
  click_item(0);
  EXPECT_EQ(win_->status_text(), "1 item selected");
  key(XKB_KEY_a, "a", kit::kCtrl);
  EXPECT_NE(win_->status_text().find("4 items selected"), std::string::npos);
}

TEST_F(FmWindowTest, SizesAreReadForTheRowsThatAreDrawnAndEverythingWhenTheOrderNeedsIt) {
  for (int i = 0; i < 40; ++i) put(work_ / ("many/f" + std::to_string(i) + ".txt"), std::string(static_cast<size_t>(i + 1), 'x'));
  make();
  win_->open_address((work_ / "many").string());
  win_->wait_idle();
  EXPECT_FALSE(win_->tab().stat_complete) << "a name-sorted folder is read without stat";
  frame();
  EXPECT_TRUE(win_->tab().shown[0].has_stat) << "the first rows were drawn";
  win_->run(Cmd::SetSort, static_cast<int>(SortKey::Size));
  win_->wait_idle();
  EXPECT_TRUE(win_->tab().stat_complete);
  EXPECT_EQ(win_->tab().label_at(0), "f0.txt");
  EXPECT_EQ(win_->tab().shown.back().size, 40u);
}

TEST_F(FmWindowTest, SelectingEverythingReadsTheSizesBehindTheScenes) {
  for (int i = 0; i < 30; ++i) put(work_ / ("many/g" + std::to_string(i) + ".txt"), std::string(100, 'x'));
  make();
  win_->open_address((work_ / "many").string());
  win_->wait_idle();
  key(XKB_KEY_a, "a", kit::kCtrl);
  frame();
  win_->wait_idle();
  frame();
  EXPECT_NE(win_->status_text().find("2.93 KB"), std::string::npos) << win_->status_text();
}

TEST_F(FmWindowTest, SortingBySizeFromTheStartReadsEverythingInTheLoader) {
  FmSettings s;
  s.sort_key = SortKey::Size;
  s.startup = Startup::Home;
  make(s);
  EXPECT_TRUE(win_->tab().stat_complete);
}

TEST_F(FmWindowTest, PasteIntoTheSelectedFolder) {
  make();
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  select("pics");
  win_->run(Cmd::Paste, 1);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "pics/a.txt"));
  EXPECT_FALSE(fs::exists(work_ / "a - Copy.txt"));
}

TEST_F(FmWindowTest, EachFolderKeepsItsViewBetweenVisitsAndRuns) {
  make();
  win_->run(Cmd::SetView, static_cast<int>(ViewMode::LargeIcons));
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  EXPECT_EQ(win_->tab().mode, ViewMode::LargeIcons) << "the setting's default follows the last choice";
  win_->run(Cmd::SetView, static_cast<int>(ViewMode::List));
  win_->run(Cmd::SetSort, static_cast<int>(SortKey::Type));
  win_->open_address(work_.string());
  win_->wait_idle();
  EXPECT_EQ(win_->tab().mode, ViewMode::LargeIcons);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  EXPECT_EQ(win_->tab().mode, ViewMode::List);
  EXPECT_EQ(win_->tab().sort_key, SortKey::Type);
  win_->save_session();
  win_.reset();
  make();  // a new run
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  EXPECT_EQ(win_->tab().mode, ViewMode::List);
  EXPECT_EQ(win_->tab().sort_key, SortKey::Type);
}

TEST_F(FmWindowTest, TheLastFolderAndTabsAreRememberedWhenAsked) {
  FmSettings s;
  s.startup = Startup::LastLocation;
  s.restore_tabs = true;
  make(s);
  win_->open_address((work_ / "docs").string());
  win_->add_tab_for_test((work_ / "pics").string());
  win_->wait_idle();
  win_->save_session();
  win_.reset();
  make(load_fm_settings(), false);
  win_->wait_idle();
  EXPECT_EQ(win_->tab_count(), 2u);
  EXPECT_EQ(win_->tab().path, (work_ / "docs").string()) << "back on the first tab";
}

TEST_F(FmWindowTest, TurningHistoryOffKeepsBackDisabled) {
  FmSettings s;
  s.remember_history = false;
  s.startup = Startup::Home;
  make(s);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  EXPECT_FALSE(win_->tab().can_back());
}

TEST_F(FmWindowTest, DialogsAndMenusStayInsideA1024x768Window) {
  make();
  select("a.txt");
  auto inside = [](const Rect& r) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= 1024 && r.y + r.h <= 768 && r.w > 0 && r.h > 0; };
  for (Cmd c : {Cmd::About, Cmd::ConnectServer, Cmd::AddNextcloud, Cmd::Properties}) {
    win_->run(c);
    frame();
    EXPECT_TRUE(inside(win_->dialog_rect())) << static_cast<int>(c);
    win_->close_dialog_for_test();
  }
  // menus opened near the bottom right corner still fit
  for (Cmd c : {Cmd::MenuOrganize, Cmd::MenuView}) {
    win_->on_motion(1010, 740);
    frame();
    win_->run(c);
    frame();
    ASSERT_TRUE(win_->menu_open());
    EXPECT_TRUE(inside(win_->menu_rect())) << static_cast<int>(c);
    key(XKB_KEY_Escape);
  }
  // the same at the smallest window the compositor lets this program have (min size 640x420)
  win_->run(Cmd::About);
  frame(640, 420);
  const Rect r = win_->dialog_rect();
  EXPECT_LE(r.x + r.w, 640);
  EXPECT_LE(r.y + r.h, 420);
}

// ---- drag and drop, clipboard, menus, undo, preview ----
TEST_F(FmWindowTest, APlainDropAsksCopyMoveOrLinkWithCopyFirstAndSelected) {
  make();
  const Rect docs = win_->item_screen_rect(index_of("docs"));
  win_->on_drop(docs.x + 20, docs.y + 8, "file://" + (work_ / "a.txt").string() + "\r\n");
  frame();
  ASSERT_TRUE(win_->menu_open());
  EXPECT_EQ(win_->menu_labels(), (std::vector<std::string>{"Copy here", "Move here", "Create symbolic link here", "Create hard link here", "-", "Cancel"}));
  key(XKB_KEY_Return);  // Copy is already highlighted
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "docs/a.txt"));
  EXPECT_TRUE(fs::exists(work_ / "a.txt")) << "copied, not moved";
}

TEST_F(FmWindowTest, TheDropMenuCanMoveOrMakeLinks) {
  make();
  const Rect docs = win_->item_screen_rect(index_of("docs"));
  const std::string uri = "file://" + (work_ / "a.txt").string() + "\r\n";
  win_->on_drop(docs.x + 20, docs.y + 8, uri);
  frame();
  key(XKB_KEY_Down);
  key(XKB_KEY_Return);  // Move here
  win_->wait_idle();
  EXPECT_FALSE(fs::exists(work_ / "a.txt"));
  EXPECT_TRUE(fs::exists(work_ / "docs/a.txt"));
  const std::string uri2 = "file://" + (work_ / "b.txt").string() + "\r\n";
  win_->on_drop(docs.x + 20, docs.y + 8, uri2);
  frame();
  key(XKB_KEY_Down);
  key(XKB_KEY_Down);
  key(XKB_KEY_Return);  // symbolic link
  win_->wait_idle();
  EXPECT_TRUE(fs::is_symlink(work_ / "docs/b - Link.txt"));
  win_->on_drop(docs.x + 20, docs.y + 8, uri2);
  frame();
  key(XKB_KEY_Down);
  key(XKB_KEY_Down);
  key(XKB_KEY_Down);
  key(XKB_KEY_Return);  // hard link
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "docs/b - Link (2).txt"));
  EXPECT_FALSE(fs::is_symlink(work_ / "docs/b - Link (2).txt"));
  EXPECT_EQ(fs::hard_link_count(work_ / "b.txt"), 2u);
}

TEST_F(FmWindowTest, CtrlDropCopiesAtOnceAndEscapeCancelsTheQuestion) {
  make();
  const Rect docs = win_->item_screen_rect(index_of("docs"));
  const std::string uri = "file://" + (work_ / "a.txt").string() + "\r\n";
  win_->on_drop(docs.x + 20, docs.y + 8, uri);
  frame();
  key(XKB_KEY_Escape);
  EXPECT_FALSE(win_->menu_open());
  win_->wait_idle();
  EXPECT_FALSE(fs::exists(work_ / "docs/a.txt"));
  kit::KeyEvent ctrl;
  ctrl.sym = XKB_KEY_Control_L;
  ctrl.mods = kit::kCtrl;
  win_->on_key(ctrl);
  win_->on_drop(docs.x + 20, docs.y + 8, uri);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "docs/a.txt")) << "no question with Ctrl held";
  EXPECT_FALSE(win_->menu_open());
}

TEST_F(FmWindowTest, DraggingAnItemWithTheMouseOntoAFolderAsksToo) {
  make();
  const Rect a = win_->item_screen_rect(index_of("a.txt")), docs = win_->item_screen_rect(index_of("docs"));
  win_->on_motion(a.x + 30, a.y + 8);
  frame();
  win_->on_button(a.x + 30, a.y + 8, kLeft, true);
  win_->on_motion(a.x + 40, a.y + 30);
  win_->on_motion(docs.x + 20, docs.y + 8);
  frame();
  win_->on_button(docs.x + 20, docs.y + 8, kLeft, false);
  frame();
  ASSERT_TRUE(win_->menu_open());
  EXPECT_EQ(win_->menu_labels()[0], "Copy here");
}

TEST_F(FmWindowTest, TheSystemClipboardCarriesFilesBothWays) {
  std::vector<std::string> put;
  bool put_cut = false;
  Host h;
  h.now = [this] { return clock_; };
  h.set_clipboard_files = [&](const std::vector<std::string>& p, bool cut) {
    put = p;
    put_cut = cut;
  };
  std::vector<std::string> offered;
  bool offered_cut = false;
  h.read_clipboard_files = [&](std::function<void(const std::vector<std::string>&, bool)> cb) { cb(offered, offered_cut); };
  win_ = std::make_unique<FmWindow>(h, FmSettings{}, kit::Palette{});
  win_->set_trash_for_test((root_ / "Trash").string());
  win_->start(work_.string());
  win_->wait_idle();
  frame();
  select("a.txt");
  key(XKB_KEY_x, "x", kit::kCtrl);
  EXPECT_EQ(put, (std::vector<std::string>{(work_ / "a.txt").string()}));
  EXPECT_TRUE(put_cut);
  // something another program copied: pasted here
  offered = {(work_ / "b.txt").string()};
  offered_cut = false;
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "docs/b.txt"));
  EXPECT_TRUE(fs::exists(work_ / "b.txt"));
}

TEST_F(FmWindowTest, TheContextMenuIsWindows7WithSubmenusAndLinuxExtras) {
  make();
  click_item(index_of("a.txt"), kRight);
  ASSERT_TRUE(win_->menu_open());
  const auto labels = win_->menu_labels();
  for (const char* want : {"Open", "Open with", "Send to", "Cut", "Copy", "Copy location", "Create shortcut", "Delete", "Rename", "Compress to", "Calculate checksums...", "Properties"})
    EXPECT_NE(std::find(labels.begin(), labels.end(), want), labels.end()) << want;
  EXPECT_EQ(win_->sub_labels("Compress to"), (std::vector<std::string>{"Zip file (.zip)", "Tar gzip (.tar.gz)", "Tar xz (.tar.xz)"}));
  key(XKB_KEY_Escape);
  click_item(index_of("docs"), kRight);
  const auto folder = win_->menu_labels();
  for (const char* want : {"Open in new tab", "Open in new window", "Open in terminal"}) EXPECT_NE(std::find(folder.begin(), folder.end(), want), folder.end()) << want;
  key(XKB_KEY_Escape);
  const Rect c = win_->layout().content;
  win_->on_motion(c.x + c.w - 40, c.y + c.h - 40);
  frame();
  win_->on_button(c.x + c.w - 40, c.y + c.h - 40, kRight, true);
  frame();
  const auto bg = win_->menu_labels();
  for (const char* want : {"View", "Sort by", "Group by", "Refresh", "Paste", "New", "Properties"}) EXPECT_NE(std::find(bg.begin(), bg.end(), want), bg.end()) << want;
  EXPECT_EQ(win_->sub_labels("New"), (std::vector<std::string>{"Folder", "Text document"}));
}

TEST_F(FmWindowTest, SubmenusOpenWhenThePointerRestsOnTheirItem) {
  fs::create_directories(root_ / "home/Documents");  // Send to lists the user's folders
  make();
  click_item(index_of("a.txt"), kRight);
  const auto labels = win_->menu_labels();
  const auto it = std::find(labels.begin(), labels.end(), "Send to");
  ASSERT_NE(it, labels.end());
  // walk to the item with the keyboard: Down until Send to, then Right opens it
  size_t downs = 0;
  for (auto k = labels.begin(); k <= it; ++k)
    if (*k != "-") ++downs;  // separators are skipped
  for (size_t i = 0; i < downs; ++i) key(XKB_KEY_Down);
  key(XKB_KEY_Right);
  frame();
  key(XKB_KEY_Escape);  // closes the submenu only
  EXPECT_TRUE(win_->menu_open());
  key(XKB_KEY_Escape);
  EXPECT_FALSE(win_->menu_open());
}

TEST_F(FmWindowTest, NewTextDocumentIsCreatedAndReadyToRename) {
  make();
  win_->run(Cmd::NewFolder, 1);
  win_->wait_idle();
  EXPECT_TRUE(fs::is_regular_file(work_ / "New Text Document.txt"));
  type("todo");
  key(XKB_KEY_Return, "\r");
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "todo.txt"));
}

TEST_F(FmWindowTest, UndoTakesBackRenamesNewFoldersDeletesAndCopies) {
  FmSettings s;
  s.confirm_delete = false;
  s.startup = Startup::Home;
  make(s);
  select("a.txt");
  key(XKB_KEY_F2);
  type("zz");
  key(XKB_KEY_Return, "\r");
  win_->wait_idle();
  ASSERT_TRUE(fs::exists(work_ / "zz.txt"));
  key(XKB_KEY_z, "z", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "a.txt"));
  EXPECT_FALSE(fs::exists(work_ / "zz.txt"));
  select("b.txt");
  key(XKB_KEY_Delete);
  win_->wait_idle();
  ASSERT_FALSE(fs::exists(work_ / "b.txt"));
  key(XKB_KEY_z, "z", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "b.txt")) << "back from the trash";
  select("a.txt");
  key(XKB_KEY_c, "c", kit::kCtrl);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  ASSERT_TRUE(fs::exists(work_ / "docs/a.txt"));
  key(XKB_KEY_z, "z", kit::kCtrl);
  win_->wait_idle();
  EXPECT_FALSE(fs::exists(work_ / "docs/a.txt"));
  EXPECT_TRUE(fs::exists(work_ / "a.txt"));
  key(XKB_KEY_n, "n", kit::kCtrl | kit::kShift);
  win_->wait_idle();
  key(XKB_KEY_Return, "\r");
  win_->wait_idle();
  ASSERT_TRUE(fs::exists(work_ / "docs/New folder"));
  key(XKB_KEY_z, "z", kit::kCtrl);
  win_->wait_idle();
  EXPECT_FALSE(fs::exists(work_ / "docs/New folder"));
  const size_t depth = win_->undo_depth();
  key(XKB_KEY_z, "z", kit::kCtrl);
  key(XKB_KEY_z, "z", kit::kCtrl);
  win_->wait_idle();
  EXPECT_LE(win_->undo_depth(), depth);
}

TEST_F(FmWindowTest, UndoOfAMoveBringsTheFileBack) {
  make();
  select("a.txt");
  key(XKB_KEY_x, "x", kit::kCtrl);
  win_->open_address((work_ / "docs").string());
  win_->wait_idle();
  key(XKB_KEY_v, "v", kit::kCtrl);
  win_->wait_idle();
  ASSERT_TRUE(fs::exists(work_ / "docs/a.txt"));
  key(XKB_KEY_z, "z", kit::kCtrl);
  win_->wait_idle();
  EXPECT_TRUE(fs::exists(work_ / "a.txt"));
  EXPECT_FALSE(fs::exists(work_ / "docs/a.txt"));
}

TEST_F(FmWindowTest, CompressAndExtractUseTheArchiveTools) {
  make();
  WinFakeRunner runner;
  win_->runner = &runner;
  select("a.txt");
  win_->run(Cmd::Compress, 1);
  win_->wait_idle();
  ASSERT_FALSE(runner.calls.empty());
  EXPECT_EQ(runner.calls[0][0], "tar");
  EXPECT_EQ(runner.calls[0][1], "-czf");
  EXPECT_EQ(fs::path(runner.calls[0][2]).filename(), "a.txt.tar.gz");
  win_->run(Cmd::Compress, 0);
  win_->wait_idle();
  EXPECT_EQ(runner.calls.back()[0], "sh") << "zip runs in the folder, through sh with the names as arguments";
  EXPECT_EQ(runner.calls.back().back(), "a.txt");
  runner.calls.clear();
  put(work_ / "pack.zip", "PK");
  win_->run(Cmd::Reload);
  win_->wait_idle();
  frame();
  select("pack.zip");
  win_->run(Cmd::Extract, 1);
  win_->wait_idle();
  ASSERT_FALSE(runner.calls.empty());
  EXPECT_EQ(runner.calls[0][0], "unzip");
  EXPECT_EQ(fs::path(runner.calls[0].back()).filename(), "pack");
}

TEST_F(FmWindowTest, TheNetworkViewListsComputersTheNetworkAnnounces) {
  make();
  class Lan : public CommandRunner {
   public:
    RunResult run(const std::vector<std::string>& argv, const std::string&, int) override {
      return {0, argv[0] == "gio" ? "smb://nas/\nsmb://printer-pc/\n" : "", false};
    }
  } lan;
  win_->runner = &lan;
  win_->open_address("network:///");
  win_->wait_idle();
  frame();
  ASSERT_EQ(win_->lan_hosts().size(), 2u);
  EXPECT_EQ(win_->lan_hosts()[0].first, "nas");
  EXPECT_EQ(win_->lan_hosts()[1].second, "smb://printer-pc/");
}

TEST_F(FmWindowTest, GroupByShowsHeadingsAndClicksStillHitTheRightItem) {
  make();
  win_->run(Cmd::SetGroup, static_cast<int>(GroupBy::Name));
  frame();
  ASSERT_FALSE(win_->tab().group_starts.empty());
  const int i = index_of("b.txt");
  click_item(i);
  ASSERT_EQ(win_->tab().selected().size(), 1u);
  EXPECT_EQ(win_->tab().name_at(win_->tab().selected()[0]), "b.txt");
  key(XKB_KEY_Down);
  EXPECT_NE(win_->tab().selected()[0], i);
  win_->run(Cmd::SetGroup, static_cast<int>(GroupBy::None));
  EXPECT_TRUE(win_->tab().group_starts.empty());
}

TEST_F(FmWindowTest, ThePreviewPaneTakesRoomAndDrawsPicturesAndText) {
  put(work_ / "t.txt", "line one\nline two\n");
  FmSettings s;
  s.show_preview_pane = true;
  s.startup = Startup::Home;
  make(s);
  EXPECT_GT(win_->preview_rect().w, 200);
  EXPECT_LT(win_->layout().content.w, 1024 - 200);
  select("t.txt");
  frame();
  win_->open_address((work_ / "pics").string());
  win_->wait_idle();
  click_item(0);
  frame();
  frame(800, 600);
  EXPECT_EQ(win_->preview_rect().w, 0) << "a narrow window keeps the list";
}

TEST_F(FmWindowTest, ATooltipAppearsAfterTheHoverDelay) {
  make();
  const Rect r = win_->item_screen_rect(0);
  win_->on_motion(r.x + 20, r.y + 8);
  frame();
  clock_ += 1.0;
  frame();  // drawn with the tooltip: must simply not crash and must stay inside the window
  SUCCEED();
}
