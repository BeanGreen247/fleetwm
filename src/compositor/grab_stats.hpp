#pragma once

// Counters for finding out where the work goes while a window is dragged or resized (set FLEETWM_GRAB_STATS=1 in the compositor's environment; off by
// default, and then every hook is one predictable branch on a cached bool). Once per second of activity it prints one line to stderr:
//
//   GRABSTATS mode=move dt=1.000 out=1920x1080 events=130 pos=130 resize_req=0 snap=0 frames=47 cap_wait=19 cap_idle=4 commits=24 commit_ms=1.31/3.02
//             damage_px=410000/822000 present=24 missed=0 titlebar_renders=0 strip_renders=0
//
//   events        pointer motions that reached Server::update_grab       pos          wlr_scene_node_set_position calls it made
//   resize_req    client size requests (xdg configure)                  snap         snap-preview zone changes
//   frames        output frame events                                    cap_wait     frames held back by the 24 FPS cap (a timer re-arms)
//   cap_idle      frames the cap dropped because nothing was damaged     commits      frames that were committed with damage pending
//   commit_ms     average/max wall time of wlr_scene_output_commit       damage_px    average/max pixels of pending scene damage per commit
//   present       wlr_output present events                              missed       present events with presented=false
//   *_renders     titlebar / frame-strip pictures drawn on the CPU (cache misses)
//   glass_builds  backdrops of glass frames built from the windows below (glass_scene.cpp) and glass_ms the milliseconds they took
//
// What it cannot show: GPU execution time (commit_ms is the CPU side of the call), or whether a pixel reached the panel (damage is queued scene
// damage). The mode word is the grab in progress when the line is printed ("idle" between drags, which makes the control).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace fleetwm::grabstats {

struct Counters {
  uint64_t events = 0, pos = 0, resize_req = 0, snap = 0, frames = 0, cap_wait = 0, cap_idle = 0, commits = 0, present = 0, missed = 0, titlebar_renders = 0, strip_renders = 0, glass_builds = 0;
  double glass_ms = 0;  // time spent building glass backdrops (glass_scene.cpp)
  double commit_ms = 0, commit_ms_max = 0, damage_px = 0, damage_px_max = 0;
  bool any() const { return events || pos || frames || commits || present || titlebar_renders || strip_renders || glass_builds; }
};

inline bool enabled() {
  static const bool on = std::getenv("FLEETWM_GRAB_STATS") != nullptr;
  return on;
}
// FLEETWM_GRAB_STATS=2 also prints every damage rectangle of each commit made during a grab (RECTS lines), and the grabbed window's boxes.
inline bool rects() {
  static const bool on = [] {
    const char* v = std::getenv("FLEETWM_GRAB_STATS");
    return v && v[0] == '2';
  }();
  return on;
}
inline Counters& c() {
  static Counters x;
  return x;
}
inline const char*& mode() {
  static const char* m = "idle";
  return m;
}
inline double now_s() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
}

inline double& last_print() {
  static double t = 0;
  return t;
}

inline void print_and_reset(int out_w, int out_h) {
  Counters& k = c();
  const double t = now_s();
  const double dt = last_print() > 0 ? t - last_print() : 0.0;
  last_print() = t;
  if (k.any() && dt > 0)
    std::fprintf(stderr,
                 "GRABSTATS mode=%s dt=%.3f out=%dx%d events=%llu pos=%llu resize_req=%llu snap=%llu frames=%llu cap_wait=%llu cap_idle=%llu commits=%llu commit_ms=%.2f/%.2f damage_px=%.0f/%.0f present=%llu missed=%llu titlebar_renders=%llu strip_renders=%llu glass_builds=%llu glass_ms=%.2f\n",
                 mode(), dt, out_w, out_h, static_cast<unsigned long long>(k.events), static_cast<unsigned long long>(k.pos), static_cast<unsigned long long>(k.resize_req),
                 static_cast<unsigned long long>(k.snap), static_cast<unsigned long long>(k.frames), static_cast<unsigned long long>(k.cap_wait),
                 static_cast<unsigned long long>(k.cap_idle), static_cast<unsigned long long>(k.commits), k.commits ? k.commit_ms / static_cast<double>(k.commits) : 0.0,
                 k.commit_ms_max, k.commits ? k.damage_px / static_cast<double>(k.commits) : 0.0, k.damage_px_max, static_cast<unsigned long long>(k.present),
                 static_cast<unsigned long long>(k.missed), static_cast<unsigned long long>(k.titlebar_renders), static_cast<unsigned long long>(k.strip_renders),
                 static_cast<unsigned long long>(k.glass_builds), k.glass_ms);
  k = Counters{};
}

// Called from the frame handler: prints once a second.
inline void tick(int out_w, int out_h) {
  const double t = now_s();
  if (last_print() == 0) last_print() = t;
  if (t - last_print() >= 1.0) print_and_reset(out_w, out_h);
}

// A grab starts or ends: close the interval under the old mode, so a line never mixes modes (its dt= says how long it was).
inline void set_mode(const char* m, int out_w, int out_h) {
  print_and_reset(out_w, out_h);
  mode() = m;
}

}  // namespace fleetwm::grabstats
