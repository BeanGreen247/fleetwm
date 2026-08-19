#include "audio_mixer_window.hpp"

#include <gtk4-layer-shell.h>

#include <string>

namespace fleetwm::audiomixer {

namespace {
constexpr int kBarHeightPx = 24;  // must match src/bar/bar_window.cpp's kBarHeightPx
constexpr int kPopupWidthPx = 280;
}  // namespace

AudioMixerWindow::AudioMixerWindow(GtkApplication* app) {
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
}

void AudioMixerWindow::on_activate(GtkApplication* app, gpointer user_data) {
  static_cast<AudioMixerWindow*>(user_data)->build(app);
}

void AudioMixerWindow::build(GtkApplication* app) {
  theme_config_ = load_theme_config();

  window_ = gtk_application_window_new(app);
  gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
  gtk_widget_add_css_class(window_, "fleetwm-panel");
  gtk_widget_add_css_class(window_, "fleetwm-audiomixer-window");

  gtk_layer_init_for_window(GTK_WINDOW(window_));
  gtk_layer_set_layer(GTK_WINDOW(window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window_), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_TOP, kBarHeightPx + 6);
  gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, 8);
  // NOT gtk_window_set_default_size(-1, ...) here -- gtk4-layer-shell
  // forwards a GTK "natural"/unset dimension straight through as
  // zwlr_layer_surface_v1.set_size's literal -1, which the wire protocol
  // reinterprets as the *unsigned* wl_fixed value 4294967295 (UINT32_MAX)
  // -- the compositor then fatals the whole connection with "width and
  // height can't be greater than INT32_MAX" (reproduced live on
  // fleetwm-dev: the popup never even got a first frame). A plain
  // min-width request on the child widget avoids the layer-surface size
  // request entirely and lets height auto-negotiate from content, same
  // as every non-layer-shell GTK window already does.
  GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(card, kPopupWidthPx, -1);
  gtk_widget_add_css_class(card, "fleetwm-audiomixer-card");
  gtk_widget_set_margin_top(card, 10);
  gtk_widget_set_margin_bottom(card, 10);
  gtk_widget_set_margin_start(card, 12);
  gtk_widget_set_margin_end(card, 12);

  gtk_box_append(GTK_BOX(card), build_master_row());

  master_unavailable_label_ = gtk_label_new("Audio unavailable (no PipeWire)");
  gtk_widget_add_css_class(master_unavailable_label_, "fleetwm-stat");
  gtk_widget_set_visible(master_unavailable_label_, FALSE);
  gtk_box_append(GTK_BOX(card), master_unavailable_label_);

  GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(separator, 4);
  gtk_widget_set_margin_bottom(separator, 4);
  gtk_box_append(GTK_BOX(card), separator);

  streams_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append(GTK_BOX(card), streams_box_);

  gtk_window_set_child(GTK_WINDOW(window_), card);

  GtkEventController* key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), this);
  gtk_widget_add_controller(window_, key_controller);

  apply_theme();
  gtk_window_present(GTK_WINDOW(window_));

  mixer_.start(
      [this](int percent, bool muted, bool available) { on_master_update(percent, muted, available); },
      [this](const std::vector<common::AudioStream>& streams) { on_streams_update(streams); });
}

void AudioMixerWindow::apply_theme() {
  apply_app_style(theme_config_);
}

void AudioMixerWindow::close() {
  g_application_quit(g_application_get_default());
}

GtkWidget* AudioMixerWindow::build_master_row() {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

  master_mute_button_ = gtk_button_new();
  GtkWidget* icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
  gtk_button_set_child(GTK_BUTTON(master_mute_button_), icon);
  gtk_widget_add_css_class(master_mute_button_, "fleetwm-workspace-button");
  g_signal_connect(master_mute_button_, "clicked", G_CALLBACK(on_master_mute_toggled), this);
  gtk_box_append(GTK_BOX(row), master_mute_button_);

  master_slider_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value(GTK_RANGE(master_slider_), 0);
  gtk_widget_set_hexpand(master_slider_, TRUE);
  gtk_scale_set_draw_value(GTK_SCALE(master_slider_), TRUE);
  g_signal_connect(master_slider_, "value-changed", G_CALLBACK(on_master_slider_changed), this);
  gtk_box_append(GTK_BOX(row), master_slider_);

  return row;
}

