#include "volume_source.hpp"

#include <cstdio>
#include <cstdlib>

#if FLEETWM_HAVE_PIPEWIRE
extern "C" {
#include <pipewire/extensions/metadata.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>
}
#include "pipewire_json.hpp"
#endif

namespace fleetwm::bar {

namespace {
constexpr guint kWpctlPollIntervalMs = 5000;
}  // namespace

void VolumeSource::start(Callback on_update) {
  on_update_ = std::move(on_update);
#if FLEETWM_HAVE_PIPEWIRE
  if (start_pipewire()) {
    return;
  }
#endif
  start_wpctl_fallback();
}

VolumeSource::~VolumeSource() {
  if (wpctl_timer_id_ != 0) {
    g_source_remove(wpctl_timer_id_);
  }
#if FLEETWM_HAVE_PIPEWIRE
  teardown_pipewire();
#endif
}

// -- wpctl fallback (polled) -------------------------------------------

void VolumeSource::start_wpctl_fallback() {
  poll_wpctl_once();
  wpctl_timer_id_ = g_timeout_add(kWpctlPollIntervalMs, on_wpctl_poll_tick, this);
}

gboolean VolumeSource::on_wpctl_poll_tick(gpointer user_data) {
  static_cast<VolumeSource*>(user_data)->poll_wpctl_once();
  return G_SOURCE_CONTINUE;
}

void VolumeSource::poll_wpctl_once() {
  // "wpctl get-volume @DEFAULT_AUDIO_SINK@" prints e.g. "Volume: 0.45\n"
  // (or "Volume: 0.45 [MUTED]\n"). Parsed with a plain fscanf-style read
  // rather than a JSON/structured API since this is already the
  // last-resort fallback (ADR 0005) -- kept deliberately simple.
  FILE* pipe = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
  if (!pipe) {
    on_update_(0, false);
    return;
  }
  char buf[128] = {};
  size_t n = std::fread(buf, 1, sizeof(buf) - 1, pipe);
  pclose(pipe);
  (void)n;

  double fraction = 0.0;
  if (std::sscanf(buf, "Volume: %lf", &fraction) == 1) {
    on_update_(static_cast<int>(fraction * 100.0 + 0.5), true);
  } else {
    on_update_(0, false);  // wpctl not installed, or no default sink
  }
}

#if FLEETWM_HAVE_PIPEWIRE

// -- PipeWire event-driven backend --------------------------------------
//
// Sequence: connect to core -> registry listener finds the "default"
// PW_TYPE_INTERFACE_Metadata global -> bind it and listen for its
// "default.audio.sink" property (a JSON string naming the sink) ->
// registry listener (same pass) also finds the matching
// PW_TYPE_INTERFACE_Node global by name -> bind that node and
// subscribe_params(SPA_PARAM_Props) -> its param event carries
// SPA_PROP_channelVolumes, averaged across channels for a single %.
//
// Runs on PipeWire's own pw_thread_loop (its callbacks fire off the GTK
// main thread) -- report() marshals back onto the GLib main context via
// g_idle_add before touching on_update_/any GTK state, since GTK is not
// thread-safe.

namespace {
struct ReportData {
  VolumeSource* self;
  int percent;
  bool available;
};
}  // namespace

void VolumeSource::report(int percent, bool available) {
  g_idle_add(on_idle_report, new ReportData{this, percent, available});
}

gboolean VolumeSource::on_idle_report(gpointer data) {
  auto* rd = static_cast<ReportData*>(data);
  if (rd->self->on_update_) {
    rd->self->on_update_(rd->percent, rd->available);
  }
  delete rd;
  return G_SOURCE_REMOVE;
}

void VolumeSource::on_sink_node_param(void* data, int, uint32_t id, uint32_t, uint32_t,
                                       const spa_pod* param) {
  auto* self = static_cast<VolumeSource*>(data);
  if (id != SPA_PARAM_Props || !param) {
    return;
  }
  const spa_pod_prop* prop =
      spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes);
  if (!prop) {
    return;
  }
  uint32_t n_values = 0;
  void* values = spa_pod_get_array(&prop->value, &n_values);
  if (!values || n_values == 0) {
    return;
  }
  // channelVolumes is an array of Float, linear scale (0.0-1.0+).
  const float* volumes = static_cast<const float*>(values);
  float sum = 0.0f;
  for (uint32_t i = 0; i < n_values; ++i) {
    sum += volumes[i];
  }
  float average = sum / static_cast<float>(n_values);
  self->report(static_cast<int>(average * 100.0f + 0.5f), true);
}

void VolumeSource::on_sink_node_info(void*, const pw_node_info*) {
  // No fields needed from node info itself -- only here because
  // pw_node_events requires both callbacks to be populated as a pair;
  // the actual volume comes from the param event above.
}

