#include "audio_mixer.hpp"

#if FLEETWM_HAVE_PIPEWIRE

extern "C" {
#include <pipewire/extensions/metadata.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
}

#include "pipewire_json.hpp"

namespace fleetwm::common {

bool AudioMixer::start(MasterCallback on_master, StreamsCallback on_streams) {
  on_master_ = std::move(on_master);
  on_streams_ = std::move(on_streams);
  sink_node_ = std::make_unique<NodeState>();
  sink_node_->self = this;
  if (!connect()) {
    available_ = false;
    if (on_master_) {
      on_master_(0, false, false);
    }
    if (on_streams_) {
      on_streams_({});
    }
    return false;
  }
  return true;
}

AudioMixer::~AudioMixer() {
  teardown();
}

// -- reporting (marshaled onto the GLib main context) ----------------------
//
// Called from PipeWire's own thread-loop thread; report_master()/
// report_streams() themselves only read plain data already copied into
// NodeState by apply_props_param(), so calling them straight from
// g_idle_add's main-thread callback (not from the PipeWire thread that
// received the event) is safe -- same split VolumeSource::report() uses.

void AudioMixer::report_master() {
  if (on_master_) {
    on_master_(sink_node_->volume_percent, sink_node_->muted, available_);
  }
}

void AudioMixer::report_streams() {
  if (!on_streams_) {
    return;
  }
  std::vector<AudioStream> streams;
  streams.reserve(streams_.size());
  for (const auto& [id, node] : streams_) {
    streams.push_back(AudioStream{id, node->label, node->volume_percent, node->muted});
  }
  on_streams_(streams);
}

gboolean AudioMixer::on_idle_report(gpointer data) {
  auto* self = static_cast<AudioMixer*>(data);
  self->report_master();
  self->report_streams();
  return G_SOURCE_REMOVE;
}

// -- shared Props-param parsing ---------------------------------------------

void AudioMixer::apply_props_param(NodeState* node, const spa_pod* param) {
  if (const spa_pod_prop* vol_prop = spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes)) {
    uint32_t n_values = 0;
    void* values = spa_pod_get_array(&vol_prop->value, &n_values);
    if (values && n_values > 0) {
      const float* volumes = static_cast<const float*>(values);
      float sum = 0.0f;
      for (uint32_t i = 0; i < n_values; ++i) {
        sum += volumes[i];
      }
      node->n_channels = n_values;
      node->volume_percent = static_cast<int>(sum / static_cast<float>(n_values) * 100.0f + 0.5f);
    }
  }
  if (const spa_pod_prop* mute_prop = spa_pod_find_prop(param, nullptr, SPA_PROP_mute)) {
    bool muted = false;
    if (spa_pod_get_bool(&mute_prop->value, &muted) == 0) {
      node->muted = muted;
    }
  }
}

// -- master sink node --------------------------------------------------------

void AudioMixer::on_sink_node_param(void* data, int, uint32_t id, uint32_t, uint32_t,
                                     const spa_pod* param) {
  auto* self = static_cast<AudioMixer*>(data);
  if (id != SPA_PARAM_Props || !param) {
    return;
  }
  apply_props_param(self->sink_node_.get(), param);
  self->available_ = true;
  g_idle_add(on_idle_report, self);
}

void AudioMixer::on_sink_node_info(void*, const pw_node_info*) {
  // No fields needed -- present only because pw_node_events requires both
  // callbacks populated as a pair (same as VolumeSource's equivalent).
}

// -- per-app stream nodes -----------------------------------------------------

void AudioMixer::on_stream_node_param(void* data, int, uint32_t id, uint32_t, uint32_t,
                                       const spa_pod* param) {
  auto* node = static_cast<NodeState*>(data);
  if (id != SPA_PARAM_Props || !param) {
    return;
  }
  apply_props_param(node, param);
  node->self->available_ = true;
  g_idle_add(on_idle_report, node->self);
}

void AudioMixer::on_stream_node_info(void*, const pw_node_info*) {
  // Same as on_sink_node_info -- required pair member, unused.
}

// -- registry: discovers the default sink and every playback stream --------

