#include "kwin_input.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>
#include <optional>

extern "C" {
#include <libinput.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
}

#include "server.hpp"

extern "C" const sd_bus_vtable fleetwm_kwin_input_vtable[];
extern "C" const sd_bus_vtable fleetwm_kwin_manager_vtable[];

namespace fleetwm {

struct KwinInput::Impl {
  struct Dev {
    wlr_input_device* wlr = nullptr;
    libinput_device* li = nullptr;
    std::string path, name, sysname;
    bool touchpad = false, pointer = false;
    std::optional<bool> enabled, tap, natural, dwt, left, flat;  // set from outside; kept and re-applied
    std::optional<double> accel;
    sd_bus_slot* slot = nullptr;
  };
  Server* server = nullptr;
  sd_bus* bus = nullptr;
  wl_event_source* source = nullptr;
  std::vector<std::unique_ptr<Dev>> devices;
  sd_bus_slot* manager_slot = nullptr;

  Dev* find(wlr_input_device* d) {
    for (auto& p : devices)
      if (p->wlr == d) return p.get();
    return nullptr;
  }

  static bool read(Dev* d, const std::string& prop) {
    libinput_device* li = d->li;
    if (prop == "touchpad") return d->touchpad;
    if (prop == "pointer") return d->pointer;
    if (prop == "keyboard") return false;
    if (prop == "supportsDisableEvents") return libinput_device_config_send_events_get_modes(li) & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
    if (prop == "enabled") return libinput_device_config_send_events_get_mode(li) != LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
    if (prop == "tapToClick") return libinput_device_config_tap_get_finger_count(li) > 0 && libinput_device_config_tap_get_enabled(li) == LIBINPUT_CONFIG_TAP_ENABLED;
    if (prop == "naturalScroll") return libinput_device_config_scroll_has_natural_scroll(li) && libinput_device_config_scroll_get_natural_scroll_enabled(li);
    if (prop == "disableWhileTyping") return libinput_device_config_dwt_is_available(li) && libinput_device_config_dwt_get_enabled(li) == LIBINPUT_CONFIG_DWT_ENABLED;
    if (prop == "leftHanded") return libinput_device_config_left_handed_is_available(li) && libinput_device_config_left_handed_get(li);
    if (prop == "pointerAccelerationProfileFlat") return libinput_device_config_accel_is_available(li) && libinput_device_config_accel_get_profile(li) == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    if (prop == "pointerAccelerationProfileAdaptive") return libinput_device_config_accel_is_available(li) && libinput_device_config_accel_get_profile(li) == LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    return false;
  }

