// Idle handling: turns the displays off and suspends the computer after the times set in
// Settings -> Power, using the profile for mains power or battery as it is right now.

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "battery_reading.hpp"
#include "output.hpp"
#include "power_config.hpp"
#include "server.hpp"

namespace fleetwm {

namespace {

int idle_timer_cb(void* data) {
  static_cast<Server*>(data)->on_idle_timer();
  return 0;
}

struct IdleInhibitor {
  Server* server;
  wlr_idle_inhibitor_v1* inhibitor;
  wl_listener destroy;
};

// Name of the program behind `pid`, from /proc/<pid>/comm.
std::string program_name(int pid) {
  std::string name;
  if (FILE* f = std::fopen(("/proc/" + std::to_string(pid) + "/comm").c_str(), "r")) {
    char buf[64] = {};
    if (std::fgets(buf, sizeof buf, f)) name = buf;
    std::fclose(f);
  }
  while (!name.empty() && (name.back() == '\n' || name.back() == ' ')) name.pop_back();
  return name.empty() ? "unknown" : name;
}

}  // namespace

void Server::init_idle() {
  idle_timer_ = wl_event_loop_add_timer(wl_display_get_event_loop(display_), idle_timer_cb, this);
  last_input_ = std::chrono::steady_clock::now();

  // Apps that need the screen to stay on (video players, presentations) ask for it
  // through idle-inhibit; while any such request is alive nothing is blanked.
  idle_inhibit_manager_ = wlr_idle_inhibit_v1_create(display_);
  new_idle_inhibitor_.notify = [](wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_idle_inhibitor_);
    auto* inhibitor = static_cast<wlr_idle_inhibitor_v1*>(data);
    auto* state = new IdleInhibitor{server, inhibitor, {}};
    state->destroy.notify = [](wl_listener* l, void*) {
      IdleInhibitor* s = wl_container_of(l, s, destroy);
      --s->server->idle_inhibitors_;
      std::erase_if(s->server->wayland_inhibitors_, [s](const auto& e) { return e.first == s->inhibitor; });
      wl_list_remove(&s->destroy.link);
      delete s;
    };
    wl_signal_add(&inhibitor->events.destroy, &state->destroy);
    ++server->idle_inhibitors_;
    pid_t pid = 0;
    if (inhibitor->resource) wl_client_get_credentials(wl_resource_get_client(inhibitor->resource), &pid, nullptr, nullptr);
    server->wayland_inhibitors_.emplace_back(inhibitor, static_cast<int>(pid));
    server->note_activity();  // the app asked for the screen: treat it as activity
  };
  wl_signal_add(&idle_inhibit_manager_->events.new_inhibitor, &new_idle_inhibitor_);

  reload_power_config();
}

void Server::ipc_idle_inhibit(bool on) {
  idle_inhibitors_ += on ? 1 : -1;
  if (on) note_activity();
}

std::string Server::idle_inhibitor_report() const {
  std::string out;
  for (const auto& [inhibitor, pid] : wayland_inhibitors_)
    out += "INHIBITOR wayland " + std::to_string(pid) + " " + program_name(pid) + "\n";
  return out;
}

void Server::reload_power_config() {
  power_config_ = load_power_config();
  arm_idle_timer(0);
}

void Server::note_activity() {
  last_input_ = std::chrono::steady_clock::now();
  if (displays_blanked_) set_displays_blanked(false);
}

void Server::arm_idle_timer(long seconds_from_now) {
  if (!idle_timer_) return;
  if (seconds_from_now < 0) {
    wl_event_source_timer_update(idle_timer_, 0);  // disarm
    return;
  }
  // Wake a moment after the deadline so the comparison below sees it as reached.
  wl_event_source_timer_update(idle_timer_, static_cast<int>(std::min<long>(seconds_from_now, 3600) * 1000 + 250));
}

void Server::on_idle_timer() {
  const long idle = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_input_).count();
  const bool on_battery = !find_battery_dir().empty() && !ac_online();
  const PowerProfile& profile = on_battery ? power_config_.battery : power_config_.ac;

  // An app is keeping the screen awake: check again later instead of acting.
  if (idle_inhibitors_ > 0) {
    arm_idle_timer(30);
    return;
  }

  const IdleActions actions = idle_actions(profile, idle);
  if (actions.suspend) {
    const char* command = std::getenv("FLEETWM_SUSPEND_COMMAND");
    if (!command || !*command) command = "systemctl suspend";
    if (fork() == 0) {
      execl("/bin/sh", "sh", "-c", command, static_cast<char*>(nullptr));
      _exit(127);
    }
    note_activity();  // after waking, start counting from zero again
    // Re-arm for whatever the profile wants next from "now".
    const IdleActions next = idle_actions(profile, 0);
    arm_idle_timer(next.next_check_seconds);
    return;
  }
  if (actions.blank_display && !displays_blanked_) set_displays_blanked(true);
  arm_idle_timer(actions.next_check_seconds);
}

void Server::set_displays_blanked(bool blanked) {
  if (displays_blanked_ == blanked) return;
  displays_blanked_ = blanked;
  for (const std::unique_ptr<Output>& output : outputs) {
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, !blanked);
    wlr_output_commit_state(output->wlr_output_ptr, &state);
    wlr_output_state_finish(&state);
    if (!blanked) wlr_output_schedule_frame(output->wlr_output_ptr);
  }
  if (!blanked) {
    // Coming back: the idle clock restarts, and the timer must be running again.
    const bool on_battery = !find_battery_dir().empty() && !ac_online();
    const IdleActions next = idle_actions(on_battery ? power_config_.battery : power_config_.ac, 0);
    arm_idle_timer(next.next_check_seconds);
  }
}

}  // namespace fleetwm