GtkWidget* AudioMixerWindow::build_stream_row(const common::AudioStream& stream) {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  GtkWidget* label = gtk_label_new(stream.label.c_str());
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_add_css_class(label, "fleetwm-stat");
  gtk_box_append(GTK_BOX(row), label);

  GtkWidget* slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value(GTK_RANGE(slider), stream.volume_percent);
  gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
  g_object_set_data(G_OBJECT(slider), "fleetwm-node-id", GUINT_TO_POINTER(stream.node_id));
  g_signal_connect(slider, "value-changed", G_CALLBACK(on_stream_slider_changed), this);
  gtk_box_append(GTK_BOX(row), slider);

  stream_sliders_[stream.node_id] = slider;
  return row;
}

void AudioMixerWindow::on_master_update(int percent, bool muted, bool available) {
  gtk_widget_set_visible(master_unavailable_label_, !available);
  gtk_widget_set_sensitive(master_slider_, available);
  gtk_widget_set_sensitive(master_mute_button_, available);
  if (!available) {
    return;
  }
  updating_from_report_ = true;
  gtk_range_set_value(GTK_RANGE(master_slider_), percent);
  updating_from_report_ = false;

  GtkWidget* current_icon = gtk_button_get_child(GTK_BUTTON(master_mute_button_));
  const char* icon_name = muted             ? "audio-volume-muted-symbolic"
                           : percent >= 50   ? "audio-volume-high-symbolic"
                           : percent > 0     ? "audio-volume-medium-symbolic"
                                              : "audio-volume-low-symbolic";
  gtk_image_set_from_icon_name(GTK_IMAGE(current_icon), icon_name);
}

void AudioMixerWindow::on_streams_update(const std::vector<common::AudioStream>& streams) {
  // Rebuild-if-the-set-changed, update-in-place otherwise: the set of
  // active streams only changes when an app starts/stops playing, which
  // is rare compared to volume ticks arriving for an app the user is
  // actively dragging.
  bool set_changed = streams.size() != stream_sliders_.size();
  if (!set_changed) {
    for (const auto& stream : streams) {
      if (!stream_sliders_.count(stream.node_id)) {
        set_changed = true;
        break;
      }
    }
  }

  if (set_changed) {
    GtkWidget* child = gtk_widget_get_first_child(streams_box_);
    while (child) {
      GtkWidget* next = gtk_widget_get_next_sibling(child);
      gtk_box_remove(GTK_BOX(streams_box_), child);
      child = next;
    }
    stream_sliders_.clear();
    for (const auto& stream : streams) {
      gtk_box_append(GTK_BOX(streams_box_), build_stream_row(stream));
    }
    return;
  }

  updating_from_report_ = true;
  for (const auto& stream : streams) {
    auto it = stream_sliders_.find(stream.node_id);
    if (it != stream_sliders_.end()) {
      gtk_range_set_value(GTK_RANGE(it->second), stream.volume_percent);
    }
  }
  updating_from_report_ = false;
}

void AudioMixerWindow::on_master_slider_changed(GtkRange* range, gpointer user_data) {
  auto* self = static_cast<AudioMixerWindow*>(user_data);
  if (self->updating_from_report_) {
    return;
  }
  self->mixer_.set_master_volume(static_cast<int>(gtk_range_get_value(range) + 0.5));
}

void AudioMixerWindow::on_master_mute_toggled(GtkButton*, gpointer user_data) {
  auto* self = static_cast<AudioMixerWindow*>(user_data);
  // Toggling mute is read back from the next report (on_master_update),
  // not tracked locally here -- avoids a second source of truth for muted
  // state drifting from what PipeWire actually reports.
  GtkWidget* current_icon = gtk_button_get_child(GTK_BUTTON(self->master_mute_button_));
  const char* current_name = nullptr;
  g_object_get(current_icon, "icon-name", &current_name, nullptr);
  bool currently_muted = current_name && std::string(current_name) == "audio-volume-muted-symbolic";
  self->mixer_.set_master_muted(!currently_muted);
}

void AudioMixerWindow::on_stream_slider_changed(GtkRange* range, gpointer user_data) {
  auto* self = static_cast<AudioMixerWindow*>(user_data);
  if (self->updating_from_report_) {
    return;
  }
  uint32_t node_id =
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(range), "fleetwm-node-id"));
  self->mixer_.set_stream_volume(node_id, static_cast<int>(gtk_range_get_value(range) + 0.5));
}

gboolean AudioMixerWindow::on_key_pressed(GtkEventControllerKey*, guint keyval, guint,
                                           GdkModifierType, gpointer user_data) {
  if (keyval == GDK_KEY_Escape) {
    static_cast<AudioMixerWindow*>(user_data)->close();
    return TRUE;
  }
  return FALSE;
}

}  // namespace fleetwm::audiomixer
