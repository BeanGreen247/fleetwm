#include <gtest/gtest.h>

#include <set>

#include "fm_settings.hpp"
#include "test_util.hpp"
#include "view_style.hpp"

using namespace fleetwm::fm;

using FmSettingsTest = fleetwm::testutil::ScopedConfigHome;

TEST_F(FmSettingsTest, DefaultsAreTheWindows7Experience) {
  const FmSettings s = load_fm_settings();
  EXPECT_EQ(s.style, ViewStyle::Windows7);
  EXPECT_EQ(s.default_view, ViewMode::Details);
  EXPECT_EQ(s.click_mode, ClickMode::Double);
  EXPECT_EQ(s.open_folders_in, OpenIn::SameWindow);
  EXPECT_FALSE(s.show_hidden);
  EXPECT_TRUE(s.show_extensions);
  EXPECT_EQ(s.verify, VerifyWhen::Always);
  EXPECT_EQ(s.verify_algo, HashAlgo::Auto);
  EXPECT_EQ(s.delete_mode, DeleteMode::Trash);
  EXPECT_TRUE(s.confirm_delete);
  EXPECT_TRUE(s.power_off_after_eject);
}

TEST_F(FmSettingsTest, EveryFieldRoundTrips) {
  FmSettings s;
  s.style = ViewStyle::Nemo;
  s.open_folders_in = OpenIn::NewTab;
  s.click_mode = ClickMode::Single;
  s.show_navigation_pane = false;
  s.nav_show_all_folders = true;
  s.nav_expand_to_current = true;
  s.nav_show_libraries = false;
  s.startup = Startup::Custom;
  s.startup_path = "/srv/data";
  s.last_location = "/tmp";
  s.always_show_tabs = true;
  s.new_tab_at = NewTabAt::CurrentFolder;
  s.restore_tabs = true;
  s.open_tabs = {"/a", "/b c"};
  s.default_view = ViewMode::LargeIcons;
  s.style_sets_view = false;
  s.show_hidden = true;
  s.show_extensions = false;
  s.show_full_path_in_title = true;
  s.menu_bar = MenuBar::Always;
  s.use_checkboxes = true;
  s.type_ahead = TypeAhead::Search;
  s.thumbnail_max_mb = 64;
  s.sort_key = SortKey::Modified;
  s.sort_ascending = false;
  s.size_format = SizeFormat::Exact;
  s.date_style = DateStyle::Iso;
  s.columns = {{"name", 300, true}, {"size", 90, false}};
  s.colour_scheme = ColorScheme::Dark;
  s.font_px = 15;
  s.row_height = 30;
  s.icon_px = 64;
  s.nav_width = 260;
  s.window_w = 1280;
  s.window_h = 800;
  s.search_in_contents = true;
  s.search_max_results = 99;
  s.verify = VerifyWhen::ExternalOnly;
  s.verify_algo = HashAlgo::Sha1;
  s.sync = SyncMode::Always;
  s.block_kib = 4096;
  s.direct_verify = false;
  s.delete_mode = DeleteMode::Ask;
  s.history_size = 5;
  save_fm_settings(s);
  EXPECT_EQ(load_fm_settings(), s);
}

TEST_F(FmSettingsTest, UnknownValuesAndBrokenFilesFallBack) {
  write_config("fleetfm.toml", "[general]\nstyle = \"beos\"\nclick_mode = \"triple\"\n[sort]\nkey = \"colour\"\n[transfer]\nblock_kib = 1\n");
  const FmSettings s = load_fm_settings();
  EXPECT_EQ(s.style, ViewStyle::Windows7);
  EXPECT_EQ(s.click_mode, ClickMode::Double);
  EXPECT_EQ(s.sort_key, SortKey::Name);
  EXPECT_EQ(s.block_kib, 64) << "clamped to the smallest block";
  write_config("fleetfm.toml", "[general\nstyle = ");
  EXPECT_EQ(load_fm_settings(), FmSettings{});
}

TEST_F(FmSettingsTest, NumbersAreClampedWhenLoaded) {
  write_config("fleetfm.toml", "[appearance]\nfont_px = 900\nwindow_w = 10\nrow_height = -4\n[search]\nmax_results = 1\n");
  const FmSettings s = load_fm_settings();
  EXPECT_EQ(s.font_px, 40);
  EXPECT_EQ(s.window_w, 640);
  EXPECT_EQ(s.row_height, 0);
  EXPECT_EQ(s.search_max_results, 10);
}

TEST_F(FmSettingsTest, AStylelessFileTakesTheViewModeFromTheStyle) {
  write_config("fleetfm.toml", "[general]\nstyle = \"mac\"\n");
  EXPECT_EQ(load_fm_settings().default_view, ViewMode::MediumIcons);
}

