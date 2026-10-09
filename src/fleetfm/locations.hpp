#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "process.hpp"

// Where a tab can point: a local path, a special place (This PC, Network, Trash, Recent) or a server (smb, sftp, ftp,
// webdav / Nextcloud, nfs ...). Servers are mounted through GVfs, the same layer Nautilus, Nemo, Caja and Thunar use, so
// the mounted share shows up as an ordinary folder below /run/user/<uid>/gvfs and every list, copy and verify works on it
// unchanged. This file is the pure part (parsing, naming, URL building) plus the mount call through an injected runner.

namespace fleetwm::fm {

struct Uri {
  std::string scheme, user, password, host, path, query;
  int port = 0;
  bool valid = false;
};
// "sftp://me@host:2222/home/me" -> parts. Percent escapes in user, password and path are decoded.
Uri parse_uri(std::string_view s);
// Back to text; the password is left out unless asked for (it never goes to a settings file or a title bar).
std::string uri_to_string(const Uri& u, bool with_password = false);
std::string percent_decode(std::string_view s);
std::string percent_encode(std::string_view s, bool keep_slash = true);

enum class PlaceKind { Local, Computer, Network, Trash, Recent, Remote, Unknown };
struct Location {
  PlaceKind kind = PlaceKind::Unknown;
  std::string path;  // Local: absolute path; Remote: the uri text
  Uri uri;           // Remote only
};
// What the address bar understood: "/home/me", "~/x", "smb://nas/media", "\\nas\media", "//nas/media", "computer:///",
// "network:///", "trash:///", "recent:///". Relative paths resolve against `cwd`; "~" against `home`.
Location parse_location(std::string_view input, const std::string& home, const std::string& cwd);

struct Protocol {
  const char* scheme;       // as typed
  const char* gvfs;         // GVfs scheme it is mounted with
  const char* name;         // for the Connect dialog
  int port;
  bool login;               // asks for a user name / password
  bool encrypted;
  const char* note;
};
// The protocols the other Linux file managers offer (GVfs backends found on this system: smb, sftp, ftp, dav, nfs, afp,
// mtp, afc, gphoto2, google, onedrive, archive, cdda, admin, http), in the order the Connect dialog lists them.
const std::vector<Protocol>& protocols();
const Protocol* find_protocol(std::string_view scheme);

// Nextcloud / ownCloud: "cloud.example.com", "https://cloud.example.com/nextcloud/" and a user -> the WebDAV address
// ("davs://user@cloud.example.com/nextcloud/remote.php/dav/files/user/"). http:// gives dav://.
std::string nextcloud_webdav_uri(std::string_view server, std::string_view user);

// The folder name GVfs's FUSE layer uses for a mounted uri ("smb-share:server=nas,share=media"), and the other way:
// a readable name in Windows 10's style ("media (\\nas)").
std::string gvfs_mount_dir_name(const Uri& u);
std::string gvfs_friendly_name(std::string_view dir_name);
// For kernel mounts: //nas/media (cifs) -> "media (\\nas)", nas:/srv/x (nfs) -> "x (nas)", me@host:/p (sshfs) -> "p on host".
std::string network_mount_label(std::string_view source, std::string_view fstype);
std::string gvfs_root(unsigned uid);  // /run/user/<uid>/gvfs

struct Credentials {
  std::string user, password, domain;
  bool anonymous = false;
};
struct MountOutcome {
  bool ok = false;
  std::string path;   // the local folder showing the mounted place
  std::string error;
};
// `gio mount <uri>`; credentials are answered on its prompts. A place that is already mounted is a success.
MountOutcome mount_location(const Uri& u, const Credentials& c, CommandRunner& run, const std::string& gvfs_dir);
bool unmount_location(const std::string& mountpoint, CommandRunner& run, std::string* error);

// Saved network places (Windows 10's "Network locations" and Nautilus's "Other Locations"), without passwords.
struct SavedPlace {
  std::string name, uri;
  bool operator==(const SavedPlace&) const = default;
};
std::string places_path();
std::vector<SavedPlace> load_places();
void save_places(const std::vector<SavedPlace>& places);

}  // namespace fleetwm::fm
