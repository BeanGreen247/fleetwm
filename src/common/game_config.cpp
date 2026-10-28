#include "game_config.hpp"

#include "fleettoml.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fleetwm {

namespace fs = std::filesystem;

std::string game_user_config_path() { return (config_internal::config_home() / "fleetwm" / "game.toml").string(); }

std::string fullscreen_scaling_to_string(FullscreenScaling s) {
  switch (s) {
    case FullscreenScaling::Integer: return "integer";
    case FullscreenScaling::Stretch: return "stretch";
    case FullscreenScaling::Off: return "off";
    default: return "fit";
  }
}

FullscreenScaling fullscreen_scaling_from_string(const std::string& s, FullscreenScaling fallback) {
  if (s == "fit") return FullscreenScaling::Fit;
  if (s == "integer") return FullscreenScaling::Integer;
  if (s == "stretch") return FullscreenScaling::Stretch;
  if (s == "off") return FullscreenScaling::Off;
  return fallback;
}

std::string fullscreen_filter_to_string(FullscreenFilter f) {
  switch (f) {
    case FullscreenFilter::Nearest: return "nearest";
    case FullscreenFilter::Linear: return "linear";
    default: return "auto";
  }
}

FullscreenFilter fullscreen_filter_from_string(const std::string& s, FullscreenFilter fallback) {
  if (s == "auto") return FullscreenFilter::Auto;
  if (s == "nearest") return FullscreenFilter::Nearest;
  if (s == "linear") return FullscreenFilter::Linear;
  return fallback;
}

std::string direct_scanout_to_string(DirectScanout d) {
  switch (d) {
    case DirectScanout::Auto: return "auto";
    case DirectScanout::Off: return "off";
    default: return "on";
  }
}

DirectScanout direct_scanout_from_string(const std::string& s, DirectScanout fallback) {
  if (s == "on") return DirectScanout::On;
  if (s == "auto") return DirectScanout::Auto;
  if (s == "off") return DirectScanout::Off;
  return fallback;
}

std::string all_games_gpu_to_string(AllGamesGpu g) {
  switch (g) {
    case AllGamesGpu::Integrated: return "integrated";
    case AllGamesGpu::Dedicated: return "dedicated";
    default: return "none";
  }
}

AllGamesGpu all_games_gpu_from_string(const std::string& s, AllGamesGpu fallback) {
  if (s == "integrated") return AllGamesGpu::Integrated;
  if (s == "dedicated") return AllGamesGpu::Dedicated;
  if (s == "none") return AllGamesGpu::None;
  return fallback;
}

std::string desktop_gpu_to_string(DesktopGpu g) {
  switch (g) {
    case DesktopGpu::Discrete: return "discrete";
    case DesktopGpu::Default: return "default";
    default: return "integrated";
  }
}

DesktopGpu desktop_gpu_from_string(const std::string& s, DesktopGpu fallback) {
  if (s == "integrated") return DesktopGpu::Integrated;
  if (s == "discrete") return DesktopGpu::Discrete;
  if (s == "default") return DesktopGpu::Default;
  return fallback;
}