TEST(FmSettingsLogic, ChangingStyleResetsOverridesAndFollowsTheStyle) {
  FmSettings s;
  s.font_px = 20;
  s.row_height = 40;
  s.nav_width = 300;
  apply_style(&s, ViewStyle::Caja);
  EXPECT_EQ(s.style, ViewStyle::Caja);
  EXPECT_EQ(s.default_view, ViewMode::MediumIcons);
  EXPECT_EQ(s.font_px, 0);
  EXPECT_EQ(s.nav_width, 0);
  EXPECT_EQ(s.menu_bar, MenuBar::Always);
  EXPECT_FALSE(s.show_details_pane);
  s.style_sets_view = false;
  s.default_view = ViewMode::List;
  apply_style(&s, ViewStyle::Windows7);
  EXPECT_EQ(s.default_view, ViewMode::List);
  EXPECT_TRUE(s.show_details_pane);
  EXPECT_EQ(s.menu_bar, MenuBar::Alt);
}

TEST(FmSettingsLogic, VerificationFollowsTheDestination) {
  FmSettings s;
  s.verify = VerifyWhen::Always;
  EXPECT_TRUE(should_verify(s, false));
  s.verify = VerifyWhen::ExternalOnly;
  EXPECT_FALSE(should_verify(s, false));
  EXPECT_TRUE(should_verify(s, true));
  s.verify = VerifyWhen::Never;
  EXPECT_FALSE(should_verify(s, true));
}

TEST(FmSettingsLogic, TransferOptionsCarryTheSettings) {
  FmSettings s;
  s.block_kib = 256;
  s.verify_algo = HashAlgo::Md5;
  s.confirm_conflicts = false;
  const TransferOptions o = transfer_options(s, true, true);
  EXPECT_TRUE(o.move);
  EXPECT_TRUE(o.verify);
  EXPECT_EQ(o.block_bytes, 256u * 1024);
  EXPECT_EQ(o.algo, HashAlgo::Md5);
  ASSERT_TRUE(o.on_conflict);
  EXPECT_EQ(o.on_conflict({}), Conflict::KeepBoth);
  s.confirm_conflicts = true;
  EXPECT_FALSE(transfer_options(s, false, false).on_conflict);
}

TEST(FmSettingsLogic, DefaultColumnsStartWithNameAndHideTheExtras) {
  const auto c = default_columns();
  ASSERT_GE(c.size(), 4u);
  EXPECT_EQ(c[0].id, "name");
  EXPECT_TRUE(c[0].visible);
  std::set<std::string> ids;
  for (const auto& col : c) ids.insert(col.id);
  EXPECT_EQ(ids.size(), c.size());
}

TEST(FmStyles, EveryStyleIsComplete) {
  std::set<std::string> keys;
  EXPECT_EQ(all_styles().size(), 9u);
  for (const StyleSpec& s : all_styles()) {
    EXPECT_TRUE(keys.insert(s.key).second) << s.key;
    EXPECT_GT(s.row_h, 0);
    EXPECT_GT(s.font_px, 8);
    EXPECT_GT(s.icon_px, 8);
    EXPECT_GT(s.toolbar_h, 0);
    EXPECT_GT(s.nav_w, 100);
    ViewStyle back;
    ASSERT_TRUE(parse_style(s.key, &back)) << s.key;
    EXPECT_EQ(back, s.id);
    ASSERT_TRUE(parse_style(s.name, &back)) << s.name;
    EXPECT_EQ(back, s.id);
    EXPECT_EQ(&style_spec(s.id), &s);
  }
}

TEST(FmStyles, Windows7IsFirstAndLooksLikeIt) {
  const StyleSpec& s = all_styles().front();
  EXPECT_EQ(s.id, ViewStyle::Windows7);
  EXPECT_EQ(s.toolbar, ToolbarKind::CommandBar);
  EXPECT_EQ(s.nav, NavKind::Tree);
  EXPECT_EQ(s.address, AddressKind::Breadcrumb);
  EXPECT_TRUE(s.address_in_toolbar_row);
  EXPECT_TRUE(s.details_pane);
  EXPECT_EQ(s.selection, SelectionPaint::Win7Glass);
  EXPECT_EQ(s.default_view, ViewMode::Details);
  EXPECT_FALSE(s.menu_bar);
}

TEST(FmStyles, NamesParseWithAliases) {
  ViewStyle v;
  EXPECT_TRUE(parse_style("Windows 7", &v));
  EXPECT_EQ(v, ViewStyle::Windows7);
  EXPECT_TRUE(parse_style("win10", &v));
  EXPECT_EQ(v, ViewStyle::Windows10);
  EXPECT_TRUE(parse_style("Finder", &v));
  EXPECT_EQ(v, ViewStyle::Mac);
  EXPECT_TRUE(parse_style("MATE", &v));
  EXPECT_EQ(v, ViewStyle::Caja);
  EXPECT_TRUE(parse_style("gnome-files", &v) || parse_style("gnome", &v));
  EXPECT_EQ(v, ViewStyle::Nautilus);
  EXPECT_TRUE(parse_style("Cinnamon", &v));
  EXPECT_EQ(v, ViewStyle::Nemo);
  EXPECT_TRUE(parse_style("KDE", &v));
  EXPECT_EQ(v, ViewStyle::Dolphin);
  EXPECT_FALSE(parse_style("", &v));
  EXPECT_FALSE(parse_style("amiga", &v));
}

