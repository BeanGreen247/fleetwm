#include "locations.hpp"

#include <toml++/toml.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config_paths.hpp"

namespace fs = std::filesystem;

namespace fleetwm::fm {

namespace {
std::string lower(std::string_view s) {
  std::string o(s);
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}
int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

std::string percent_decode(std::string_view s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
      o.push_back(static_cast<char>(hexv(s[i + 1]) * 16 + hexv(s[i + 2])));
      i += 2;
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

std::string percent_encode(std::string_view s, bool keep_slash) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (keep_slash && c == '/')) {
      o.push_back(static_cast<char>(c));
    } else {
      o.push_back('%');
      o.push_back(hex[c >> 4]);
      o.push_back(hex[c & 15]);
    }
  }
  return o;
}

Uri parse_uri(std::string_view s) {
  Uri u;
  const size_t sep = s.find("://");
  if (sep == std::string_view::npos || sep == 0) return u;
  for (size_t i = 0; i < sep; ++i)
    if (!std::isalnum(static_cast<unsigned char>(s[i])) && s[i] != '+' && s[i] != '-' && s[i] != '.') return u;
  u.scheme = lower(s.substr(0, sep));
  std::string_view rest = s.substr(sep + 3);
  const size_t slash = rest.find('/');
  std::string_view auth = rest.substr(0, slash);
  std::string_view tail = slash == std::string_view::npos ? std::string_view() : rest.substr(slash);
  const size_t q = tail.find('?');
  if (q != std::string_view::npos) {
    u.query = std::string(tail.substr(q + 1));
    tail = tail.substr(0, q);
  }
  u.path = percent_decode(tail);
  const size_t at = auth.rfind('@');
  if (at != std::string_view::npos) {
    std::string_view ui = auth.substr(0, at);
    auth = auth.substr(at + 1);
    const size_t colon = ui.find(':');
    u.user = percent_decode(ui.substr(0, colon));
    if (colon != std::string_view::npos) u.password = percent_decode(ui.substr(colon + 1));
  }
  if (!auth.empty() && auth.front() == '[') {  // [::1]:22
    const size_t close = auth.find(']');
    if (close == std::string_view::npos) return u;
    u.host = std::string(auth.substr(1, close - 1));
    auth = auth.substr(close + 1);
    if (!auth.empty() && auth.front() == ':') auth.remove_prefix(1);
    else auth = {};
  } else {
    const size_t colon = auth.rfind(':');
    if (colon != std::string_view::npos) {
      u.host = std::string(auth.substr(0, colon));
      auth = auth.substr(colon + 1);
    } else {
      u.host = std::string(auth);
      auth = {};
    }
  }
  if (!auth.empty()) {
    int p = 0;
    for (char c : auth) {
      if (c < '0' || c > '9') return u;
      p = p * 10 + (c - '0');
      if (p > 65535) return u;
    }
    u.port = p;
  }
  u.valid = true;
  return u;
}

std::string uri_to_string(const Uri& u, bool with_password) {
  std::string o = u.scheme + "://";
  if (!u.user.empty()) {
    o += percent_encode(u.user, false);
    if (with_password && !u.password.empty()) o += ":" + percent_encode(u.password, false);
    o += "@";
  }
  o += u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host;
  if (u.port) o += ":" + std::to_string(u.port);
  o += percent_encode(u.path);
  if (!u.query.empty()) o += "?" + u.query;
  return o;
}

