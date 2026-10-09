// fleetwm-fm: native file manager. Tabs, a Windows 7 style window by default (Windows 10, Mac, Caja, Nautilus, Nemo, Thunar,
// PCManFM and Dolphin styles in Settings), verified copies, safe eject, and network places (SMB, SFTP, FTP, WebDAV/Nextcloud, NFS).
//
//   fleetwm-fm [FOLDER|URI ...]            open each in a tab
//   fleetwm-fm --screenshot FILE [--size WxH] [--style NAME] [--view MODE] [--show WHAT] [FOLDER]
//                                          render one frame to a PNG without a compositor (WHAT: settings, about, properties,
//                                          connect, nextcloud, menu-view, menu-organize, menu-context, transfer)
//   fleetwm-fm --write-icon FILE [SIZE]    write the application icon as a PNG
//   fleetwm-fm --train DIR                 scripted run of the whole window on a tree made inside DIR (PGO training and a smoke test)

#include <fcntl.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fleetkit.hpp"
#include "fm_settings.hpp"
#include "file_ops.hpp"
#include "icons.hpp"
#include "locations.hpp"
#include "malloc_tuning.hpp"
#include "quit_signals.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "window.hpp"

namespace {

using namespace fleetwm;
namespace fs = std::filesystem;

double now_seconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const char* kUsage = "[--train DIR] [--screenshot FILE [--size WxH] [--style NAME] [--view MODE] [--show WHAT]] [--write-icon FILE [SIZE]] [--connect | --nextcloud] [FOLDER|URI ...]";

int screenshot(const std::vector<std::string>& args) {
  std::string out, style, view, show, where;
  std::vector<std::string> clicks;  // "x,y": a left click after the window is up (checks popups and the like)
  int w = 1024, h = 768;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "--screenshot" && i + 1 < args.size()) out = args[++i];
    else if (a == "--size" && i + 1 < args.size()) std::sscanf(args[++i].c_str(), "%dx%d", &w, &h);
    else if (a == "--style" && i + 1 < args.size()) style = args[++i];
    else if (a == "--view" && i + 1 < args.size()) view = args[++i];
    else if (a == "--show" && i + 1 < args.size()) show = args[++i];
    else if (a == "--click" && i + 1 < args.size()) clicks.push_back(args[++i]);
    else if (a[0] != '-') where = a;
  }
  fm::FmSettings s = fm::load_fm_settings();
  fm::ViewStyle st;
  if (!style.empty() && fm::parse_style(style, &st)) fm::apply_style(&s, st);
  fm::ViewMode vm;
  if (!view.empty() && fm::parse_view_mode(view, &vm)) s.default_view = vm;
  s.startup = fm::Startup::Home;
  kit::Palette pal = kit::load_palette(load_theme_config());
  fm::Host host;
  host.now = now_seconds;
  fm::FmWindow win(host, s, pal);
  win.start(where.empty() ? std::string() : where);
  win.wait_idle(8.0);
  win.screenshot_setup(show);
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t* cr = cairo_create(surf);
  win.draw(cr, w, h);
  for (const std::string& c : clicks) {
    int cx = 0, cy = 0;
    if (std::sscanf(c.c_str(), "%d,%d", &cx, &cy) != 2) continue;
    win.draw(cr, w, h);
    win.on_motion(cx, cy);
    win.draw(cr, w, h);
    win.on_button(cx, cy, 0x110, true);
    win.on_button(cx, cy, 0x110, false);
    win.draw(cr, w, h);
  }
  win.draw(cr, w, h);   // the first frame builds the layout the second one positions menus and dialogs with
  win.draw(cr, w, h);
  win.wait_idle(2.0);
  win.draw(cr, w, h);
  cairo_destroy(cr);
  const bool ok = cairo_surface_write_to_png(surf, out.c_str()) == CAIRO_STATUS_SUCCESS;
  cairo_surface_destroy(surf);
  if (!ok) std::fprintf(stderr, "fleetwm-fm: cannot write %s\n", out.c_str());
  return ok ? 0 : 1;
}

struct Watcher {
  int fd = -1;
  int wd = -1;
  Watcher() { fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC); }
  void watch(const std::string& path) {
    if (fd < 0) return;
    if (wd >= 0) inotify_rm_watch(fd, wd);
    wd = inotify_add_watch(fd, path.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
  }
  bool drain() {
    char buf[4096];
    bool any = false;
    while (read(fd, buf, sizeof buf) > 0) any = true;
    return any;
  }
};

}  // namespace

int run_train(const std::string& dir);  // train.cpp

