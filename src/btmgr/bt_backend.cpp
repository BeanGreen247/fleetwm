#include "bt_backend.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace fleetwm::bt {

namespace {

constexpr const char* kBluez = "org.bluez";
constexpr const char* kAdapterIface = "org.bluez.Adapter1";
constexpr const char* kDeviceIface = "org.bluez.Device1";
constexpr const char* kBatteryIface = "org.bluez.Battery1";
constexpr const char* kAgentPath = "/org/fleetwm/bluetooth/agent";
constexpr uint64_t kConnectTimeoutUs = 30ULL * 1000000;
constexpr uint64_t kPairTimeoutUs = 90ULL * 1000000;

class Err {
 public:
  ~Err() { sd_bus_error_free(&e); }
  sd_bus_error e = SD_BUS_ERROR_NULL;
  std::string text() const { return friendly_error(e.name ? e.name : "", e.message ? e.message : ""); }
};

class BluezBackend;
struct Call {
  BluezBackend* self = nullptr;
  std::string op, path;
  sd_bus_slot* slot = nullptr;
};

class BluezBackend : public Backend {
 public:
  explicit BluezBackend(sd_bus* bus) : bus_(bus) {
    // Changes: any property of a BlueZ object, and objects coming and going (a device found while scanning).
    sd_bus_match_signal(bus_, &props_slot_, kBluez, nullptr, "org.freedesktop.DBus.Properties", "PropertiesChanged", &BluezBackend::changed, this);
    sd_bus_match_signal(bus_, &added_slot_, kBluez, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", &BluezBackend::changed, this);
    sd_bus_match_signal(bus_, &removed_slot_, kBluez, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", &BluezBackend::changed, this);
  }

  ~BluezBackend() override {
    if (discovery_on_ && !adapter_path_.empty()) {
      Err err;
      sd_bus_call_method(bus_, kBluez, adapter_path_.c_str(), kAdapterIface, "StopDiscovery", &err.e, nullptr, "");
    }
    if (pending_) sd_bus_message_unref(pending_);
    for (Call* c : calls_) {
      sd_bus_slot_unref(c->slot);
      delete c;
    }
    sd_bus_slot_unref(props_slot_);
    sd_bus_slot_unref(added_slot_);
    sd_bus_slot_unref(removed_slot_);
    if (agent_registered_) {
      Err err;
      sd_bus_call_method(bus_, kBluez, "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent", &err.e, nullptr, "o", kAgentPath);
    }
    sd_bus_slot_unref(agent_slot_);
    sd_bus_flush_close_unref(bus_);
  }

  // ------------------------------------------------------------------ reading --
  State read() override {
    State st;
    Err err;
    sd_bus_message* m = nullptr;
    const int r = sd_bus_call_method(bus_, kBluez, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", &err.e, &m, "");
    if (r < 0) {
      st.error = err.text();
      return st;
    }
    if (sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}") >= 0) {
      while (sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}") > 0) {
        const char* path = nullptr;
        sd_bus_message_read(m, "o", &path);
        Adapter a;
        Device d;
        bool is_adapter = false, is_device = false;
        int battery = -1;
        if (path) a.path = d.path = path;
        if (sd_bus_message_enter_container(m, 'a', "{sa{sv}}") >= 0) {
          while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
            const char* iface = nullptr;
            sd_bus_message_read(m, "s", &iface);
            const bool ia = iface && !std::strcmp(iface, kAdapterIface), id = iface && !std::strcmp(iface, kDeviceIface),
                       ib = iface && !std::strcmp(iface, kBatteryIface);
            is_adapter |= ia;
            is_device |= id;
            if (sd_bus_message_enter_container(m, 'a', "{sv}") >= 0) {
              while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
                const char* key = nullptr;
                sd_bus_message_read(m, "s", &key);
                read_property(m, key ? key : "", ia, id, ib, &a, &d, &battery);
                sd_bus_message_exit_container(m);
              }
              sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
          }
          sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
        if (is_adapter && !st.available) {  // the first adapter is the one this program drives
          st.available = true;
          st.adapter = a;
          adapter_path_ = a.path;
        } else if (is_device) {
          d.battery = battery;
          st.devices.push_back(d);
        }
      }
      sd_bus_message_exit_container(m);
    }
    sd_bus_message_unref(m);
    if (st.available) {  // keep only the devices of that adapter, and give each its address
      std::vector<Device> mine;
      for (Device& d : st.devices)
        if (d.path.rfind(st.adapter.path + "/", 0) == 0) mine.push_back(d);
      st.devices = mine;
    } else {
      st.devices.clear();
    }
    sort_devices(&st.devices);
    return st;
  }

  // ------------------------------------------------------------------ actions --
  bool set_bool(const std::string& path, const char* iface, const char* name, bool on, std::string* error) {
    Err err;
    const int r = sd_bus_set_property(bus_, kBluez, path.c_str(), iface, name, &err.e, "b", on ? 1 : 0);
    if (r < 0 && error) *error = err.text();
    return r >= 0;
  }
  bool set_powered(bool on, std::string* error) override { return set_bool(adapter(), kAdapterIface, "Powered", on, error); }
  bool set_discoverable(bool on, std::string* error) override { return set_bool(adapter(), kAdapterIface, "Discoverable", on, error); }
  bool set_trusted(const std::string& device_path, bool on, std::string* error) override {
    return set_bool(device_path, kDeviceIface, "Trusted", on, error);
  }
  bool set_discovery(bool on, std::string* error) override {
    if (on == discovery_on_) return true;
    Err err;
    const int r = sd_bus_call_method(bus_, kBluez, adapter().c_str(), kAdapterIface, on ? "StartDiscovery" : "StopDiscovery", &err.e, nullptr, "");
    if (r < 0) {
      if (error) *error = err.text();
      return false;
    }
    discovery_on_ = on;
    return true;
  }
  bool remove_device(const std::string& device_path, std::string* error) override {
    Err err;
    const int r = sd_bus_call_method(bus_, kBluez, adapter().c_str(), kAdapterIface, "RemoveDevice", &err.e, nullptr, "o", device_path.c_str());
    if (r < 0 && error) *error = err.text();
    return r >= 0;
  }

  void connect_device(const std::string& path) override { call_async("connect", path, "Connect", kConnectTimeoutUs); }
  void disconnect_device(const std::string& path) override { call_async("disconnect", path, "Disconnect", kConnectTimeoutUs); }
  void pair_device(const std::string& path) override {
    std::string error;
    if (!register_agent(&error)) {
      if (on_result) on_result("pair", path, false, error);
      return;
    }
    call_async("pair", path, "Pair", kPairTimeoutUs);
  }

  // ------------------------------------------------------------------- agent --
  const Prompt& prompt() const override { return prompt_; }

  void answer(bool yes) override {
    if (!pending_) return clear_prompt();
    if (yes) {
      if (prompt_.kind == Prompt::Kind::EnterPin || prompt_.kind == Prompt::Kind::EnterPasskey) {
        sd_bus_reply_method_errorf(pending_, "org.bluez.Error.Rejected", "No code entered");
      } else {
        sd_bus_reply_method_return(pending_, "");
      }
    } else {
      sd_bus_reply_method_errorf(pending_, "org.bluez.Error.Rejected", "Rejected by the user");
    }
    clear_prompt();
  }

  void answer_text(const std::string& text) override {
    if (!pending_) return clear_prompt();
    if (prompt_.kind == Prompt::Kind::EnterPin) {
      sd_bus_reply_method_return(pending_, "s", text.c_str());
    } else if (prompt_.kind == Prompt::Kind::EnterPasskey) {
      char* end = nullptr;
      const unsigned long v = std::strtoul(text.c_str(), &end, 10);
      if (text.empty() || *end != '\0' || v > 999999) sd_bus_reply_method_errorf(pending_, "org.bluez.Error.Rejected", "Not a 6 digit passkey");
      else sd_bus_reply_method_return(pending_, "u", static_cast<uint32_t>(v));
    } else {
      sd_bus_reply_method_errorf(pending_, "org.bluez.Error.Rejected", "Unexpected answer");
    }
    clear_prompt();
  }

  int bus_fd() const override { return sd_bus_get_fd(bus_); }
  void process() override {
    int r;
    do {
      r = sd_bus_process(bus_, nullptr);
    } while (r > 0);
  }

 private:
  sd_bus* bus_ = nullptr;
  std::string adapter_path_;
  sd_bus_slot *props_slot_ = nullptr, *added_slot_ = nullptr, *removed_slot_ = nullptr, *agent_slot_ = nullptr;
  bool discovery_on_ = false, agent_registered_ = false;
  std::vector<Call*> calls_;
  Prompt prompt_;
  sd_bus_message* pending_ = nullptr;

  std::string adapter() {
    if (adapter_path_.empty()) read();
    return adapter_path_.empty() ? "/org/bluez/hci0" : adapter_path_;
  }

  static int changed(sd_bus_message*, void* ud, sd_bus_error*) {
    auto* self = static_cast<BluezBackend*>(ud);
    if (self->on_change) self->on_change();
    return 0;
  }

  // One property of the {sv} entry the message is positioned on; skips what is not wanted.
  static void read_property(sd_bus_message* m, const std::string& key, bool in_adapter, bool in_device, bool in_battery, Adapter* a,
                            Device* d, int* battery) {
    char type = 0;
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v' || !contents) {
      sd_bus_message_skip(m, "v");
      return;
    }
    auto read_s = [&](std::string* out) {
      const char* v = nullptr;
      if (sd_bus_message_enter_container(m, 'v', "s") >= 0) {
        if (sd_bus_message_read(m, "s", &v) >= 0 && v) *out = v;
        sd_bus_message_exit_container(m);
        return true;
      }
      return false;
    };
    auto read_b = [&](bool* out) {
      int v = 0;
      if (sd_bus_message_enter_container(m, 'v', "b") >= 0) {
        if (sd_bus_message_read(m, "b", &v) >= 0) *out = v != 0;
        sd_bus_message_exit_container(m);
        return true;
      }
      return false;
    };
    bool done = false;
    if (in_adapter) {
      if (key == "Alias") done = read_s(&a->name);
      else if (key == "Address") done = read_s(&a->address);
      else if (key == "Powered") done = read_b(&a->powered);
      else if (key == "Discovering") done = read_b(&a->discovering);
      else if (key == "Discoverable") done = read_b(&a->discoverable);
    } else if (in_device) {
      if (key == "Alias") done = read_s(&d->name);
      else if (key == "Address") done = read_s(&d->address);
      else if (key == "Icon") done = read_s(&d->icon);
      else if (key == "Paired") done = read_b(&d->paired);
      else if (key == "Connected") done = read_b(&d->connected);
      else if (key == "Trusted") done = read_b(&d->trusted);
      else if (key == "RSSI" && sd_bus_message_enter_container(m, 'v', "n") >= 0) {
        int16_t v = 0;
        if (sd_bus_message_read(m, "n", &v) >= 0) d->rssi = v;
        sd_bus_message_exit_container(m);
        done = true;
      }
    } else if (in_battery && key == "Percentage" && sd_bus_message_enter_container(m, 'v', "y") >= 0) {
      uint8_t v = 0;
      if (sd_bus_message_read(m, "y", &v) >= 0) *battery = v;
      sd_bus_message_exit_container(m);
      done = true;
    }
    if (!done) sd_bus_message_skip(m, "v");
  }