int AudioMixer::on_metadata_property(void* data, uint32_t, const char* key, const char*,
                                      const char* value) {
  auto* self = static_cast<AudioMixer*>(data);
  if (!key || std::string(key) != "default.audio.sink" || !value) {
    return 0;
  }
  std::string name = extract_json_string_field(value, "name");
  if (!name.empty()) {
    self->default_sink_name_ = name;
  }
  return 0;
}

void AudioMixer::on_registry_global(void* data, uint32_t id, uint32_t, const char* type, uint32_t,
                                     const spa_dict* props) {
  auto* self = static_cast<AudioMixer*>(data);
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

  if (std::string(type) != PW_TYPE_INTERFACE_Node) {
    return;
  }
  const char* media_class = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : nullptr;
  if (!media_class) {
    return;
  }

  if (std::string(media_class) == "Audio/Sink" && self->sink_node_->proxy == nullptr) {
    const char* node_name = spa_dict_lookup(props, "node.name");
    if (!node_name || self->default_sink_name_.empty() ||
        std::string(node_name) != self->default_sink_name_) {
      return;
    }
    self->sink_node_id_ = id;
    self->sink_node_->node_id = id;
    self->sink_node_->proxy = static_cast<pw_proxy*>(
        pw_registry_bind(self->pw_registry_, id, type, PW_VERSION_NODE, 0));
    if (!self->sink_node_->proxy) {
      return;
    }
    static const pw_node_events node_events = {
        .version = PW_VERSION_NODE_EVENTS,
        .info = on_sink_node_info,
        .param = on_sink_node_param,
    };
    pw_proxy_add_object_listener(self->sink_node_->proxy, &self->sink_node_->listener,
                                  &node_events, self);
    uint32_t param_id = SPA_PARAM_Props;
    pw_node_subscribe_params(reinterpret_cast<pw_node*>(self->sink_node_->proxy), &param_id, 1);
    return;
  }

  if (std::string(media_class) == "Stream/Output/Audio" && !self->streams_.count(id)) {
    // Label preference: application.name (set by every well-behaved
    // PipeWire/PulseAudio client, e.g. "Firefox", "Spotify"), falling
    // back to node.description, then node.name, then the raw node id --
    // same fallback chain pavucontrol effectively uses.
    const char* label = spa_dict_lookup(props, PW_KEY_APP_NAME);
    if (!label) {
      label = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    }
    if (!label) {
      label = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    }

    auto node = std::make_unique<NodeState>();
    node->self = self;
    node->node_id = id;
    node->label = label ? label : ("Stream " + std::to_string(id));
    node->proxy =
        static_cast<pw_proxy*>(pw_registry_bind(self->pw_registry_, id, type, PW_VERSION_NODE, 0));
    if (!node->proxy) {
      return;
    }
    static const pw_node_events stream_events = {
        .version = PW_VERSION_NODE_EVENTS,
        .info = on_stream_node_info,
        .param = on_stream_node_param,
    };
    pw_proxy_add_object_listener(node->proxy, &node->listener, &stream_events, node.get());
    uint32_t param_id = SPA_PARAM_Props;
    pw_node_subscribe_params(reinterpret_cast<pw_node*>(node->proxy), &param_id, 1);
    self->streams_[id] = std::move(node);
  }
}

void AudioMixer::on_registry_global_remove(void* data, uint32_t id) {
  auto* self = static_cast<AudioMixer*>(data);
  auto it = self->streams_.find(id);
  if (it == self->streams_.end()) {
    return;
  }
  if (it->second->proxy) {
    pw_proxy_destroy(it->second->proxy);
  }
  self->streams_.erase(it);
  g_idle_add(on_idle_report, self);
}

// -- connect/teardown --------------------------------------------------------

bool AudioMixer::connect() {
  pw_init(nullptr, nullptr);

  pw_loop_ = pw_thread_loop_new("fleetwm-audiomixer-pipewire", nullptr);
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
    teardown();
    return false;
  }
  pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
  if (!pw_core_) {
    pw_thread_loop_unlock(pw_loop_);
    teardown();
    return false;
  }
  pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
  if (!pw_registry_) {
    pw_thread_loop_unlock(pw_loop_);
    teardown();
    return false;
  }
  static const pw_registry_events registry_events = {
      .version = PW_VERSION_REGISTRY_EVENTS,
      .global = on_registry_global,
      .global_remove = on_registry_global_remove,
  };
  pw_registry_add_listener(pw_registry_, &registry_listener_, &registry_events, this);
  pw_thread_loop_unlock(pw_loop_);
  return true;
}

