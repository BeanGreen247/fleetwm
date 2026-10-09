// File manager window benchmark, no compositor: how long a big folder takes to show and what a frame costs.
//   fleetwm-fm-uibench FOLDER [frames]
// Prints: time from start() to a listed folder (read + sort, on the loader thread), RSS before and after, then the median cost of
// drawing one 1024x768 frame in every view mode at the top of the list, in the middle, and while the pointer moves.
#include <cairo.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "window.hpp"

using namespace fleetwm;
using namespace fleetwm::fm;

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static long rss_kb() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.compare(0, 6, "VmRSS:") == 0) return std::atol(line.c_str() + 6);
  return 0;
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  const int frames = argc > 2 ? std::atoi(argv[2]) : 200;
  FmSettings s;
  s.startup = Startup::Home;
  Host h;
  h.now = [] { return now(); };
  const long rss0 = rss_kb();
  FmWindow win(h, s, kit::Palette{});
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1024, 768);
  cairo_t* cr = cairo_create(surf);
  win.draw(cr, 1024, 768);
  const double t0 = now();
  win.start(dir);
  win.wait_idle(120);
  const double load_ms = (now() - t0) * 1000;
  win.draw(cr, 1024, 768);
  const long rss1 = rss_kb();
  std::printf("folder %s: %zu entries shown\n", dir.c_str(), win.tab().shown.size());
  std::printf("start() to listed: %.1f ms   RSS %ld kB -> %ld kB (+%.0f bytes per entry)\n", load_ms, rss0, rss1, win.tab().shown.empty() ? 0.0 : (rss1 - rss0) * 1024.0 / static_cast<double>(win.tab().shown.size()));
  struct M {
    const char* name;
    ViewMode m;
  } modes[] = {{"details", ViewMode::Details}, {"list", ViewMode::List}, {"medium icons", ViewMode::MediumIcons}, {"large icons", ViewMode::LargeIcons}, {"tiles", ViewMode::Tiles}, {"content", ViewMode::Content}};
  for (const M& m : modes) {
    win.run(Cmd::SetView, static_cast<int>(m.m));
    win.draw(cr, 1024, 768);
    win.draw(cr, 1024, 768);
    auto measure = [&](int where) {
      std::vector<double> t;
      for (int i = 0; i < frames; ++i) {
        if (where == 1) win.tab().scroll_y = (win.tab().scroll_y + 997) % 4000000;  // jumps to an arbitrary place each frame
        if (where == 2) win.on_motion(300 + (i % 40) * 8, 150 + (i % 17) * 20);
        const double a = now();
        win.draw(cr, 1024, 768);
        t.push_back((now() - a) * 1000);
      }
      std::sort(t.begin(), t.end());
      return t[t.size() / 2];
    };
    win.tab().scroll_y = 0;
    const double top = measure(0);
    win.tab().scroll_y = 0;
    const double scrolled = measure(1);
    win.tab().scroll_y = 0;
    const double hover = measure(2);
    std::printf("  %-13s frame median: top %.2f ms   scrolling %.2f ms   pointer moving %.2f ms\n", m.name, top, scrolled, hover);
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return 0;
}