  // ------------------------------------------------------------ async calls --
  static int call_done(sd_bus_message* reply, void* ud, sd_bus_error*) {
    Call* c = static_cast<Call*>(ud);
    BluezBackend* self = c->self;
    const sd_bus_error* e = sd_bus_message_get_error(reply);
    const bool ok = e == nullptr;
    const std::string error = ok ? "" : friendly_error(e->name ? e->name : "", e->message ? e->message : "");
    const std::string op = c->op, path = c->path;
    for (size_t i = 0; i < self->calls_.size(); ++i)
      if (self->calls_[i] == c) {
        self->calls_.erase(self->calls_.begin() + static_cast<long>(i));
        break;
      }
    sd_bus_slot_unref(c->slot);
    delete c;
    if (ok && op == "pair") {  // the usual next steps: remember the device, and connect to it
      std::string ignore;
      self->set_trusted(path, true, &ignore);
      self->connect_device(path);
    }
    if (self->on_result) self->on_result(op, path, ok, error);
    return 0;
  }

  void call_async(const char* op, const std::string& path, const char* method, uint64_t timeout_us) {
    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(bus_, &m, kBluez, path.c_str(), kDeviceIface, method) < 0) {
      if (on_result) on_result(op, path, false, "Could not start the action.");
      return;
    }
    auto* c = new Call{this, op, path, nullptr};
    if (sd_bus_call_async(bus_, &c->slot, m, &BluezBackend::call_done, c, timeout_us) < 0) {
      delete c;
      sd_bus_message_unref(m);
      if (on_result) on_result(op, path, false, "Could not start the action.");
      return;
    }
    calls_.push_back(c);
    sd_bus_message_unref(m);
  }

