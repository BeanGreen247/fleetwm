#pragma once

// BlueZ over the system D-Bus: read the adapter and devices, switch the adapter on and off, scan, connect, pair, forget.
// Device calls are asynchronous (a connect can take half a minute and pairing needs this same connection to answer BlueZ's
// questions), so the owner polls process() when bus_fd() is readable and gets the outcome through on_result.
// Pairing questions (confirm this number, show this PIN, type the passkey) arrive as a Prompt; answer() replies.

#include <functional>
#include <memory>
#include <string>

#include "bt_types.hpp"

namespace fleetwm::bt {

struct Prompt {
  enum class Kind {
    None,
    Confirm,      // "Does the device show 123456?"  answer(true/false)
    DisplayCode,  // show this PIN or passkey to type on the device; nothing to answer
    EnterPin,     // type the PIN shown by / agreed with the device: answer_text()
    EnterPasskey, // type the 6 digits the device shows: answer_text()
    Authorize,    // "Allow this device to connect?" answer(true/false)
  };
  Kind kind = Kind::None;
  std::string device_path;
  std::string device_name;
  std::string code;  // the passkey / PIN for Confirm and DisplayCode
};

class Backend {
 public:
  virtual ~Backend() = default;
  // Null when the system bus cannot be reached. BlueZ itself may still be absent: read() then says available = false.
  static std::unique_ptr<Backend> open();

  virtual State read() = 0;

  // Synchronous and quick (a property write).
  virtual bool set_powered(bool on, std::string* error) = 0;
  virtual bool set_discoverable(bool on, std::string* error) = 0;
  virtual bool set_trusted(const std::string& device_path, bool on, std::string* error) = 0;
  // Starts or stops scanning (reference counted by BlueZ per client; this client counts itself once).
  virtual bool set_discovery(bool on, std::string* error) = 0;
  virtual bool remove_device(const std::string& device_path, std::string* error) = 0;

  // Asynchronous: the result comes through on_result(op, device_path, ok, error_text).
  virtual void connect_device(const std::string& device_path) = 0;
  virtual void disconnect_device(const std::string& device_path) = 0;
  virtual void pair_device(const std::string& device_path) = 0;  // registers the agent first; trusts the device on success
  std::function<void(const std::string& op, const std::string& path, bool ok, const std::string& error)> on_result;

  // The agent: a question from BlueZ the user has to answer.
  virtual const Prompt& prompt() const = 0;
  virtual void answer(bool yes) = 0;
  virtual void answer_text(const std::string& text) = 0;
  std::function<void()> on_prompt;  // a prompt appeared or went away

  // Event loop glue: poll() this fd for POLLIN and call process() when it is readable.
  virtual int bus_fd() const = 0;
  virtual void process() = 0;
  // BlueZ changed something (a device appeared, a property changed): read() again.
  std::function<void()> on_change;
};

}  // namespace fleetwm::bt
