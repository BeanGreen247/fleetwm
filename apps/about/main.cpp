// fleetwm-about: what Windows' winver is for this desktop -- the exact version and git revision, when it was built, the
// compositor and library versions it runs on, the project and license links. Esc closes it; a second launch closes the first.

#include <signal.h>
#include <sys/signalfd.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include "desktop_entry.hpp"
#include "fleetkit.hpp"
#include "malloc_tuning.hpp"
#include "prewarm.hpp"
#include "quit_signals.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "version.hpp"
#include "win7_look.hpp"

namespace {

using namespace fleetwm;
using namespace fleetwm::kit;

constexpr int kWindowW = 560, kWindowH = 660;
const char* const kProjectUrl = "https://github.com/BeanGreen247/fleetwm";
const char* const kLicenseUrl = "https://github.com/BeanGreen247/fleetwm#license";
const char* const kIssuesUrl = "https://github.com/BeanGreen247/fleetwm/issues";
const char* const kChangelogUrl = "https://github.com/BeanGreen247/fleetwm/blob/master/CHANGELOG.md";

std::string pid_path() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  return std::string(rt && *rt ? rt : "/tmp") + "/fleetwm-about.pid";
}

bool toggle_existing() {
  std::ifstream in(pid_path());
  long pid = 0;
  if (!(in >> pid) || pid <= 0 || pid == getpid()) return false;
  std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
  std::string name;
  if (!(comm >> name) || name != "fleetwm-about") return false;
  kill(static_cast<pid_t>(pid), SIGTERM);
  return true;
}

// "Fedora Linux 44 (Workstation Edition)" from os-release, else "Linux".
std::string os_name() {
  std::ifstream in("/etc/os-release");
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
    std::string v = line.substr(12);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) v = v.substr(1, v.size() - 2);
    return v;
  }
  return "Linux";
}

std::string kernel_name() {
  utsname u{};
  return uname(&u) == 0 ? std::string(u.sysname) + " " + u.release + " " + u.machine : "unknown";
}

struct About {
  App app;
  ThemeConfig theme;
  Palette pal;
  Ui ui{pal};
  std::unique_ptr<Surface> surface;
  std::string os = os_name(), kernel = kernel_name();

  void reload() {
    theme = load_theme_config();
    pal = load_palette(theme);
    ui.set_palette(pal);
  }

  void redraw() { surface->queue_draw(); }

  void info(const char* name, const std::string& value) {
    ui.row(name);
    ui.label(value);
    ui.newline();
  }

  void draw(cairo_t* cr, int w, int h) {
    ui.begin(cr, w, h);
    ui.set_margins(28, 22, 28);
    ui.set_label_width(150);
    draw_start_orb(cr, w - 70, 66, 32, pal.accent, false, false);
    ui.title("Fleetwm");
    ui.label("A wlroots Wayland compositor and desktop shell", true);
    ui.newline();
    ui.space(10);
    ui.section("Version");
    info("Version", version_number());
    info("Git revision", version_revision().empty() ? "unknown" : version_revision());
    info("Built", std::string(__DATE__) + ", " + __TIME__);
    info("Window layout", theme.window_layout == WindowLayout::Desktop ? "Desktop" : "Tiling");
    ui.space(8);
    ui.section("This computer");
    info("System", os);
    info("Kernel", kernel);
    const char* session = std::getenv("XDG_SESSION_TYPE");
    info("Session", session && *session ? session : "wayland");
    ui.space(8);
    ui.section("Project");
    info("Copyright", "(c) 2026 Thomas Mozdren (BeanGreen247)");
    info("License", "MIT");
    ui.space(6);
    if (ui.button("Project page", true, true)) spawn_detached({"xdg-open", kProjectUrl});
    ui.same_line();
    if (ui.button("License")) spawn_detached({"xdg-open", kLicenseUrl});
    ui.same_line();
    if (ui.button("Changelog")) spawn_detached({"xdg-open", kChangelogUrl});
    ui.same_line();
    if (ui.button("Report a problem")) spawn_detached({"xdg-open", kIssuesUrl});
    ui.newline();
    ui.space(10);
    ui.label("The software is provided as is, without warranty of any kind; see the license for the full terms.", true);
    ui.newline();
    ui.end();
    if (ui.wants_another_frame()) surface->queue_draw();
  }
};

}  // namespace

int main(int argc, char** argv) {
  fleetwm::block_quit_signals();
  if (fleetwm::handle_info_flags(argc, argv, "fleetwm-about", "")) return 0;
  fleetwm::tune_malloc_for_low_rss();
  fleetwm::prewarm::start("fleetwm-about");
  signal(SIGCHLD, SIG_IGN);
  if (toggle_existing()) return 0;
  { std::ofstream(pid_path()) << getpid() << "\n"; }

  About S;
  S.reload();
  if (!S.app.connect()) return 1;

  Surface::Config cfg;
  cfg.toplevel = true;
  cfg.app_id = "dev.fleetwm.About";
  cfg.title = "About Fleetwm";
  cfg.width = kWindowW;
  cfg.height = kWindowH;
  cfg.min_width = 520;
  cfg.min_height = 560;
  S.surface = std::make_unique<Surface>(S.app, cfg);
  S.surface->on_draw = [&S](cairo_t* cr, int w, int h) { S.draw(cr, w, h); };
  S.surface->on_motion = [&S](double x, double y) {
    if (S.ui.pointer_motion(x, y)) S.redraw();
  };
  S.surface->on_leave = [&S] {
    S.ui.pointer_leave();
    S.redraw();
  };
  S.surface->on_button = [&S](double x, double y, uint32_t b, bool p) {
    S.ui.pointer_button(x, y, b, p);
    S.redraw();
  };
  S.surface->on_key = [&S](const KeyEvent& e) {
    if (e.pressed && e.sym == XKB_KEY_Escape) {
      S.app.quit();
      return;
    }
    S.ui.key(e);
    S.redraw();
  };
  S.surface->on_closed = [&S] { S.app.quit(); };

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  const int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  S.app.watch_fd(sfd, [&S] { S.app.quit(); });

  S.app.run();
  unlink(pid_path().c_str());
  return 0;
}