  // ----------------------------------------------------------------- agent --
  bool register_agent(std::string* error) {
    if (agent_registered_) return true;
    if (!agent_slot_) {
      const int r = sd_bus_add_object(bus_, &agent_slot_, kAgentPath, &BluezBackend::agent_call, this);
      if (r < 0) {
        if (error) *error = "Could not offer the pairing agent on the bus.";
        return false;
      }
    }
    Err err;
    if (sd_bus_call_method(bus_, kBluez, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", &err.e, nullptr, "os", kAgentPath, "KeyboardDisplay") < 0) {
      if (error) *error = err.text();
      return false;
    }
    agent_registered_ = true;
    Err err2;  // a default agent is what receives requests started by the other device; failure here only means someone else has it
    sd_bus_call_method(bus_, kBluez, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", &err2.e, nullptr, "o", kAgentPath);
    return true;
  }

  std::string device_name(const char* path) {
    if (!path) return "";
    Err err;
    char* v = nullptr;
    std::string out;
    if (sd_bus_get_property_string(bus_, kBluez, path, kDeviceIface, "Alias", &err.e, &v) >= 0 && v) out = v;
    free(v);
    return out;
  }

  void set_prompt(Prompt::Kind kind, const char* device, const std::string& code, sd_bus_message* hold) {
    if (pending_) {  // a new question replaces an old one that was never answered
      sd_bus_reply_method_errorf(pending_, "org.bluez.Error.Canceled", "Replaced");
      sd_bus_message_unref(pending_);
      pending_ = nullptr;
    }
    prompt_.kind = kind;
    prompt_.device_path = device ? device : "";
    prompt_.device_name = device_name(device);
    prompt_.code = code;
    if (hold) pending_ = sd_bus_message_ref(hold);
    if (on_prompt) on_prompt();
  }

  void clear_prompt() {
    if (pending_) {
      sd_bus_message_unref(pending_);
      pending_ = nullptr;
    }
    prompt_ = {};
    if (on_prompt) on_prompt();
  }

  static BluezBackend* me(void* ud) { return static_cast<BluezBackend*>(ud); }
  static std::string six(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%06u", v);
    return buf;
  }

  // Every call to the agent object lands here; the interface is org.bluez.Agent1.
  static int agent_call(sd_bus_message* m, void* ud, sd_bus_error* e) {
    struct Entry {
      const char* member;
      int (*fn)(sd_bus_message*, void*, sd_bus_error*);
    };
    static const Entry table[] = {
        {"Release", &BluezBackend::m_release},
        {"RequestPinCode", &BluezBackend::m_request_pin},
        {"DisplayPinCode", &BluezBackend::m_display_pin},
        {"RequestPasskey", &BluezBackend::m_request_passkey},
        {"DisplayPasskey", &BluezBackend::m_display_passkey},
        {"RequestConfirmation", &BluezBackend::m_confirm},
        {"RequestAuthorization", &BluezBackend::m_authorize},
        {"AuthorizeService", &BluezBackend::m_authorize_service},
        {"Cancel", &BluezBackend::m_cancel},
    };
    for (const Entry& en : table)
      if (sd_bus_message_is_method_call(m, "org.bluez.Agent1", en.member)) return en.fn(m, ud, e);
    return 0;  // not ours (introspection, properties): the bus answers with "unknown method"
  }

  static int m_release(sd_bus_message* m, void* ud, sd_bus_error*) {
    me(ud)->agent_registered_ = false;
    return sd_bus_reply_method_return(m, "");
  }
  static int m_request_pin(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char* dev = nullptr;
    sd_bus_message_read(m, "o", &dev);
    me(ud)->set_prompt(Prompt::Kind::EnterPin, dev, "", m);
    return 1;
  }
  static int m_display_pin(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char *dev = nullptr, *pin = nullptr;
    sd_bus_message_read(m, "os", &dev, &pin);
    me(ud)->set_prompt(Prompt::Kind::DisplayCode, dev, pin ? pin : "", nullptr);
    return sd_bus_reply_method_return(m, "");
  }
  static int m_request_passkey(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char* dev = nullptr;
    sd_bus_message_read(m, "o", &dev);
    me(ud)->set_prompt(Prompt::Kind::EnterPasskey, dev, "", m);
    return 1;
  }
  static int m_display_passkey(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char* dev = nullptr;
    uint32_t key = 0;
    uint16_t entered = 0;
    sd_bus_message_read(m, "ouq", &dev, &key, &entered);
    me(ud)->set_prompt(Prompt::Kind::DisplayCode, dev, six(key), nullptr);
    return sd_bus_reply_method_return(m, "");
  }
  static int m_confirm(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char* dev = nullptr;
    uint32_t key = 0;
    sd_bus_message_read(m, "ou", &dev, &key);
    me(ud)->set_prompt(Prompt::Kind::Confirm, dev, six(key), m);
    return 1;
  }
  static int m_authorize(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char* dev = nullptr;
    sd_bus_message_read(m, "o", &dev);
    me(ud)->set_prompt(Prompt::Kind::Authorize, dev, "", m);
    return 1;
  }
  static int m_authorize_service(sd_bus_message* m, void* ud, sd_bus_error*) {
    const char *dev = nullptr, *uuid = nullptr;
    sd_bus_message_read(m, "os", &dev, &uuid);
    me(ud)->set_prompt(Prompt::Kind::Authorize, dev, "", m);
    return 1;
  }
  static int m_cancel(sd_bus_message* m, void* ud, sd_bus_error*) {
    me(ud)->clear_prompt();
    return sd_bus_reply_method_return(m, "");
  }
};

}  // namespace

std::unique_ptr<Backend> Backend::open() {
  sd_bus* bus = nullptr;
  if (sd_bus_open_system(&bus) < 0) return nullptr;
  sd_bus_set_method_call_timeout(bus, 5 * 1000000ULL);
  return std::make_unique<BluezBackend>(bus);
}

}  // namespace fleetwm::bt