Location parse_location(std::string_view in, const std::string& home, const std::string& cwd) {
  Location l;
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t')) in.remove_prefix(1);
  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\n')) in.remove_suffix(1);
  if (in.empty()) return l;
  const std::string low = lower(in);
  if (low == "computer:///" || low == "computer://" || low == "this pc" || low == "computer:") return l.kind = PlaceKind::Computer, l;
  if (low == "network:///" || low == "network://" || low == "network:") return l.kind = PlaceKind::Network, l;
  if (low == "trash:///" || low == "trash://" || low == "trash:") return l.kind = PlaceKind::Trash, l;
  if (low == "recent:///" || low == "recent://" || low == "recent:") return l.kind = PlaceKind::Recent, l;
  if (in.compare(0, 2, "\\\\") == 0) {  // \\server\share\folder
    std::string p(in.substr(2));
    std::replace(p.begin(), p.end(), '\\', '/');
    l.kind = PlaceKind::Remote;
    l.path = "smb://" + percent_encode(p);
    l.uri = parse_uri(l.path);
    return l;
  }
  if (in.compare(0, 2, "//") == 0 && in.size() > 2 && in[2] != '/') {
    l.kind = PlaceKind::Remote;
    l.path = "smb:" + std::string(in);
    l.uri = parse_uri(l.path);
    return l;
  }
  if (in.compare(0, 7, "file://") == 0) {
    l.kind = PlaceKind::Local;
    l.path = percent_decode(in.substr(7));
    return l;
  }
  if (in.find("://") != std::string_view::npos) {
    Uri u = parse_uri(in);
    if (u.valid && find_protocol(u.scheme)) {
      l.kind = PlaceKind::Remote;
      l.uri = u;
      l.path = std::string(in);
      return l;
    }
    return l;
  }
  std::string p(in);
  if (p == "~" || p.compare(0, 2, "~/") == 0) p = home + p.substr(1);
  else if (p[0] != '/') p = cwd + (cwd.empty() || cwd.back() == '/' ? "" : "/") + p;
  l.kind = PlaceKind::Local;
  l.path = fs::path(p).lexically_normal().string();
  if (l.path.size() > 1 && l.path.back() == '/') l.path.pop_back();
  return l;
}

const std::vector<Protocol>& protocols() {
  static const std::vector<Protocol> list = {
      {"smb", "smb", "Windows share (SMB / CIFS, Samba, NAS)", 445, true, false, "smb://server/share"},
      {"sftp", "sftp", "SSH (SFTP)", 22, true, true, "also ssh:// and fish://"},
      {"ssh", "sftp", "SSH (SFTP)", 22, true, true, "same as sftp://"},
      {"ftp", "ftp", "FTP", 21, true, false, "plain text on the wire"},
      {"ftps", "ftp", "FTP over TLS", 990, true, true, "explicit and implicit TLS"},
      {"davs", "davs", "WebDAV, secure (Nextcloud, ownCloud)", 443, true, true, "Nextcloud: /remote.php/dav/files/<user>/"},
      {"dav", "dav", "WebDAV", 80, true, false, "plain text on the wire"},
      {"webdav", "dav", "WebDAV", 80, true, false, "alias of dav://"},
      {"nfs", "nfs", "NFS (network file system)", 2049, false, false, "nfs://server/export"},
      {"afp", "afp", "Apple Filing Protocol", 548, true, false, "old Macs and Time Capsules"},
      {"mtp", "mtp", "Phone or player (MTP)", 0, false, false, "Android over USB"},
      {"afc", "afc", "iPhone / iPad (AFC)", 0, false, false, "photos over USB"},
      {"gphoto2", "gphoto2", "Camera (PTP)", 0, false, false, "digital cameras"},
      {"http", "http", "Web server (read only)", 80, false, false, "browse an index page"},
      {"https", "http", "Web server, secure (read only)", 443, false, true, ""},
      {"archive", "archive", "Archive (zip, tar, iso)", 0, false, false, "open an archive like a folder"},
      {"admin", "admin", "Administrator access", 0, false, false, "asks for authorisation"},
  };
  return list;
}

const Protocol* find_protocol(std::string_view scheme) {
  const std::string s = lower(scheme);
  for (const Protocol& p : protocols())
    if (s == p.scheme) return &p;
  return nullptr;
}

std::string nextcloud_webdav_uri(std::string_view server, std::string_view user) {
  std::string s(server);
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '/')) s.pop_back();
  std::string scheme = "davs";
  if (lower(s).compare(0, 8, "https://") == 0) s = s.substr(8);
  else if (lower(s).compare(0, 7, "http://") == 0) {
    s = s.substr(7);
    scheme = "dav";
  } else if (lower(s).compare(0, 7, "davs://") == 0) s = s.substr(7);
  else if (lower(s).compare(0, 6, "dav://") == 0) {
    s = s.substr(6);
    scheme = "dav";
  }
  // Someone pasted the full WebDAV address: keep what comes before /remote.php.
  const size_t rp = s.find("/remote.php");
  if (rp != std::string::npos) s.resize(rp);
  if (s.empty() || user.empty()) return {};
  const size_t slash = s.find('/');
  const std::string host = s.substr(0, slash), prefix = slash == std::string::npos ? "" : s.substr(slash);
  return scheme + "://" + percent_encode(user, false) + "@" + host + prefix + "/remote.php/dav/files/" + percent_encode(user, false) + "/";
}

