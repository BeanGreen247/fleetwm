#pragma once

// Login sessions the greeter can start: Fleetwm itself plus every desktop other
// packages register in wayland-sessions/ and xsessions/ (MATE, KDE, GNOME, ...).

#include <optional>
#include <string>
#include <vector>

namespace fleetwm {

enum class SessionKind { Wayland, X11 };

struct SessionEntry {
  std::string id;             // "wayland:fleetwm", "x11:mate": unique, safe to send over the greeter socket
  std::string name;           // shown on the login card
  std::string exec;           // Exec= line with the %-codes removed
  std::string desktop_names;  // DesktopNames= joined with ':', else the file stem
  SessionKind kind = SessionKind::Wayland;
};

constexpr const char* kFleetwmSessionId = "wayland:fleetwm";

// Parses one .desktop file's text. Nothing when it is hidden, has no Exec, or is not a Application/Session entry.
std::optional<SessionEntry> parse_session_desktop(const std::string& stem, SessionKind kind,
                                                  const std::string& text);

// Fleetwm first, then the others by name. Entries whose TryExec is not an executable file are left out; X11
// entries only when `allow_x11` (Xorg and xinit are installed). `fleetwm_exec` is the compositor path.
std::vector<SessionEntry> discover_sessions(const std::vector<std::string>& wayland_dirs,
                                            const std::vector<std::string>& x11_dirs, bool allow_x11,
                                            const std::string& fleetwm_exec);

// The standard directories, /usr/local/share before /usr/share.
std::vector<std::string> default_wayland_session_dirs();
std::vector<std::string> default_x11_session_dirs();

// True when /usr/bin/Xorg and /usr/bin/xinit exist.
bool x11_sessions_available();

const SessionEntry* find_session(const std::vector<SessionEntry>& list, const std::string& id);

// The command to run as the user. Fleetwm: the compositor itself. Other Wayland: `sh -c "exec <Exec>"`.
// X11: xinit <Xsession wrapper if present> <Exec> -- Xorg :<display> vt<vt> (display and vt from the caller).
std::vector<std::string> session_argv(const SessionEntry& s, const std::string& fleetwm_exec, int x_display,
                                      int vt, bool have_xsession_wrapper);

// The vt number in "tty3" -> 3, 0 when the name is not ttyN.
int vt_from_tty(const std::string& tty_name);

// First :N with no /tmp/.X11-unix/XN socket.
int first_free_x_display();

}  // namespace fleetwm
