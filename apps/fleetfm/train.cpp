// `fleetwm-fm --train DIR`: drives the whole window with scripted input on a folder tree it makes inside DIR, drawing into memory. No compositor
// and nothing outside DIR is touched (the configuration, the trash and the home folder are pointed there). Used by the PGO training run
// (scripts/pgo-train-session.sh) so the instrumented build sees every style, every view, search, copy with verification, move, delete, rename,
// undo, dialogs, menus, grouping, the preview pane and drag and drop; also a quick smoke test: it exits 1 if a step does not do what it should.

#include <xkbcommon/xkbcommon-keysyms.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "file_ops.hpp"
#include "fm_settings.hpp"
#include "theme.hpp"
#include "window.hpp"

namespace fs = std::filesystem;
using namespace fleetwm;
using namespace fleetwm::fm;

namespace {

struct Driver {
  std::unique_ptr<FmWindow> win;
  double clock = 100;
  int w = 1024, h = 768;
  cairo_surface_t* surf = nullptr;
  int failures = 0;

  void make(const FmSettings& s, const std::string& start) {
    Host host;
    host.now = [this] { return clock; };
    win = std::make_unique<FmWindow>(host, s, kit::Palette{});
    win->start(start);
    win->wait_idle(10);
    frame();
  }
  void frame() {
    if (!surf) surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    win->draw(cr, w, h);
    cairo_destroy(cr);
  }
  void key(xkb_keysym_t sym, const std::string& utf8 = "", uint32_t mods = 0) {
    kit::KeyEvent e;
    e.sym = sym;
    e.utf8 = utf8;
    e.mods = mods;
    win->on_key(e);
    e.pressed = false;
    e.mods = 0;
    win->on_key(e);
    frame();
  }
  void type(const std::string& s) {
    for (char c : s) key(static_cast<xkb_keysym_t>(c), std::string(1, c));
  }
  void click(const Rect& r, uint32_t button = 0x110) {
    const double x = r.x + r.w / 2.0, y = r.y + std::min(r.h / 2.0, 12.0);
    win->on_motion(x, y);
    frame();
    win->on_button(x, y, button, true);
    win->on_button(x, y, button, false);
    frame();
    clock += 1;
  }
  void expect(bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "fleetwm-fm --train: %s\n", what);
      ++failures;
    }
  }
  void settle() {
    win->wait_idle(10);
    frame();
  }
};

void make_tree(const fs::path& root) {
  fs::create_directories(root / "docs/reports");
  fs::create_directories(root / "pics");
  fs::create_directories(root / "music");
  fs::create_directories(root / "empty");
  for (int i = 0; i < 60; ++i) {
    std::ofstream(root / "docs" / ("report-" + std::to_string(i) + ".txt")) << std::string(static_cast<size_t>(200 + i * 37), 'x') << "\nneedle " << i << "\n";
    std::ofstream(root / "music" / ("track " + std::to_string(i) + ".mp3")) << std::string(1000, 'm');
  }
  for (const char* n : {"a.txt", "b.txt", "c.md", "Makefile", "data.csv", "archive.zip", "movie.mkv", "script.sh"}) std::ofstream(root / n) << "line one\nline two\n";
  std::ofstream(root / ".hidden") << "h";
  std::ofstream(root / "big.bin", std::ios::binary) << std::string(3 << 20, 'b');
  // a small real PNG for the thumbnail and preview paths
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 90);
  cairo_t* cr = cairo_create(s);
  cairo_set_source_rgb(cr, 0.2, 0.5, 0.8);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_write_to_png(s, (root / "pics/photo.png").c_str());
  cairo_surface_destroy(s);
  fs::copy_file(root / "pics/photo.png", root / "photo2.png");
}

}  // namespace