  static void write(Dev* d, const std::string& prop, bool v) {
    libinput_device* li = d->li;
    if (prop == "enabled") {
      d->enabled = v;
      libinput_device_config_send_events_set_mode(li, v ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
    } else if (prop == "tapToClick") {
      d->tap = v;
      if (libinput_device_config_tap_get_finger_count(li) > 0) libinput_device_config_tap_set_enabled(li, v ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
    } else if (prop == "naturalScroll") {
      d->natural = v;
      if (libinput_device_config_scroll_has_natural_scroll(li)) libinput_device_config_scroll_set_natural_scroll_enabled(li, v ? 1 : 0);
    } else if (prop == "disableWhileTyping") {
      d->dwt = v;
      if (libinput_device_config_dwt_is_available(li)) libinput_device_config_dwt_set_enabled(li, v ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
    } else if (prop == "pointerAccelerationProfileFlat" || prop == "pointerAccelerationProfileAdaptive") {
      const bool flat = prop == "pointerAccelerationProfileFlat" ? v : !v;  // one switch seen from both names
      d->flat = flat;
      if (libinput_device_config_accel_is_available(li))
        libinput_device_config_accel_set_profile(li, flat ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
    } else if (prop == "leftHanded") {
      d->left = v;
      if (libinput_device_config_left_handed_is_available(li)) libinput_device_config_left_handed_set(li, v ? 1 : 0);
    }
  }
};

}  // namespace fleetwm

using fleetwm::KwinInput;

extern "C" int fleetwm_kwin_prop_get(sd_bus*, const char*, const char*, const char* prop, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* d = static_cast<KwinInput::Impl::Dev*>(userdata);
  if (!std::strcmp(prop, "name")) return sd_bus_message_append(reply, "s", d->name.c_str());
  if (!std::strcmp(prop, "sysName")) return sd_bus_message_append(reply, "s", d->sysname.c_str());
  return sd_bus_message_append(reply, "b", KwinInput::Impl::read(d, prop) ? 1 : 0);
}

extern "C" int fleetwm_kwin_accel_get(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* d = static_cast<KwinInput::Impl::Dev*>(userdata);
  return sd_bus_message_append(reply, "d", libinput_device_config_accel_is_available(d->li) ? libinput_device_config_accel_get_speed(d->li) : 0.0);
}

extern "C" int fleetwm_kwin_accel_set(sd_bus*, const char*, const char*, const char*, sd_bus_message* value, void* userdata, sd_bus_error*) {
  auto* d = static_cast<KwinInput::Impl::Dev*>(userdata);
  double v = 0;
  const int r = sd_bus_message_read(value, "d", &v);
  if (r < 0) return r;
  v = v < -1 ? -1 : (v > 1 ? 1 : v);
  d->accel = v;
  if (libinput_device_config_accel_is_available(d->li)) libinput_device_config_accel_set_speed(d->li, v);
  return 1;
}

extern "C" int fleetwm_kwin_names_get(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
  auto* impl = static_cast<KwinInput::Impl*>(userdata);
  int r = sd_bus_message_open_container(reply, 'a', "s");
  if (r < 0) return r;
  for (auto& d : impl->devices) {
    r = sd_bus_message_append(reply, "s", d->sysname.c_str());
    if (r < 0) return r;
  }
  return sd_bus_message_close_container(reply);
}

extern "C" int fleetwm_kwin_prop_set(sd_bus*, const char*, const char*, const char* prop, sd_bus_message* value, void* userdata, sd_bus_error*) {
  auto* d = static_cast<KwinInput::Impl::Dev*>(userdata);
  int v = 0;
  const int r = sd_bus_message_read(value, "b", &v);
  if (r < 0) return r;
  KwinInput::Impl::write(d, prop, v != 0);
  return 1;
}

namespace fleetwm {

KwinInput::KwinInput(Server* server) : impl_(new Impl) { impl_->server = server; }

KwinInput::~KwinInput() {
  for (auto& d : impl_->devices)
    if (d->slot) sd_bus_slot_unref(d->slot);
  if (impl_->manager_slot) sd_bus_slot_unref(impl_->manager_slot);
  if (impl_->source) wl_event_source_remove(impl_->source);
  if (impl_->bus) sd_bus_flush_close_unref(impl_->bus);
}

bool KwinInput::start() {
  if (sd_bus_open_user(&impl_->bus) < 0) {
    impl_->bus = nullptr;
    return false;
  }
  if (sd_bus_request_name(impl_->bus, "org.kde.KWin", 0) < 0) {  // another KWin (a KDE session) owns it
    sd_bus_flush_close_unref(impl_->bus);
    impl_->bus = nullptr;
    return false;
  }
  sd_bus_add_object_vtable(impl_->bus, &impl_->manager_slot, "/org/kde/KWin/InputDevice", "org.kde.KWin.InputDeviceManager", fleetwm_kwin_manager_vtable, impl_.get());
  impl_->source = wl_event_loop_add_fd(
      wl_display_get_event_loop(impl_->server->display()), sd_bus_get_fd(impl_->bus), WL_EVENT_READABLE,
      [](int, uint32_t, void* data) -> int {
        auto* impl = static_cast<Impl*>(data);
        while (sd_bus_process(impl->bus, nullptr) > 0) {
        }
        sd_bus_flush(impl->bus);
        return 0;
      },
      impl_.get());
  return true;
}

void KwinInput::device_added(wlr_input_device* device) {
  if (!impl_->bus || impl_->find(device) || !wlr_input_device_is_libinput(device)) return;
  libinput_device* li = wlr_libinput_get_device_handle(device);
  if (!li) return;
  auto d = std::make_unique<Impl::Dev>();
  d->wlr = device;
  d->li = li;
  d->name = libinput_device_get_name(li) ? libinput_device_get_name(li) : "";
  d->sysname = libinput_device_get_sysname(li) ? libinput_device_get_sysname(li) : "";
  d->pointer = libinput_device_has_capability(li, LIBINPUT_DEVICE_CAP_POINTER);
  d->touchpad = d->pointer && libinput_device_config_tap_get_finger_count(li) > 0;  // only touchpads have tap fingers
  d->path = "/org/kde/KWin/InputDevice/" + d->sysname;
  if (sd_bus_add_object_vtable(impl_->bus, &d->slot, d->path.c_str(), "org.kde.KWin.InputDevice", fleetwm_kwin_input_vtable, d.get()) < 0) return;
  impl_->devices.push_back(std::move(d));
}

void KwinInput::device_removed(wlr_input_device* device) {
  auto& v = impl_->devices;
  for (auto it = v.begin(); it != v.end(); ++it)
    if ((*it)->wlr == device) {
      if ((*it)->slot) sd_bus_slot_unref((*it)->slot);
      v.erase(it);
      return;
    }
}

void KwinInput::reapply(wlr_input_device* device) {
  Impl::Dev* d = impl_->find(device);
  if (!d) return;
  if (d->enabled) Impl::write(d, "enabled", *d->enabled);
  if (d->tap) Impl::write(d, "tapToClick", *d->tap);
  if (d->natural) Impl::write(d, "naturalScroll", *d->natural);
  if (d->dwt) Impl::write(d, "disableWhileTyping", *d->dwt);
  if (d->left) Impl::write(d, "leftHanded", *d->left);
  if (d->flat) Impl::write(d, "pointerAccelerationProfileFlat", *d->flat);
  if (d->accel && libinput_device_config_accel_is_available(d->li)) libinput_device_config_accel_set_speed(d->li, *d->accel);
}

}  // namespace fleetwm
