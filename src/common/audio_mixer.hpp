#pragma once

#include <glib.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if FLEETWM_HAVE_PIPEWIRE
extern "C" {
#include <pipewire/pipewire.h>
}
#endif

namespace fleetwm::common {

// One playback stream (a running app's audio output), as PipeWire exposes
// it: each app that's actually playing something is its own
// "Stream/Output/Audio" node in the graph, same data pavucontrol/wpctl
// show per-app.
struct AudioStream {
  uint32_t node_id = 0;
  std::string label;
  int volume_percent = 0;
  bool muted = false;
};

// Read/write audio control, built for the audio mixer popup and Settings
// tab (unlike bar/volume_source.cpp, which is read-only master-volume-only,
// ADR 0005). Two things tracked, both event-driven via the same PipeWire
// registry+param-subscription pattern volume_source.cpp already
// established:
//  - the default sink's master volume + mute (get and set)
//  - every currently-playing app's own stream volume + mute (get and set)
//
// No wpctl-subprocess fallback here (unlike volume_source.cpp) -- wpctl has
// no scriptable per-app volume subcommand worth building a parser for, and
// this is opt-in UI a user explicitly opens, not a background bar stat that
// must always show *something*. If PipeWire isn't available at build time
// or connect time, start() returns false and the caller shows an
// unavailable state instead.
class AudioMixer {
 public:
  using MasterCallback = std::function<void(int percent, bool muted, bool available)>;
  using StreamsCallback = std::function<void(const std::vector<AudioStream>& streams)>;

  // Starts the backend and begins delivering updates via the two
  // callbacks (each called once immediately once its initial state is
  // known, then again on every subsequent change). Both are stored and
  // called for the lifetime of this object -- must outlive it. Returns
  // false if PipeWire isn't available (not compiled in, or couldn't
  // connect) -- caller should show an unavailable/disabled UI rather than
  // calling any of the setters below.
  bool start(MasterCallback on_master, StreamsCallback on_streams);

  ~AudioMixer();

  // No-ops (safe to call, silently ignored) if start() returned false or
  // the given node_id isn't currently tracked.
  void set_master_volume(int percent);
  void set_master_muted(bool muted);
  void set_stream_volume(uint32_t node_id, int percent);
  void set_stream_muted(uint32_t node_id, bool muted);

 private:
#if FLEETWM_HAVE_PIPEWIRE
  struct NodeState {
    AudioMixer* self = nullptr;
    uint32_t node_id = 0;
    pw_proxy* proxy = nullptr;
    spa_hook listener{};
    uint32_t n_channels = 2;  // updated once a real channelVolumes array arrives
    int volume_percent = 0;
    bool muted = false;
    std::string label;  // empty for the master sink node
  };

  bool connect();
  void teardown();
  static void write_node_volume(pw_proxy* proxy, uint32_t n_channels, int percent);
  static void write_node_muted(pw_proxy* proxy, bool muted);

  void report_master();
  void report_streams();
  static gboolean on_idle_report(gpointer data);

  // Applies a Props param event (channelVolumes + mute) to `node`. Shared
  // by both the master sink and every per-app stream -- the wire format is
  // identical, only what happens after (report_master vs report_streams)
  // differs, handled by the two thin per-kind wrappers below.
  static void apply_props_param(NodeState* node, const spa_pod* param);
  static void on_sink_node_param(void* data, int seq, uint32_t id, uint32_t index, uint32_t next,
                                  const spa_pod* param);
  static void on_sink_node_info(void* data, const struct pw_node_info* info);
  static void on_stream_node_param(void* data, int seq, uint32_t id, uint32_t index, uint32_t next,
                                    const spa_pod* param);
  static void on_stream_node_info(void* data, const struct pw_node_info* info);

  static void on_registry_global(void* data, uint32_t id, uint32_t permissions, const char* type,
                                  uint32_t version, const spa_dict* props);
  static void on_registry_global_remove(void* data, uint32_t id);
  static int on_metadata_property(void* data, uint32_t subject, const char* key, const char* type,
                                   const char* value);

  MasterCallback on_master_;
  StreamsCallback on_streams_;

  pw_thread_loop* pw_loop_ = nullptr;
  pw_context* pw_context_ = nullptr;
  pw_core* pw_core_ = nullptr;
  pw_registry* pw_registry_ = nullptr;
  spa_hook registry_listener_{};
  spa_hook core_listener_{};

  pw_proxy* metadata_proxy_ = nullptr;
  spa_hook metadata_listener_{};
  std::string default_sink_name_;

  uint32_t sink_node_id_ = 0xffffffff;  // SPA_ID_INVALID
  std::unique_ptr<NodeState> sink_node_;

  // node_id -> stream state. unique_ptr so pointers handed to PipeWire as
  // listener user_data stay stable even as the map itself is mutated
  // (insert/erase never invalidates other elements' addresses either way
  // with std::map, but unique_ptr also makes ownership on erase explicit).
  std::map<uint32_t, std::unique_ptr<NodeState>> streams_;
  bool available_ = false;
#endif
};

}  // namespace fleetwm::common