int run_train(const std::string& dir) {
  const fs::path base = fs::absolute(dir);
  fs::remove_all(base);
  fs::create_directories(base / "home/Documents");
  fs::create_directories(base / "config");
  setenv("HOME", (base / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (base / "config").c_str(), 1);
  setenv("XDG_DATA_HOME", (base / "data").c_str(), 1);
  const fs::path root = base / "work";
  make_tree(root);
  Driver d;

  // every style and every view mode
  for (const StyleSpec& st : all_styles()) {
    FmSettings s;
    apply_style(&s, st.id);
    s.startup = Startup::Home;
    d.make(s, root.string());
    for (ViewMode m : {ViewMode::Details, ViewMode::List, ViewMode::SmallIcons, ViewMode::MediumIcons, ViewMode::LargeIcons, ViewMode::ExtraLargeIcons, ViewMode::Tiles, ViewMode::Content}) {
      d.win->run(Cmd::SetView, static_cast<int>(m));
      d.settle();
      d.win->on_motion(400, 300);
      d.frame();
    }
    d.win->open_address("computer:///");
    d.settle();
    d.win->open_address((root / "pics").string());
    d.settle();
    d.win.reset();
  }

  // the Windows 7 window through its everyday work
  FmSettings s;
  s.startup = Startup::Home;
  s.confirm_delete = false;
  s.confirm_conflicts = false;
  d.make(s, root.string());
  for (int pass = 0; pass < 2; ++pass) {
    d.win->open_address((root / "docs").string());
    d.settle();
    for (SortKey k : {SortKey::Size, SortKey::Modified, SortKey::Type, SortKey::Name}) {
      d.win->run(Cmd::SetSort, static_cast<int>(k));
      d.settle();
    }
    for (GroupBy g : {GroupBy::Name, GroupBy::Type, GroupBy::Size, GroupBy::Modified, GroupBy::None}) {
      d.win->run(Cmd::SetGroup, static_cast<int>(g));
      d.settle();
    }
    for (int i = 0; i < 30; ++i) d.win->on_scroll(0, 3);
    d.frame();
    d.key(XKB_KEY_a, "a", kit::kCtrl);
    d.key(XKB_KEY_Escape);
    for (int i = 0; i < 15; ++i) d.key(XKB_KEY_Down);
    d.key(XKB_KEY_End);
    d.key(XKB_KEY_Home);
    d.type("report-4");
  }
  // search
  d.key(XKB_KEY_f, "f", kit::kCtrl);
  d.type("needle");
  d.settle();
  d.key(XKB_KEY_Escape);
  d.settle();
  d.key(XKB_KEY_f, "f", kit::kCtrl);
  d.type("rep");
  d.settle();
  d.key(XKB_KEY_Escape);
  d.settle();
  // tabs and navigation
  d.key(XKB_KEY_t, "t", kit::kCtrl);
  d.settle();
  d.win->open_address((root / "music").string());
  d.settle();
  d.key(XKB_KEY_Tab, "", kit::kCtrl);
  d.key(XKB_KEY_w, "w", kit::kCtrl);
  d.key(XKB_KEY_Left, "", kit::kAlt);
  d.settle();
  d.key(XKB_KEY_Right, "", kit::kAlt);
  d.settle();
  d.key(XKB_KEY_Up, "", kit::kAlt);
  d.settle();
  d.key(XKB_KEY_l, "l", kit::kCtrl);
  d.type((root / "pics").string());
  d.key(XKB_KEY_Return, "\r");
  d.settle();
  // preview pane and thumbnails
  d.win->run(Cmd::TogglePreview);
  d.win->run(Cmd::SetView, static_cast<int>(ViewMode::LargeIcons));
  d.settle();
  d.click(d.win->item_screen_rect(0));
  d.settle();
  d.win->open_address(root.string());
  d.settle();
  d.win->run(Cmd::SetView, static_cast<int>(ViewMode::Details));
  for (int i = 0; i < d.win->tab().shown.size() && i < 12; ++i) {
    d.click(d.win->item_screen_rect(i));
    d.frame();
  }
  d.win->run(Cmd::TogglePreview);
  // hover tooltip
  d.win->on_motion(300, 120);
  d.frame();
  d.clock += 1;
  d.frame();
  // copy with verification, big file and many small ones, move, undo
  d.win->open_address(root.string());
  d.settle();
  int bi = -1;
  for (size_t i = 0; i < d.win->tab().shown.size(); ++i)
    if (d.win->tab().name_at(static_cast<int>(i)) == "big.bin") bi = static_cast<int>(i);
  d.expect(bi >= 0, "big.bin not listed");
  if (bi >= 0) {
    d.click(d.win->item_screen_rect(bi));
    d.key(XKB_KEY_c, "c", kit::kCtrl);
    d.win->open_address((root / "empty").string());
    d.settle();
    d.key(XKB_KEY_v, "v", kit::kCtrl);
    d.settle();
    d.expect(fs::exists(root / "empty/big.bin"), "copy of big.bin missing");
  }
  d.win->open_address((root / "docs").string());
  d.settle();
  d.key(XKB_KEY_a, "a", kit::kCtrl);
  d.key(XKB_KEY_c, "c", kit::kCtrl);
  d.win->open_address((root / "empty").string());
  d.settle();
  d.key(XKB_KEY_v, "v", kit::kCtrl);
  d.settle();
  d.expect(fs::exists(root / "empty/report-5.txt"), "copy of the reports missing");
  d.key(XKB_KEY_z, "z", kit::kCtrl);
  d.settle();
  d.key(XKB_KEY_a, "a", kit::kCtrl);
  d.key(XKB_KEY_x, "x", kit::kCtrl);
  d.win->open_address((root / "music").string());
  d.settle();
  d.key(XKB_KEY_v, "v", kit::kCtrl);
  d.settle();
  d.key(XKB_KEY_z, "z", kit::kCtrl);
  d.settle();
  // rename, new folder, delete, restore
  d.win->open_address(root.string());
  d.settle();
  d.click(d.win->item_screen_rect(d.win->tab().shown.size() > 3 ? 3 : 0));
  d.key(XKB_KEY_F2);
  d.type("renamed");
  d.key(XKB_KEY_Return, "\r");
  d.settle();
  d.key(XKB_KEY_n, "n", kit::kCtrl | kit::kShift);
  d.settle();
  d.type("fresh");
  d.key(XKB_KEY_Return, "\r");
  d.settle();
  d.key(XKB_KEY_Delete);
  d.settle();
  d.win->open_address("trash:///");
  d.settle();
  d.win->run(Cmd::SelectAll);
  d.win->run(Cmd::Restore);
  d.settle();
  // drag and drop with the question
  d.win->open_address(root.string());
  d.settle();
  const std::string uri = "file://" + (root / "a.txt").string() + "\r\n";
  for (int i = 0; i < d.win->tab().shown.size(); ++i)
    if (d.win->tab().is_dir_at(i)) {
      const Rect r = d.win->item_screen_rect(i);
      d.win->on_drag_motion(r.x + 20, r.y + 8, nullptr);
      d.frame();
      d.win->on_drop(r.x + 20, r.y + 8, uri);
      d.frame();
      d.key(XKB_KEY_Down);
      d.key(XKB_KEY_Return);
      d.settle();
      break;
    }
  // menus and dialogs
  d.click(d.win->item_screen_rect(0), 0x111);
  d.key(XKB_KEY_Escape);
  const Rect c = d.win->layout().content;
  d.win->on_motion(c.x + c.w - 30, c.y + c.h - 30);
  d.frame();
  d.win->on_button(c.x + c.w - 30, c.y + c.h - 30, 0x111, true);
  d.win->on_button(c.x + c.w - 30, c.y + c.h - 30, 0x111, false);
  d.frame();
  d.key(XKB_KEY_Escape);
  for (Cmd m : {Cmd::MenuOrganize, Cmd::MenuView, Cmd::MenuSort}) {
    d.win->run(m);
    d.frame();
    d.key(XKB_KEY_Down);
    d.key(XKB_KEY_Right);
    d.key(XKB_KEY_Escape);
    d.key(XKB_KEY_Escape);
  }
  for (Cmd dl : {Cmd::About, Cmd::ConnectServer, Cmd::AddNextcloud, Cmd::Properties, Cmd::Checksums}) {
    d.win->run(dl);
    d.settle();
    d.win->close_dialog_for_test();
  }
  d.win->open_address("network:///");
  d.settle();
  d.win->open_address("recent:///");
  d.settle();
  d.win->save_session();
  d.win.reset();
  fs::remove_all(base);
  return d.failures == 0 ? 0 : 1;
}
