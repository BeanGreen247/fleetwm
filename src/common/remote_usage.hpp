#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>

// What the remote desktop servers are doing right now (written by fleetwm-remote every 10 s while a viewer is connected, and at once when a viewer comes or goes; read
// by the bar and Settings) and the daily tally it keeps (read by Settings). "Sent" is what goes to the viewers (the picture),
// "received" what comes back (keys and mouse).

namespace fleetwm {

enum RemoteProto { kProtoVnc = 0, kProtoRdp = 1 };

struct RemoteLiveProto {
  int clients = 0;
  uint64_t tx = 0, rx = 0;         // bytes since the daemon started
  double tx_rate = 0, rx_rate = 0;  // bytes per second over the last second
  double cpu_percent = 0;           // of one core, the viewers' threads of this protocol
  uint64_t ram = 0;                 // bytes the viewers' buffers hold
};

struct RemoteLive {
  RemoteLiveProto proto[2];
  bool valid = false;  // false when there is no file or it is stale
  int total_clients() const { return proto[0].clients + proto[1].clients; }
};

// <XDG_RUNTIME_DIR>/fleetwm-remote/ : the directory the bar watches, and the stats file in it.
std::string remote_stats_dir();
std::string remote_stats_path();

std::string format_remote_live(const RemoteLive& live, time_t now);
// Parses the file text; `*written` is the time it was written. False when it is not a stats file.
bool parse_remote_live(const std::string& text, RemoteLive* out, time_t* written);
// Reads the file; invalid when it is missing or older than 25 seconds (it is rewritten every 10 s and at every change).
RemoteLive load_remote_live();

// "VNC", "RDP", "VNC + RDP", or "" when nobody is connected.
std::string remote_live_label(const RemoteLive& live);
// The bar's hover text.
std::string remote_live_tooltip(const RemoteLive& live);

std::string remote_format_bytes(double bytes);  // "0 B", "340 KB", "12.4 MB", "1.20 GB"
std::string remote_format_rate(double bytes_per_second);
std::string remote_format_duration(uint64_t seconds);  // "45 s", "12 min", "3 h 05 min"

// ---- the tally ----
struct DayUsage {
  uint64_t tx = 0, rx = 0;
  uint64_t active_s = 0;  // seconds with at least one viewer connected
  uint64_t cpu_ms = 0;    // CPU the viewers' threads used
  DayUsage& operator+=(const DayUsage& o) {
    tx += o.tx;
    rx += o.rx;
    active_s += o.active_s;
    cpu_ms += o.cpu_ms;
    return *this;
  }
};

struct UsageHistory {
  std::map<std::string, std::array<DayUsage, 2>> days;  // "2026-10-26" -> [vnc, rdp]
  std::array<DayUsage, 2> all_time;
};

constexpr int kUsageKeepDays = 60;

std::string usage_path();  // $XDG_STATE_HOME/fleetwm/remote-usage.txt
UsageHistory load_usage();
// Throws std::runtime_error on I/O failure.
void save_usage(const UsageHistory& h);
std::string usage_day_key(time_t t);  // local date
void add_usage(UsageHistory* h, const std::string& day, int proto, const DayUsage& delta);

struct UsageAverage {
  double tx = 0, rx = 0, active_s = 0, cpu_s = 0;  // per day
};
// The mean per day over the last `days` days including today (days without use count as zero).
UsageAverage average_usage(const UsageHistory& h, int proto, int days, time_t now);

}  // namespace fleetwm