GameConfig load_game_config() {
  GameConfig config;
  const fs::path path = game_user_config_path();
  std::error_code ec;
  if (!fs::exists(path, ec)) return config;
  try {
    toml::table root = toml::parse_file(path.string());
    if (auto v = root["fullscreen_scaling"].value<std::string>()) config.scaling = fullscreen_scaling_from_string(*v, config.scaling);
    if (auto v = root["fullscreen_filter"].value<std::string>()) config.filter = fullscreen_filter_from_string(*v, config.filter);
    if (auto v = root["adaptive_sync"].value<bool>()) config.adaptive_sync = *v;
    if (auto v = root["direct_scanout"].value<std::string>()) config.direct_scanout = direct_scanout_from_string(*v, config.direct_scanout);
    if (auto v = root["all_games_gpu"].value<std::string>()) config.all_games_gpu = all_games_gpu_from_string(*v, config.all_games_gpu);
    if (auto v = root["all_games_gpu_stamp"].value<int64_t>()) config.all_games_gpu_stamp = *v;
    if (auto v = root["desktop_gpu"].value<std::string>()) config.desktop_gpu = desktop_gpu_from_string(*v, config.desktop_gpu);
    if (auto v = root["gamemode"].value<bool>()) config.gamemode = *v;
    if (auto v = root["tuxthrottle"].value<bool>()) config.tuxthrottle = *v;
    if (auto v = root["fast_desktop"].value<bool>()) config.fast_desktop = *v;
    if (auto v = root["max_render_time_ms"].value<int64_t>()) config.max_render_time_ms = std::clamp(static_cast<int>(*v), 0, kRenderTimeMax);
    if (auto v = root["realtime"].value<bool>()) config.realtime = *v;
    if (auto v = root["mode_switch"].value<bool>()) config.mode_switch = *v;
    if (toml::array* a = root["gamemode_apps"].as_array())
      for (const toml::node& n : *a)
        if (auto v = n.value<std::string>()) config.gamemode_apps.push_back(*v);
    if (auto v = root["fps_limit"].value<int64_t>()) config.fps_limit = *v <= 0 ? 0 : std::clamp(static_cast<int>(*v), kFpsLimitMin, kFpsLimitMax);
  } catch (const toml::parse_error&) {
    return GameConfig{};
  }
  return config;
}

void save_game_config(const GameConfig& config) {
  const fs::path path = game_user_config_path();
  fs::create_directories(path.parent_path());
  toml::table root;
  root.insert_or_assign("fullscreen_scaling", fullscreen_scaling_to_string(config.scaling));
  root.insert_or_assign("fullscreen_filter", fullscreen_filter_to_string(config.filter));
  root.insert_or_assign("adaptive_sync", config.adaptive_sync);
  root.insert_or_assign("direct_scanout", direct_scanout_to_string(config.direct_scanout));
  root.insert_or_assign("desktop_gpu", desktop_gpu_to_string(config.desktop_gpu));
  root.insert_or_assign("all_games_gpu", all_games_gpu_to_string(config.all_games_gpu));
  root.insert_or_assign("all_games_gpu_stamp", static_cast<int64_t>(config.all_games_gpu_stamp));
  root.insert_or_assign("gamemode", config.gamemode);
  root.insert_or_assign("tuxthrottle", config.tuxthrottle);
  root.insert_or_assign("fast_desktop", config.fast_desktop);
  root.insert_or_assign("max_render_time_ms", static_cast<int64_t>(std::clamp(config.max_render_time_ms, 0, kRenderTimeMax)));
  root.insert_or_assign("realtime", config.realtime);
  root.insert_or_assign("mode_switch", config.mode_switch);
  toml::array apps;
  for (const std::string& a : config.gamemode_apps) apps.push_back(a);
  root.insert_or_assign("gamemode_apps", apps);
  root.insert_or_assign("fps_limit", static_cast<int64_t>(config.fps_limit <= 0 ? 0 : std::clamp(config.fps_limit, kFpsLimitMin, kFpsLimitMax)));
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
    out << root;
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
}

FitRect fullscreen_fit(FullscreenScaling mode, int src_w, int src_h, int dst_w, int dst_h) {
  FitRect r;
  if (src_w < 1 || src_h < 1 || dst_w < 1 || dst_h < 1 || mode == FullscreenScaling::Off || (src_w == dst_w && src_h == dst_h)) {
    r.w = src_w;
    r.h = src_h;
    return r;
  }
  r.scaled = true;
  if (mode == FullscreenScaling::Stretch) {
    r.w = dst_w;
    r.h = dst_h;
    return r;
  }
  const double fit = std::min(static_cast<double>(dst_w) / src_w, static_cast<double>(dst_h) / src_h);
  if (mode == FullscreenScaling::Integer && fit >= 1.0) {
    const int k = static_cast<int>(std::floor(fit));
    r.w = src_w * k;
    r.h = src_h * k;
    r.integer = true;
  } else {
    r.w = std::max(1, static_cast<int>(std::lround(src_w * fit)));
    r.h = std::max(1, static_cast<int>(std::lround(src_h * fit)));
  }
  r.x = (dst_w - r.w) / 2;
  r.y = (dst_h - r.h) / 2;
  return r;
}

}  // namespace fleetwm