std::string gvfs_mount_dir_name(const Uri& u) {
  const std::string scheme = u.scheme == "ssh" || u.scheme == "fish" ? "sftp" : u.scheme;
  if (scheme == "smb") {
    std::string share = u.path;
    if (!share.empty() && share[0] == '/') share.erase(0, 1);
    const size_t slash = share.find('/');
    if (slash != std::string::npos) share.resize(slash);
    return "smb-share:server=" + u.host + ",share=" + share;
  }
  std::string n = scheme + ":host=" + u.host;
  if (u.port) n += ",port=" + std::to_string(u.port);
  if (scheme == "dav" || scheme == "davs") {
    n = "dav:host=" + u.host;
    if (u.port) n += ",port=" + std::to_string(u.port);
    if (scheme == "davs") n += ",ssl=true";
    if (!u.user.empty()) n += ",user=" + u.user;
    // the path up to and including the user's own root is the mount prefix
    std::string pre = u.path;
    while (pre.size() > 1 && pre.back() == '/') pre.pop_back();
    if (pre.size() > 1) n += ",prefix=" + percent_encode(pre, false);
    return n;
  }
  if (!u.user.empty()) n += ",user=" + u.user;
  return n;
}

std::string gvfs_friendly_name(std::string_view dir) {
  const size_t colon = dir.find(':');
  if (colon == std::string_view::npos) return std::string(dir);
  const std::string kind(dir.substr(0, colon));
  std::string server, share, host, user, prefix;
  bool ssl = false;
  std::string rest(dir.substr(colon + 1));
  size_t i = 0;
  while (i <= rest.size()) {
    size_t comma = rest.find(',', i);
    if (comma == std::string::npos) comma = rest.size();
    const std::string kv = rest.substr(i, comma - i);
    const size_t eq = kv.find('=');
    if (eq != std::string::npos) {
      const std::string k = kv.substr(0, eq), v = percent_decode(kv.substr(eq + 1));
      if (k == "server") server = v;
      else if (k == "share") share = v;
      else if (k == "host") host = v;
      else if (k == "user") user = v;
      else if (k == "prefix") prefix = v;
      else if (k == "ssl") ssl = v == "true";
    }
    i = comma + 1;
  }
  if (kind == "smb-share") return share + " (\\\\" + server + ")";
  if (kind == "smb-server") return server + " (SMB)";
  if (kind == "sftp") return (user.empty() ? "" : user + " on ") + host + " (SFTP)";
  if (kind == "ftp") return (user.empty() ? "" : user + " on ") + host + " (FTP)";
  if (kind == "dav") {
    const bool nc = prefix.find("remote.php") != std::string::npos;
    return (user.empty() ? "" : user + " on ") + host + (nc ? " (Nextcloud)" : (ssl ? " (WebDAV, secure)" : " (WebDAV)"));
  }
  if (kind == "mtp") return host + " (phone)";
  if (kind == "afc") return host + " (iOS)";
  if (kind == "nfs") return host + " (NFS)";
  if (kind == "afp-volume") return host + " (AFP)";
  return std::string(dir);
}

std::string network_mount_label(std::string_view source, std::string_view fstype) {
  std::string s(source);
  if (fstype == "cifs" || fstype == "smb3" || fstype == "smbfs") {
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    const size_t slash = s.find('/');
    if (slash == std::string::npos) return "\\\\" + s;
    std::string share = s.substr(slash + 1);
    std::replace(share.begin(), share.end(), '/', '\\');
    return share + " (\\\\" + s.substr(0, slash) + ")";
  }
  if (fstype == "nfs" || fstype == "nfs4") {
    const size_t colon = s.find(':');
    if (colon == std::string::npos) return s;
    std::string p = s.substr(colon + 1);
    const size_t last = p.rfind('/');
    return (last == std::string::npos || last + 1 == p.size() ? p : p.substr(last + 1)) + " (" + s.substr(0, colon) + ")";
  }
  if (fstype == "fuse.sshfs") {
    const size_t colon = s.find(':');
    if (colon == std::string::npos) return s;
    std::string host = s.substr(0, colon);
    const size_t at = host.find('@');
    if (at != std::string::npos) host = host.substr(at + 1);
    std::string p = s.substr(colon + 1);
    return (p.empty() ? "~" : p) + " on " + host;
  }
  return s;
}

