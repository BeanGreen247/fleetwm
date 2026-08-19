#pragma once

#include <gtk/gtk.h>

#include <map>

#include "app_style.hpp"
#include "audio_mixer.hpp"
#include "theme.hpp"

namespace fleetwm::audiomixer {

// A compact, top-right-anchored layer-shell popup (not a GtkMenuButton/
// xdg_popup -- same "GTK-managed popup input routing proved unreliable"
// lesson the power menu already paid for, see power_menu_window.hpp) with
// a master volume slider on top, a separator, and one slider per
// currently-playing app below it. Backed by common::AudioMixer, which does
// all the actual PipeWire work; this class only owns GTK widgets and
// forwards slider moves to it.
class AudioMixerWindow {
 public:
  explicit AudioMixerWindow(GtkApplication* app);

 private:
  static void on_activate(GtkApplication* app, gpointer user_data);
  void build(GtkApplication* app);
  void apply_theme();
  void close();

  void on_master_update(int percent, bool muted, bool available);
  void on_streams_update(const std::vector<common::AudioStream>& streams);

  GtkWidget* build_master_row();
  GtkWidget* build_stream_row(const common::AudioStream& stream);

  static void on_master_slider_changed(GtkRange* range, gpointer user_data);
  static void on_master_mute_toggled(GtkButton* button, gpointer user_data);
  static void on_stream_slider_changed(GtkRange* range, gpointer user_data);
  static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode,
                                  GdkModifierType state, gpointer user_data);

  ThemeConfig theme_config_;
  common::AudioMixer mixer_;

  GtkWidget* window_ = nullptr;
  GtkWidget* master_slider_ = nullptr;
  GtkWidget* master_mute_button_ = nullptr;
  GtkWidget* master_unavailable_label_ = nullptr;
  GtkWidget* streams_box_ = nullptr;

  // Slider updates from AudioMixer must not re-trigger a set_*_volume()
  // call back into the mixer (that would fight the user's own drag with
  // every echoed PipeWire event) -- guarded the same way both master and
  // per-stream rows: true only while programmatically setting a slider's
  // value from an incoming report.
  bool updating_from_report_ = false;

  // node_id -> slider widget, so on_streams_update() can update an
  // existing row's slider in place instead of rebuilding the whole list
  // (which would fight the user's finger mid-drag) whenever anything about
  // any stream changes, and can still rebuild the whole list when the set
  // of streams itself changes (an app started/stopped playing).
  std::map<uint32_t, GtkWidget*> stream_sliders_;
};

}  // namespace fleetwm::audiomixer