TEST(FmStyles, ViewModesParseAndSizeTheirIcons) {
  ViewMode m;
  for (ViewMode x : {ViewMode::Details, ViewMode::List, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons, ViewMode::Tiles, ViewMode::Content}) {
    ASSERT_TRUE(parse_view_mode(view_mode_key(x), &m));
    EXPECT_EQ(m, x);
  }
  EXPECT_FALSE(parse_view_mode("hologram", &m));
  EXPECT_LT(view_mode_icon_px(ViewMode::SmallIcons), view_mode_icon_px(ViewMode::MediumIcons));
  EXPECT_LT(view_mode_icon_px(ViewMode::MediumIcons), view_mode_icon_px(ViewMode::LargeIcons));
  EXPECT_LT(view_mode_icon_px(ViewMode::LargeIcons), view_mode_icon_px(ViewMode::ExtraLargeIcons));
}

namespace {
bool inside(const Rect& r, int w, int h) { return r.w == 0 || r.h == 0 || (r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h); }
bool disjoint(const Rect& a, const Rect& b) { return a.w == 0 || a.h == 0 || b.w == 0 || b.h == 0 || a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y; }
}  // namespace

TEST(FmLayout, EveryStyleFitsAt1024x768WithNothingClippedOrOverlapping) {
  for (const StyleSpec& s : all_styles()) {
    LayoutInput in;
    in.width = 1024;
    in.height = 768;
    in.details_pane = s.details_pane;
    in.status_bar = s.status_bar;
    in.menu_bar = s.menu_bar;
    const Layout l = compute_layout(s, in);
    EXPECT_TRUE(l.fits) << s.name;
    for (const Rect* r : {&l.menu, &l.toolbar, &l.address, &l.nav, &l.tabs, &l.content, &l.details, &l.status})
      EXPECT_TRUE(inside(*r, 1024, 768)) << s.name;
    EXPECT_TRUE(disjoint(l.nav, l.content)) << s.name;
    EXPECT_TRUE(disjoint(l.toolbar, l.content)) << s.name;
    EXPECT_TRUE(disjoint(l.tabs, l.content)) << s.name;
    EXPECT_TRUE(disjoint(l.status, l.content)) << s.name;
    EXPECT_TRUE(disjoint(l.details, l.content)) << s.name;
    EXPECT_GE(l.content.w, 600) << s.name;
    EXPECT_GE(l.content.h, 300) << s.name;
  }
}

TEST(FmLayout, Windows7RowHoldsBackForwardAddressAndSearch) {
  LayoutInput in;
  in.width = 1024;
  in.height = 768;
  const Layout l = compute_layout(style_spec(ViewStyle::Windows7), in);
  EXPECT_LT(l.address.y, l.toolbar.y) << "the address bar sits above the command bar";
  EXPECT_EQ(l.address.x, l.back_forward_w);
  EXPECT_GT(l.search_w, 100);
  EXPECT_LE(l.address.x + l.address.w + l.search_w, 1024);
  in.details_pane = true;
  in.status_bar = true;
  const Layout with_bars = compute_layout(style_spec(ViewStyle::Windows7), in);
  EXPECT_GT(with_bars.details.h, 0);
  EXPECT_GT(with_bars.status.h, 0) << "View -> Status bar works in Windows 7 too";
}

TEST(FmLayout, WhatIsSwitchedOffTakesNoRoom) {
  LayoutInput in;
  in.width = 1024;
  in.height = 768;
  const StyleSpec& s = style_spec(ViewStyle::Windows7);
  in.details_pane = in.status_bar = true;
  const Layout full = compute_layout(s, in);
  in.nav_pane = false;
  in.details_pane = false;
  in.status_bar = false;
  in.tabs = false;
  const Layout bare = compute_layout(s, in);
  EXPECT_EQ(bare.nav.w, 0);
  EXPECT_EQ(bare.details.h, 0);
  EXPECT_EQ(bare.status.h, 0);
  EXPECT_EQ(bare.tabs.h, 0);
  EXPECT_GT(bare.content.w, full.content.w);
  EXPECT_GT(bare.content.h, full.content.h);
}

TEST(FmLayout, NavigationWidthIsCappedAndOverridable) {
  const StyleSpec& s = style_spec(ViewStyle::Windows7);
  LayoutInput in;
  in.width = 1024;
  in.height = 768;
  in.nav_width_override = 700;
  EXPECT_LE(compute_layout(s, in).nav.w, 1024 / 3);
  in.nav_width_override = 250;
  EXPECT_EQ(compute_layout(s, in).nav.w, 250);
}

TEST(FmLayout, SmallWindowsGiveUpThePanesBeforeTheList) {
  const StyleSpec& s = style_spec(ViewStyle::Windows7);
  LayoutInput in;
  in.width = 700;
  in.height = 600;
  const Layout l = compute_layout(s, in);
  EXPECT_EQ(l.nav.w, 0);
  EXPECT_EQ(l.details.h, 0);
  EXPECT_GT(l.content.w, 600);
}
