#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <systemd/sd-bus.h>

#include "fleetkit.hpp"

namespace fleetwm::bar {

struct MediaState {
  bool available = false;
  std::string service;
  std::string identity;
  std::string title;
  std::string artist;
  std::string status;
  int64_t position_us = 0;
  int64_t duration_us = 0;
  bool can_play = false;
  bool can_go_previous = false;
  bool can_go_next = false;
};

struct MediaPlayer {
  std::string service;
  std::string identity;
  bool playing = false;
};

class Mpris {
 public:
  Mpris(kit::App& app, std::function<void()> changed);
  ~Mpris();
  Mpris(const Mpris&) = delete;
  Mpris& operator=(const Mpris&) = delete;

  bool start(const std::string& preferred_player = {});
  void set_preferred_player(const std::string& preferred_player);
  const MediaState& state() const { return state_; }
  const std::vector<MediaPlayer>& players() const { return players_; }
  const std::string& preferred_player() const { return preferred_player_; }
  double position_seconds() const;

  void toggle_play_pause();
  void previous();
  void next();
  void select_next_player();

 private:
  static int on_signal(sd_bus_message* message, void* userdata, sd_bus_error* error);
  void process();
  void schedule_refresh();
  void refresh();
  void refresh_state();
  void notify();
  std::vector<MediaPlayer> list_players();
  bool read_string(const std::string& service, const char* path, const char* iface, const char* property, std::string* out);
  bool read_bool(const std::string& service, const char* path, const char* iface, const char* property, bool* out);
  bool read_int64(const std::string& service, const char* path, const char* iface, const char* property, int64_t* out);
  void read_metadata(const std::string& service, std::string* title, std::string* artist, int64_t* duration);
  void call_player(const char* method);
  void reset_state();

  kit::App& app_;
  std::function<void()> changed_;
  sd_bus* bus_ = nullptr;
  sd_bus_slot* owner_slot_ = nullptr;
  sd_bus_slot* properties_slot_ = nullptr;
  int watch_id_ = 0;
  int refresh_timer_ = 0;
  std::string preferred_player_;
  std::vector<MediaPlayer> players_;
  MediaState state_;
  std::chrono::steady_clock::time_point position_stamp_{};
};

}  // namespace fleetwm::bar
