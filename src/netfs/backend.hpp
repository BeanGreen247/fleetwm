#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// What fleetwm-netfs needs from a protocol: names, sizes and times of a folder, ranged reads, whole-file uploads and the
// folder/file operations. One implementation per protocol (SFTP, FTP/FTPS, WebDAV, SMB); the file system layer (vfs.hpp)
// adds caching and write buffering on top and is the same for all of them. Paths are absolute below the share root
// ("/" is the root), without percent escapes.

namespace fleetwm::netfs {

struct NetStat {
  bool dir = false;
  uint64_t size = 0;
  int64_t mtime = 0;     // seconds since the epoch, 0 unknown
  uint32_t mode = 0;     // permission bits when the server tells, else 0 (the file system layer picks 0644 / 0755)
};

struct NetEntry {
  std::string name;
  NetStat st;
};

class Backend {
 public:
  virtual ~Backend() = default;
  // Logs in. False with *err set (wrong password, host unreachable, refused host key ...).
  virtual bool connect(std::string* err) = 0;
  virtual bool list(const std::string& path, std::vector<NetEntry>* out, std::string* err) = 0;
  // False and *missing = true when the path does not exist; false with *missing = false for any other failure.
  virtual bool stat(const std::string& path, NetStat* st, bool* missing, std::string* err) = 0;
  // Bytes read (0 at the end of the file) or -1 with *err set.
  virtual int64_t read(const std::string& path, uint64_t offset, void* buf, size_t len, std::string* err) = 0;
  // Replaces the remote file with the contents of the local file `local`.
  virtual bool put(const std::string& path, const std::string& local, std::string* err) = 0;
  virtual bool mkdir(const std::string& path, std::string* err) = 0;
  virtual bool remove_file(const std::string& path, std::string* err) = 0;
  virtual bool remove_dir(const std::string& path, std::string* err) = 0;
  virtual bool rename(const std::string& from, const std::string& to, std::string* err) = 0;
};

struct Target {
  std::string scheme;    // sftp, ftp, ftps, dav, davs
  std::string host;
  int port = 0;          // 0: the protocol's default
  std::string user, password;
  std::string base;      // path on the server that appears as the root of the mount
  bool anonymous = false;
  std::string known_hosts;  // file for the SSH host key fingerprints (trust on first use)
};

// null when the protocol is not built in.
std::unique_ptr<Backend> make_backend(const Target& t);

}  // namespace fleetwm::netfs