int main(int argc, char** argv) {
  block_quit_signals();
  if (handle_info_flags(argc, argv, "fleetwm-fm", kUsage)) return 0;
  std::vector<std::string> args(argv + 1, argv + argc);
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--write-icon" && i + 1 < args.size()) {
      const int size = i + 2 < args.size() && args[i + 2][0] != '-' ? std::atoi(args[i + 2].c_str()) : 256;
      return fm::write_icon_png(fm::IconKind::App, size > 0 ? size : 256, args[i + 1]) ? 0 : 1;
    }
    if (args[i] == "--screenshot") return screenshot(args);
    if (args[i] == "--train" && i + 1 < args.size()) return run_train(args[i + 1]);
  }
  tune_malloc_for_low_rss();

  kit::App app;
  if (!app.connect()) return 1;
  fm::FmSettings settings = fm::load_fm_settings();
  const kit::Palette pal = kit::load_palette(load_theme_config());
  Watcher watcher;

  kit::Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.FileManager";
  cfg.title = "File Manager";
  cfg.width = settings.window_w;
  cfg.height = settings.window_h;
  cfg.min_width = 640;
  cfg.min_height = 420;
  cfg.output = app.requested_output();
  auto surface = std::make_unique<kit::Surface>(app, cfg);

  fm::Host host;
  host.redraw = [&] { surface->queue_draw(); };
  host.set_title = [&](const std::string& t) { surface->set_title(t); };
  host.post = [&](std::function<void()> fn) { app.post(std::move(fn)); };
  host.after = [&](int ms, std::function<void()> fn) { app.add_oneshot(ms, std::move(fn)); };
  host.watch = [&](const std::string& p) { watcher.watch(p); };
  host.paste_text = [&](std::function<void(const std::string&)> cb) { app.paste_text(std::move(cb)); };
  host.now = now_seconds;
  host.quit = [&] { app.quit(); };
  host.set_clipboard_files = [&](const std::vector<std::string>& paths, bool cut) {
    std::string uris = fm::make_uri_list(paths), plain;
    std::string gnome = cut ? "cut" : "copy";
    for (const std::string& p : paths) {
      plain += p + "\n";
      gnome += "\nfile://" + fm::percent_encode(p);
    }
    app.set_clipboard({{"text/uri-list", uris}, {"x-special/gnome-copied-files", gnome}, {"text/plain;charset=utf-8", plain}, {"text/plain", plain}});
  };
  host.read_clipboard_files = [&](std::function<void(const std::vector<std::string>&, bool)> cb) {
    app.paste_mime({"x-special/gnome-copied-files", "text/uri-list", "text/plain;charset=utf-8", "text/plain"}, [cb](const std::string& mime, const std::string& data) {
      bool cut = false;
      std::string body = data;
      if (mime == "x-special/gnome-copied-files") {
        const size_t nl = data.find('\n');
        cut = data.compare(0, 3, "cut") == 0;
        body = nl == std::string::npos ? std::string() : data.substr(nl + 1);
      }
      cb(fm::parse_path_list(body), cut);
    });
  };
  host.set_clipboard_text = [&](const std::string& t) { app.set_clipboard({{"text/plain;charset=utf-8", t}, {"text/plain", t}, {"UTF8_STRING", t}}); };
  host.start_drag = [&](const std::vector<std::string>& paths, bool allow_move, std::function<void(bool, bool)> done) {
    return app.start_drag(*surface, {{"text/uri-list", fm::make_uri_list(paths)}}, allow_move, std::move(done));
  };
  host.new_window = [&](const std::string& path) {
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    std::string exe = n > 0 ? std::string(self, static_cast<size_t>(n)) : std::string(argv[0]);
    if (exe.size() > 10 && exe.compare(exe.size() - 10, 10, " (deleted)") == 0) exe.resize(exe.size() - 10);
    fm::spawn_detached({exe, path});
  };

  fm::FmWindow win(host, settings, pal);
  surface->on_draw = [&](cairo_t* cr, int w, int h) { win.draw(cr, w, h); };
  surface->on_motion = [&](double x, double y) { win.on_motion(x, y); };
  surface->on_button = [&](double x, double y, uint32_t b, bool p) {
    win.on_focus(true);
    win.on_button(x, y, b, p);
  };
  surface->on_scroll = [&](double dx, double dy) { win.on_scroll(dx, dy); };
  surface->on_leave = [&] { win.on_leave(); };
  surface->on_key = [&](const kit::KeyEvent& e) {
    win.on_focus(true);
    win.on_key(e);
  };
  surface->on_keyboard_leave = [&] { win.on_focus(false); };
  surface->on_configure = [&](int w, int h) {
    if (w > 0 && h > 0) win.set_window_size(w, h);
  };
  surface->on_drag_motion = [&](double x, double y, uint32_t* action) { return win.on_drag_motion(x, y, action); };
  surface->on_drag_leave = [&] { win.on_drag_leave(); };
  surface->on_drop = [&](double x, double y, const std::string& list) { win.on_drop(x, y, list); };
  surface->on_closed = [&] { app.quit(); };

  app.watch_fd(watcher.fd, [&] {
    if (watcher.drain()) win.on_dir_changed();
  });

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  app.watch_fd(sfd, [&] { app.quit(); });

  std::vector<std::string> open;
  for (const std::string& a : args)
    if (!a.empty() && a[0] != '-') open.push_back(a);
  win.start(open.empty() ? std::string() : open[0]);
  for (const std::string& a : args) {  // from the Settings app: open the dialog straight away
    if (a == "--connect") win.screenshot_setup("connect");
    else if (a == "--nextcloud") win.screenshot_setup("nextcloud");
  }
  for (size_t i = 1; i < open.size(); ++i) win.open_address(open[i], true);
  surface->queue_draw();
  app.run();
  win.save_session();
  return 0;
}