int VolumeSource::on_metadata_property(void* data, uint32_t, const char* key, const char*,
                                        const char* value) {
  auto* self = static_cast<VolumeSource*>(data);
  if (!key || std::string(key) != "default.audio.sink" || !value) {
    return 0;
  }
  // Value is JSON like {"name":"alsa_output.xyz"} -- see pipewire_json.hpp
  // for why this doesn't pull in a full JSON dependency for one field.
  std::string name = fleetwm::common::extract_json_string_field(value, "name");
  if (!name.empty()) {
    self->default_sink_name_ = name;
  }
  return 0;
}

void VolumeSource::on_registry_global(void* data, uint32_t id, uint32_t, const char* type,
                                       uint32_t, const spa_dict* props) {
  auto* self = static_cast<VolumeSource*>(data);
  if (!type) {
    return;
  }

  if (std::string(type) == PW_TYPE_INTERFACE_Metadata && !self->metadata_proxy_) {
    const char* name = props ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : nullptr;
    if (!name || std::string(name) != "default") {
      return;
    }
    self->metadata_proxy_ = static_cast<pw_proxy*>(
        pw_registry_bind(self->pw_registry_, id, type, PW_VERSION_METADATA, 0));
    if (!self->metadata_proxy_) {
      return;
    }
    static const pw_metadata_events metadata_events = {
        .version = PW_VERSION_METADATA_EVENTS,
        .property = on_metadata_property,
    };
    pw_proxy_add_object_listener(self->metadata_proxy_, &self->metadata_listener_,
                                  &metadata_events, self);
    return;
  }

  if (std::string(type) == PW_TYPE_INTERFACE_Node && self->sink_node_proxy_ == nullptr) {
    const char* media_class = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : nullptr;
    const char* node_name = props ? spa_dict_lookup(props, "node.name") : nullptr;
    if (!media_class || std::string(media_class) != "Audio/Sink") {
      return;
    }
    if (!node_name || self->default_sink_name_.empty() ||
        std::string(node_name) != self->default_sink_name_) {
      return;
    }
    self->sink_node_id_ = id;
    self->sink_node_proxy_ = static_cast<pw_proxy*>(
        pw_registry_bind(self->pw_registry_, id, type, PW_VERSION_NODE, 0));
    if (!self->sink_node_proxy_) {
      return;
    }
    static const pw_node_events node_events = {
        .version = PW_VERSION_NODE_EVENTS,
        .info = on_sink_node_info,
        .param = on_sink_node_param,
    };
    pw_proxy_add_object_listener(self->sink_node_proxy_, &self->sink_node_listener_, &node_events,
                                  self);
    uint32_t param_id = SPA_PARAM_Props;
    pw_node_subscribe_params(reinterpret_cast<pw_node*>(self->sink_node_proxy_), &param_id, 1);
  }
}

bool VolumeSource::start_pipewire() {
  pw_init(nullptr, nullptr);

  pw_loop_ = pw_thread_loop_new("fleetwm-bar-pipewire", nullptr);
  if (!pw_loop_) {
    return false;
  }
  if (pw_thread_loop_start(pw_loop_) < 0) {
    pw_thread_loop_destroy(pw_loop_);
    pw_loop_ = nullptr;
    return false;
  }

  pw_thread_loop_lock(pw_loop_);
  pw_context_ = pw_context_new(pw_thread_loop_get_loop(pw_loop_), nullptr, 0);
  if (!pw_context_) {
    pw_thread_loop_unlock(pw_loop_);
    teardown_pipewire();
    return false;
  }
  pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
  if (!pw_core_) {
    pw_thread_loop_unlock(pw_loop_);
    teardown_pipewire();
    return false;
  }
  pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
  if (!pw_registry_) {
    pw_thread_loop_unlock(pw_loop_);
    teardown_pipewire();
    return false;
  }
  static const pw_registry_events registry_events = {
      .version = PW_VERSION_REGISTRY_EVENTS,
      .global = on_registry_global,
  };
  pw_registry_add_listener(pw_registry_, &registry_listener_, &registry_events, this);
  pw_thread_loop_unlock(pw_loop_);
  return true;
}

void VolumeSource::teardown_pipewire() {
  if (pw_loop_) {
    pw_thread_loop_stop(pw_loop_);
  }
  if (sink_node_proxy_) {
    pw_proxy_destroy(sink_node_proxy_);
    sink_node_proxy_ = nullptr;
  }
  if (metadata_proxy_) {
    pw_proxy_destroy(metadata_proxy_);
    metadata_proxy_ = nullptr;
  }
  if (pw_core_) {
    pw_core_disconnect(pw_core_);
    pw_core_ = nullptr;
  }
  if (pw_context_) {
    pw_context_destroy(pw_context_);
    pw_context_ = nullptr;
  }
  if (pw_loop_) {
    pw_thread_loop_destroy(pw_loop_);
    pw_loop_ = nullptr;
  }
}

#endif  // FLEETWM_HAVE_PIPEWIRE

}  // namespace fleetwm::bar