void AudioMixer::teardown() {
  if (pw_loop_) {
    pw_thread_loop_stop(pw_loop_);
  }
  for (auto& [id, node] : streams_) {
    if (node->proxy) {
      pw_proxy_destroy(node->proxy);
    }
  }
  streams_.clear();
  if (sink_node_ && sink_node_->proxy) {
    pw_proxy_destroy(sink_node_->proxy);
    sink_node_->proxy = nullptr;
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

// -- writers (set volume/mute) ------------------------------------------------
//
// Builds a Props param object with a fresh spa_pod_builder over a stack
// buffer and pushes it with pw_node_set_param() -- the write-side
// counterpart of the channelVolumes/mute Props param these same nodes emit
// on the read side above. Runs from whatever thread calls set_*() (the GTK
// main thread in every real caller); pw_node_set_param() itself is a
// PipeWire client call, not something that needs the thread-loop lock held
// by the caller (matches how pw_registry_bind()'s callers above only take
// the lock around registry/context/core setup, not every subsequent call).

void AudioMixer::write_node_volume(pw_proxy* proxy, uint32_t n_channels, int percent) {
  if (!proxy) {
    return;
  }
  float fraction = static_cast<float>(percent) / 100.0f;
  if (fraction < 0.0f) {
    fraction = 0.0f;
  }
  constexpr uint32_t kMaxChannels = 32;
  uint32_t channels = n_channels == 0 ? 2 : n_channels;
  if (channels > kMaxChannels) {
    channels = kMaxChannels;
  }
  float values[kMaxChannels];
  for (uint32_t i = 0; i < channels; ++i) {
    values[i] = fraction;
  }

  uint8_t buffer[1024];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  spa_pod_frame frame;
  spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop(&builder, SPA_PROP_channelVolumes, 0);
  spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, channels, values);
  const spa_pod* pod = static_cast<const spa_pod*>(spa_pod_builder_pop(&builder, &frame));

  pw_node_set_param(reinterpret_cast<pw_node*>(proxy), SPA_PARAM_Props, 0, pod);
}

void AudioMixer::write_node_muted(pw_proxy* proxy, bool muted) {
  if (!proxy) {
    return;
  }
  uint8_t buffer[256];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const spa_pod* pod = static_cast<const spa_pod*>(spa_pod_builder_add_object(
      &builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_mute, SPA_POD_Bool(muted)));
  pw_node_set_param(reinterpret_cast<pw_node*>(proxy), SPA_PARAM_Props, 0, pod);
}

void AudioMixer::set_master_volume(int percent) {
  if (!sink_node_ || !sink_node_->proxy) {
    return;
  }
  write_node_volume(sink_node_->proxy, sink_node_->n_channels, percent);
}

void AudioMixer::set_master_muted(bool muted) {
  if (!sink_node_ || !sink_node_->proxy) {
    return;
  }
  write_node_muted(sink_node_->proxy, muted);
}

void AudioMixer::set_stream_volume(uint32_t node_id, int percent) {
  auto it = streams_.find(node_id);
  if (it == streams_.end()) {
    return;
  }
  write_node_volume(it->second->proxy, it->second->n_channels, percent);
}

void AudioMixer::set_stream_muted(uint32_t node_id, bool muted) {
  auto it = streams_.find(node_id);
  if (it == streams_.end()) {
    return;
  }
  write_node_muted(it->second->proxy, muted);
}

}  // namespace fleetwm::common

#else  // !FLEETWM_HAVE_PIPEWIRE

namespace fleetwm::common {

bool AudioMixer::start(MasterCallback on_master, StreamsCallback on_streams) {
  if (on_master) {
    on_master(0, false, false);
  }
  if (on_streams) {
    on_streams({});
  }
  return false;
}

AudioMixer::~AudioMixer() = default;
void AudioMixer::set_master_volume(int) {}
void AudioMixer::set_master_muted(bool) {}
void AudioMixer::set_stream_volume(uint32_t, int) {}
void AudioMixer::set_stream_muted(uint32_t, bool) {}

}  // namespace fleetwm::common

#endif  // FLEETWM_HAVE_PIPEWIRE
