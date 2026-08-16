#pragma once

#include <glib.h>

#include <functional>
#include <string>

#if FLEETWM_HAVE_PIPEWIRE
extern "C" {
#include <pipewire/pipewire.h>
}
#endif

namespace fleetwm::bar {

// Reports the default audio sink's volume as an integer percentage
// (ADR 0005: "Volume%"). Two backends, mutually exclusive at build time:
//
// - PipeWire (FLEETWM_HAVE_PIPEWIRE): event-driven via a registry +
//   metadata + node-proxy subscription -- zero cost at idle, instant
//   update on change. This is the primary mechanism per ADR 0005.
// - wpctl subprocess, polled on a timer: fallback used only when
//   PipeWire dev headers weren't available at build time.
//
// Either backend reports "N/A" (via an empty formatted string, callers
// decide the label prefix) if it can't determine a volume at all --
// matches ADR 0005's GPU%/general "no crash, just show N/A" contract.
class VolumeSource {
 public:
  using Callback = std::function<void(int percent, bool available)>;

  // Starts the backend and begins delivering updates via `on_update`
  // (called once immediately if an initial value is already known, then
  // again on every subsequent change/poll). `on_update` is stored and
  // called for the lifetime of this object -- must outlive it.
  void start(Callback on_update);

  ~VolumeSource();

 private:
#if FLEETWM_HAVE_PIPEWIRE
  bool start_pipewire();
  void teardown_pipewire();
#endif
  void start_wpctl_fallback();
  static gboolean on_wpctl_poll_tick(gpointer user_data);
  void poll_wpctl_once();

  Callback on_update_;

#if FLEETWM_HAVE_PIPEWIRE
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
  pw_proxy* sink_node_proxy_ = nullptr;
  spa_hook sink_node_listener_{};

  // report() is called from PipeWire's own thread-loop thread (see
  // start_pipewire()) and marshals onto the GLib main context via
  // g_idle_add rather than touching on_update_/GTK state directly, since
  // GTK is not thread-safe. on_idle_report() is the g_idle_add
  // trampoline that runs the deferred call on the main thread.
  void report(int percent, bool available);
  static gboolean on_idle_report(gpointer data);

  static void on_registry_global(void* data, uint32_t id, uint32_t permissions, const char* type,
                                  uint32_t version, const spa_dict* props);
  static int on_metadata_property(void* data, uint32_t subject, const char* key,
                                   const char* type, const char* value);
  static void on_sink_node_param(void* data, int seq, uint32_t id, uint32_t index,
                                  uint32_t next, const spa_pod* param);
  static void on_sink_node_info(void* data, const struct pw_node_info* info);
#endif

  guint wpctl_timer_id_ = 0;
};

}  // namespace fleetwm::bar