std::string gvfs_root(unsigned uid) { return "/run/user/" + std::to_string(uid) + "/gvfs"; }

MountOutcome mount_location(const Uri& u, const Credentials& c, CommandRunner& run, const std::string& gvfs_dir) {
  MountOutcome r;
  const Protocol* pr = find_protocol(u.scheme);
  if (!pr || !u.valid) {
    r.error = "Unknown address";
    return r;
  }
  const std::string dir = gvfs_dir + "/" + gvfs_mount_dir_name(u);
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    r.ok = true;
    r.path = dir;
    return r;
  }
  Uri with_user = u;
  if (!c.user.empty()) with_user.user = c.user;
  std::vector<std::string> argv = {"gio", "mount"};
  if (c.anonymous) argv.push_back("--anonymous");
  argv.push_back(uri_to_string(with_user));
  // gio asks on stdin when it is not a terminal: user, domain, password in the order it needs them (user is already in
  // the address for sftp / ftp / dav, so only the password is asked).
  std::string input;
  if (!c.anonymous) {
    if (u.scheme == "smb") input = (c.user.empty() ? "\n" : c.user + "\n") + (c.domain.empty() ? "WORKGROUP" : c.domain) + "\n" + c.password + "\n";
    else if (!c.password.empty()) input = c.password + "\n";
  }
  const RunResult rr = run.run(argv, input, 60000);
  if (rr.status != 0 && !fs::exists(dir, ec)) {
    r.error = rr.output;
    while (!r.error.empty() && (r.error.back() == '\n' || r.error.back() == ' ')) r.error.pop_back();
    if (r.error.empty()) r.error = rr.timed_out ? "The server did not answer in time" : "The connection failed";
    return r;
  }
  r.ok = true;
  r.path = dir;
  return r;
}

bool unmount_location(const std::string& mountpoint, CommandRunner& run, std::string* error) {
  const RunResult r = run.run({"gio", "mount", "-u", mountpoint});
  if (r.status != 0 && error) *error = r.output;
  return r.status == 0;
}

std::string places_path() { return (config_internal::config_home() / "fleetwm" / "fleetfm-places.toml").string(); }

std::vector<SavedPlace> load_places() {
  std::vector<SavedPlace> out;
  std::error_code ec;
  if (!fs::exists(places_path(), ec)) return out;
  try {
    toml::table root = toml::parse_file(places_path());
    if (auto* arr = root["place"].as_array())
      for (auto& el : *arr)
        if (auto* t = el.as_table()) {
          SavedPlace p;
          if (auto v = (*t)["name"].value<std::string>()) p.name = *v;
          if (auto v = (*t)["uri"].value<std::string>()) p.uri = *v;
          if (!p.uri.empty()) {
            Uri u = parse_uri(p.uri);
            u.password.clear();  // never keep a password that slipped in
            p.uri = uri_to_string(u);
            out.push_back(std::move(p));
          }
        }
  } catch (const toml::parse_error&) {
    out.clear();
  }
  return out;
}

void save_places(const std::vector<SavedPlace>& places) {
  const fs::path path = places_path();
  fs::create_directories(path.parent_path());
  toml::array arr;
  for (const SavedPlace& p : places) {
    Uri u = parse_uri(p.uri);
    toml::table t;
    t.insert_or_assign("name", p.name);
    t.insert_or_assign("uri", u.valid ? uri_to_string(u) : p.uri);
    arr.push_back(std::move(t));
  }
  toml::table root;
  root.insert_or_assign("place", std::move(arr));
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
    out << root;
    if (!out) throw std::runtime_error("cannot write " + tmp.string());
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) throw std::runtime_error("cannot save " + path.string() + ": " + ec.message());
}

}  // namespace fleetwm::fm
