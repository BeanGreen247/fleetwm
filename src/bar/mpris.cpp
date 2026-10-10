#include "mpris.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace fleetwm::bar {

namespace {
constexpr const char* kPlayerPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kPropertiesIface = "org.freedesktop.DBus.Properties";
constexpr const char* kMprisPrefix = "org.mpris.MediaPlayer2.";

bool is_mpris_name(const char* name) {
  return name && std::strncmp(name, kMprisPrefix, std::strlen(kMprisPrefix)) == 0;
}

bool same_state(const MediaState& a, const MediaState& b) {
  return a.available == b.available && a.service == b.service && a.identity == b.identity && a.title == b.title && a.artist == b.artist &&
         a.status == b.status && a.position_us == b.position_us && a.duration_us == b.duration_us && a.can_play == b.can_play &&
         a.can_go_previous == b.can_go_previous && a.can_go_next == b.can_go_next;
}

}  // namespace

Mpris::Mpris(kit::App& app, std::function<void()> changed) : app_(app), changed_(std::move(changed)) {}

Mpris::~Mpris() {
  if (watch_id_) app_.unwatch(watch_id_);
  if (refresh_timer_) app_.unwatch(refresh_timer_);
  if (owner_slot_) sd_bus_slot_unref(owner_slot_);
  if (properties_slot_) sd_bus_slot_unref(properties_slot_);
  if (bus_) sd_bus_flush_close_unref(bus_);
}

bool Mpris::start(const std::string& preferred_player) {
  preferred_player_ = preferred_player;
  if (sd_bus_open_user(&bus_) < 0) return false;
  sd_bus_set_method_call_timeout(bus_, 1000000);
  if (sd_bus_add_match(bus_, &owner_slot_,
                       "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
                       &Mpris::on_signal, this) < 0)
    return false;
  if (sd_bus_add_match(bus_, &properties_slot_, "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
                       &Mpris::on_signal, this) < 0)
    return false;
  watch_id_ = app_.watch_fd(sd_bus_get_fd(bus_), [this] { process(); });
  refresh();
  return true;
}

void Mpris::set_preferred_player(const std::string& preferred_player) {
  if (preferred_player_ == preferred_player) return;
  preferred_player_ = preferred_player;
  refresh();
}

void Mpris::process() {
  while (bus_) {
    const int r = sd_bus_process(bus_, nullptr);
    if (r <= 0) break;
  }
  if (bus_) sd_bus_flush(bus_);
}

int Mpris::on_signal(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* self = static_cast<Mpris*>(userdata);
  const char* member = sd_bus_message_get_member(message);
  if (member && std::strcmp(member, "NameOwnerChanged") == 0) {
    const char* name = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner) >= 0 && is_mpris_name(name)) self->schedule_refresh();
  } else if (member && std::strcmp(member, "PropertiesChanged") == 0) {
    self->schedule_refresh();
  }
  return 0;
}

void Mpris::schedule_refresh() {
  if (refresh_timer_) return;
  refresh_timer_ = app_.add_oneshot(100, [this] {
    refresh_timer_ = 0;
    refresh();
  });
}

std::vector<MediaPlayer> Mpris::list_players() {
  std::vector<MediaPlayer> out;
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message* reply = nullptr;
  if (!bus_ || sd_bus_call_method(bus_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", &error,
                                  &reply, nullptr) < 0) {
    sd_bus_error_free(&error);
    return out;
  }
  if (sd_bus_message_enter_container(reply, 'a', "s") >= 0) {
    const char* name = nullptr;
    while (sd_bus_message_read(reply, "s", &name) > 0) {
      if (!is_mpris_name(name)) continue;
      MediaPlayer p;
      p.service = name;
      read_string(p.service, kPlayerPath, "org.mpris.MediaPlayer2", "Identity", &p.identity);
      std::string status;
      read_string(p.service, kPlayerPath, kPlayerIface, "PlaybackStatus", &status);
      p.playing = status == "Playing";
      out.push_back(std::move(p));
    }
    sd_bus_message_exit_container(reply);
  }
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  std::sort(out.begin(), out.end(), [](const MediaPlayer& a, const MediaPlayer& b) { return a.service < b.service; });
  return out;
}

bool Mpris::read_string(const std::string& service, const char* path, const char* iface, const char* property, std::string* out) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  char* value = nullptr;
  const int r = sd_bus_get_property_string(bus_, service.c_str(), path, iface, property, &error, &value);
  if (r >= 0 && value) *out = value;
  std::free(value);
  sd_bus_error_free(&error);
  return r >= 0;
}

bool Mpris::read_bool(const std::string& service, const char* path, const char* iface, const char* property, bool* out) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  int value = 0;
  const int r = sd_bus_get_property_trivial(bus_, service.c_str(), path, iface, property, &error, 'b', &value);
  if (r >= 0) *out = value != 0;
  sd_bus_error_free(&error);
  return r >= 0;
}

bool Mpris::read_int64(const std::string& service, const char* path, const char* iface, const char* property, int64_t* out) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  int64_t value = 0;
  const int r = sd_bus_get_property_trivial(bus_, service.c_str(), path, iface, property, &error, 'x', &value);
  if (r >= 0) *out = value;
  sd_bus_error_free(&error);
  return r >= 0;
}

