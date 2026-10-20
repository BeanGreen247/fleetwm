#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "backend.hpp"

// The file system behind the FUSE mount, without FUSE in it so the tests can drive it with a fake server. It adds what makes a
// slow network usable for a file manager that stats every name it lists: stat results come from the folder listing that was just
// read, misses are remembered for a moment, reads fetch a megabyte at a time, and writes go to a temporary local file that is
// uploaded in one piece when the program closes it (flush), so a failed upload is reported to the program that wrote the file.
// Every method returns 0 or a negative errno, the way FUSE wants it.

namespace fleetwm::netfs {

struct VfsOptions {
  int stat_ttl_s = 5;            // how long a listing or stat answer is trusted
  int miss_ttl_s = 2;            // how long "does not exist" is trusted
  size_t read_block = 1 << 20;   // bytes fetched per server request
  std::string temp_dir;          // where write buffers live ("" = $XDG_RUNTIME_DIR, then /tmp)
  std::function<int64_t()> clock;  // seconds, for tests
};

class Vfs {
 public:
  Vfs(std::unique_ptr<Backend> backend, VfsOptions opt = {});
  ~Vfs();

  int getattr(const std::string& path, NetStat* st);
  int readdir(const std::string& path, std::vector<NetEntry>* out);
  // Bytes read, or a negative errno.
  int64_t read(const std::string& path, uint64_t off, void* buf, size_t len);

  // Write handles. `truncate` starts from an empty file; otherwise the current contents are fetched first.
  int open_write(const std::string& path, bool truncate, bool create, uint64_t* handle);
  int64_t write(uint64_t handle, uint64_t off, const void* data, size_t len);
  int64_t read_handle(uint64_t handle, uint64_t off, void* buf, size_t len);  // a file opened read-write reads its own buffer
  int truncate_handle(uint64_t handle, uint64_t size);
  int flush(uint64_t handle);     // uploads when something changed
  void release(uint64_t handle);  // flush was already asked for; drops the buffer
  // truncate(2) on a path with no open handle: fetches, cuts and uploads.
  int truncate_path(const std::string& path, uint64_t size);

  int mkdir(const std::string& path);
  int unlink(const std::string& path);
  int rmdir(const std::string& path);
  int rename(const std::string& from, const std::string& to);

  const std::string& last_error() const { return last_error_; }
  uint64_t stat_calls() const { return stat_calls_; }  // calls that reached the server, for the tests
  uint64_t list_calls() const { return list_calls_; }

 private:
  struct Cached {
    NetStat st;
    bool missing = false;
    int64_t expires = 0;
  };
  struct Listing {
    std::vector<NetEntry> entries;
    int64_t expires = 0;
  };
  struct WriteBuf {
    std::string path;
    int fd = -1;
    bool dirty = false;
  };
  int64_t now() const;
  static std::string parent_of(const std::string& p);
  static std::string name_of(const std::string& p);
  void forget(const std::string& path);  // the entry and the listing of its folder
  int fetch_to(const std::string& path, uint64_t size, int fd);
  int upload(WriteBuf& w);
  int fail(const std::string& what, const std::string& err, int code = 5 /*EIO*/);

  std::unique_ptr<Backend> be_;
  VfsOptions opt_;
  std::map<std::string, Cached> stats_;
  std::map<std::string, Listing> listings_;
  struct Block {
    std::string path;
    uint64_t off = 0;
    std::string data;
    bool at_end = false;
  } block_;
  std::map<uint64_t, WriteBuf> writes_;
  uint64_t next_handle_ = 1;
  std::string last_error_;
  uint64_t stat_calls_ = 0, list_calls_ = 0;
};

}  // namespace fleetwm::netfs
