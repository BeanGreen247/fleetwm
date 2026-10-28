#pragma once

#include <string>
#include <vector>

// Settings for fullscreen programs and games, saved per user in game.toml:
//   * fullscreen_scaling: what happens when a fullscreen window's picture is not the size of the screen (an old 4:3 game on a 16:9 monitor, a
//     640x480 emulator): "fit" keeps the aspect ratio and adds black bars (default), "integer" does the same with whole-number zoom steps so pixels stay
//     square and crisp, "stretch" fills the screen, "off" leaves the picture at its own size in the corner.
//   * fullscreen_filter: how the zoomed picture is sampled: "auto" (nearest for integer zoom, smooth otherwise), "nearest" or "linear".
//   * adaptive_sync: switch the monitor to variable refresh rate while a window is fullscreen (when the monitor supports it).
//   * desktop_gpu: "integrated" (default) draws the desktop with the integrated GPU so the dedicated card can sleep and keep its power and heat for games, "discrete" uses the
//     dedicated card, "default" leaves the choice to wlroots. Needs a restart of the session. A program asks for the dedicated GPU by itself with `fleetwm-game --gpu discrete`
//     or PrefersNonDefaultGPU=true in its desktop entry; all screens keep working whichever card they are plugged into.
//   * gamemode: ask Feral GameMode (the gamemoded service: CPU governor, priorities, GPU clocks) to boost every game while it runs: Steam and Proton titles,
//     Wine programs, Lutris, gamescope, anything in gamemode_apps, and any other program that goes fullscreen (except video players and browsers).
//   * max_render_time_ms: start drawing a fullscreen game's frame this many milliseconds before the screen refreshes instead of right after the last one, so the
//     picture shows input that is up to a frame newer (0 = off, 1..16). Too small a budget misses the refresh; try 4 to 6 and watch the frame times.
//   * realtime: give the compositor a real-time scheduling class (through rtkit) while a game runs, so nothing delays a frame. Off by default.
//   * mode_switch: when a fullscreen window's picture is exactly the size of one of the monitor's own modes (an old game at 800x600), switch the monitor to that
//     mode while it runs and back afterwards, instead of scaling. Off by default.
//   * tuxthrottle: while a game runs, switch TuxThrottle's Game Mode on (and off after the last one), when `tuxthrottlectl` is installed (src/common/tuxthrottle.hpp).
//   * fast_desktop (off by default): while a game runs, or the power profile is Performance, the desktop switches to its fast look: glass effects (blur, translucent frames, glass pointers) are
//     off for the compositor and the bar until it ends, without touching theme.toml.
//   * fps_limit: the frame rate cap `fleetwm-game` gives a game through libstrangle (0 = no cap); lower fps for a quieter, cooler machine.

namespace fleetwm {

enum class FullscreenScaling { Fit, Integer, Stretch, Off };
enum class FullscreenFilter { Auto, Nearest, Linear };
// Which graphics card draws the desktop on a computer with two (a laptop or gaming PC with an integrated and a dedicated GPU).
enum class DesktopGpu { Integrated, Discrete, Default };

// Direct scanout: the fullscreen window's own buffer is shown by the display hardware without being composited (the least work and latency). With a game drawn on the dedicated
// card it can make the display flip wait for that card's queue (an unlimited game then shows 2 frames a second); Auto switches it off only for such windows (started with
// NVIDIA PRIME offload), Off always, On never touches it.
enum class DirectScanout { On, Auto, Off };

struct GameConfig {
  FullscreenScaling scaling = FullscreenScaling::Fit;
  FullscreenFilter filter = FullscreenFilter::Auto;
  bool adaptive_sync = true;
  DirectScanout direct_scanout = DirectScanout::On;
  DesktopGpu desktop_gpu = DesktopGpu::Integrated;
  bool gamemode = true;
  bool tuxthrottle = true;
  bool fast_desktop = false;  // off: glass and blur stay on while gaming (they are cheap enough); opt in to drop them
  int max_render_time_ms = 0;
  bool realtime = false;
  bool mode_switch = false;
  int fps_limit = 0;  // 0 = off, else 10..1000
  std::vector<std::string> gamemode_apps;  // extra app ids (or X11 classes, any part of the name) that count as games
  bool operator==(const GameConfig&) const = default;
};

inline constexpr int kFpsLimitMin = 10, kFpsLimitMax = 1000;
inline constexpr int kRenderTimeMax = 16;

std::string game_user_config_path();
// A missing or broken file gives the defaults; unknown words keep the default for that key.
GameConfig load_game_config();
// Throws std::runtime_error on I/O failure.
void save_game_config(const GameConfig& config);

std::string fullscreen_scaling_to_string(FullscreenScaling s);
FullscreenScaling fullscreen_scaling_from_string(const std::string& s, FullscreenScaling fallback);
std::string direct_scanout_to_string(DirectScanout d);
DirectScanout direct_scanout_from_string(const std::string& s, DirectScanout fallback);
std::string desktop_gpu_to_string(DesktopGpu g);
DesktopGpu desktop_gpu_from_string(const std::string& s, DesktopGpu fallback);
std::string fullscreen_filter_to_string(FullscreenFilter f);
FullscreenFilter fullscreen_filter_from_string(const std::string& s, FullscreenFilter fallback);

// How a picture of `src_w` x `src_h` is placed on a `dst_w` x `dst_h` screen: the size it is drawn at and the offset of its top-left corner.
struct FitRect {
  int w = 0, h = 0, x = 0, y = 0;
  bool scaled = false;  // false when the picture is drawn at its own size
  bool integer = false;  // whole-number zoom
};
FitRect fullscreen_fit(FullscreenScaling mode, int src_w, int src_h, int dst_w, int dst_h);

}  // namespace fleetwm