void Mpris::read_metadata(const std::string& service, std::string* title, std::string* artist, int64_t* duration) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message* message = nullptr;
  if (sd_bus_get_property(bus_, service.c_str(), kPlayerPath, kPlayerIface, "Metadata", &error, &message, "a{sv}") < 0) {
    sd_bus_error_free(&error);
    return;
  }
  if (sd_bus_message_enter_container(message, 'a', "{sv}") < 0) {
    sd_bus_message_unref(message);
    sd_bus_error_free(&error);
    return;
  }
  while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
    const char* key = nullptr;
    if (sd_bus_message_read(message, "s", &key) < 0 || sd_bus_message_enter_container(message, 'v', nullptr) < 0) {
      sd_bus_message_exit_container(message);
      continue;
    }
    const char* signature = sd_bus_message_get_signature(message, true);
    if (key && std::strcmp(key, "xesam:title") == 0 && signature && std::strcmp(signature, "s") == 0) {
      const char* value = nullptr;
      if (sd_bus_message_read(message, "s", &value) >= 0 && value) *title = value;
    } else if (key && std::strcmp(key, "xesam:artist") == 0 && signature && std::strcmp(signature, "as") == 0) {
      if (sd_bus_message_enter_container(message, 'a', "s") >= 0) {
        const char* value = nullptr;
        if (sd_bus_message_read(message, "s", &value) >= 0 && value) *artist = value;
        sd_bus_message_exit_container(message);
      }
    } else if (key && std::strcmp(key, "mpris:length") == 0 && signature && std::strcmp(signature, "x") == 0) {
      int64_t value = 0;
      if (sd_bus_message_read(message, "x", &value) >= 0) *duration = value;
    } else {
      sd_bus_message_skip(message, nullptr);
    }
    sd_bus_message_exit_container(message);
    sd_bus_message_exit_container(message);
  }
  sd_bus_message_exit_container(message);
  sd_bus_message_unref(message);
  sd_bus_error_free(&error);
}

void Mpris::reset_state() {
  MediaState empty;
  if (!same_state(state_, empty)) {
    state_ = empty;
    notify();
  }
}

void Mpris::refresh_state() {
  if (!state_.available || state_.service.empty()) {
    refresh();
    return;
  }
  const MediaState old = state_;
  read_string(state_.service, kPlayerPath, kPlayerIface, "PlaybackStatus", &state_.status);
  read_bool(state_.service, kPlayerPath, kPlayerIface, "CanPlay", &state_.can_play);
  read_bool(state_.service, kPlayerPath, kPlayerIface, "CanGoPrevious", &state_.can_go_previous);
  read_bool(state_.service, kPlayerPath, kPlayerIface, "CanGoNext", &state_.can_go_next);
  read_int64(state_.service, kPlayerPath, kPlayerIface, "Position", &state_.position_us);
  read_metadata(state_.service, &state_.title, &state_.artist, &state_.duration_us);
  position_stamp_ = std::chrono::steady_clock::now();
  if (!same_state(old, state_)) notify();
}

void Mpris::refresh() {
  const std::vector<MediaPlayer> players = list_players();
  const std::string previous = state_.service;
  players_ = players;
  const MediaPlayer* selected = nullptr;
  for (const MediaPlayer& p : players) {
    if (!preferred_player_.empty() && (p.service == preferred_player_ || p.identity == preferred_player_)) {
      selected = &p;
      break;
    }
  }
  if (!selected && preferred_player_.empty()) {
    for (const MediaPlayer& p : players)
      if (p.service == previous && !previous.empty()) {
        selected = &p;
        break;
      }
  }
  if (!selected) {
    for (const MediaPlayer& p : players)
      if (p.playing) {
        selected = &p;
        break;
      }
  }
  if (!selected && !players.empty()) selected = &players.front();
  if (!selected) {
    reset_state();
    return;
  }
  MediaState old = state_;
  state_ = {};
  state_.available = true;
  state_.service = selected->service;
  state_.identity = selected->identity;
  refresh_state();
  if (!same_state(old, state_)) notify();
}

void Mpris::notify() {
  if (changed_) changed_();
}

double Mpris::position_seconds() const {
  if (!state_.available) return 0;
  double position = static_cast<double>(state_.position_us) / 1000000.0;
  if (state_.status == "Playing") {
    position += std::chrono::duration<double>(std::chrono::steady_clock::now() - position_stamp_).count();
  }
  if (state_.duration_us > 0) position = std::min(position, static_cast<double>(state_.duration_us) / 1000000.0);
  return std::max(0.0, position);
}

void Mpris::call_player(const char* method) {
  if (!state_.available) return;
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_call_method(bus_, state_.service.c_str(), kPlayerPath, kPlayerIface, method, &error, nullptr, nullptr);
  sd_bus_error_free(&error);
  refresh_state();
}

void Mpris::toggle_play_pause() { call_player("PlayPause"); }
void Mpris::previous() { call_player("Previous"); }
void Mpris::next() { call_player("Next"); }

void Mpris::select_next_player() {
  if (players_.size() < 2) return;
  auto it = std::find_if(players_.begin(), players_.end(), [&](const MediaPlayer& p) { return p.service == state_.service; });
  const size_t next_index = it == players_.end() ? 0 : (static_cast<size_t>(std::distance(players_.begin(), it)) + 1) % players_.size();
  preferred_player_ = players_[next_index].service;
  refresh();
}

}  // namespace fleetwm::bar
