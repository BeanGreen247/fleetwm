#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "hasher.hpp"
#include "speed_meter.hpp"

// Copy and move with an integrity check. A file is written under a temporary name, optionally flushed to the device
// (fsync), read back from the device (page cache dropped, O_DIRECT when the file system allows it) and compared by checksum
// with what was read from the source; only then is it renamed into place and, for a move, the source removed. A crash or
// a bad stick leaves a ".fleetfm-part" file, never a half-written file under the real name.

namespace fleetwm::fm {

enum class Conflict { Replace, Skip, KeepBoth, Cancel };
enum class SyncMode { Never, Auto, Always };  // Auto: fsync when verifying
enum class Phase { Idle, Discovering, Copying, Verifying, Finishing, Done };

struct ConflictInfo {
  std::string source, destination;
  uint64_t source_size = 0, dest_size = 0;
  int64_t source_mtime = 0, dest_mtime = 0;
  bool dest_is_dir = false;
};

struct TransferOptions {
  bool move = false;
  bool verify = true;
  HashAlgo algo = HashAlgo::Sha256;
  SyncMode sync = SyncMode::Auto;
  size_t block_bytes = 1 << 20;
  // Verification cost control. A file at least this big is flushed, read back and renamed on its own as soon as it is written; smaller
  // files are written to temporary names, flushed together (one syncfs for the whole batch instead of one fsync each, which is what
  // made a verified copy of 20,000 small files take 9 minutes on a virtual disk), read back and renamed once the batch is this big.
  uint64_t big_file_bytes = 64ull << 20;
  uint64_t batch_bytes = 256ull << 20;
  uint32_t batch_files = 4096;
  // Asked once per clash unless the answer is "apply to all" (the caller keeps that state). Default: KeepBoth.
  std::function<Conflict(const ConflictInfo&)> on_conflict;
  // Read the file back from the device with O_DIRECT where possible; false only drops the page cache.
  bool direct_verify = true;
  bool follow_symlinks = false;  // copy the target instead of the link
};

struct Progress {
  Phase phase = Phase::Idle;
  uint64_t bytes_done = 0, bytes_total = 0;     // copied
  uint64_t verified = 0, verify_total = 0;       // bytes read back, and how many will be (0 when verification is off)
  uint64_t files_done = 0, files_total = 0;
  std::string current;                           // file being worked on
  double speed = 0;                              // bytes/s over the whole job (copy and verify counted)
  double eta = -1;                               // seconds, -1 unknown
  bool paused = false;
  double fraction() const;                       // 0..1 across copy and verify
};

struct TransferError {
  std::string path, message;
};
struct VerifiedFile {
  std::string source, destination, checksum;
  bool matched = false;
};

struct TransferResult {
  bool cancelled = false;
  std::vector<TransferError> errors;
  std::vector<VerifiedFile> verified;  // every file that was checked, with its checksum
  uint64_t mismatches = 0;
  uint64_t files_copied = 0, bytes_copied = 0;
  std::vector<std::pair<std::string, std::string>> top_level;  // (source, where it went) for each item placed directly in the destination: what an undo needs
  bool ok() const { return !cancelled && errors.empty() && mismatches == 0; }
};

// "name (2).ext" until the name is free in `dir`.
std::string unique_name(const std::string& dir, const std::string& name);
// What Explorer calls a copy made next to its original: "name - Copy.ext", then "name - Copy (2).ext" ...
std::string copy_name(const std::string& dir, const std::string& name);

class Transfer {
 public:
  Transfer(std::vector<std::string> sources, std::string dest_dir, TransferOptions opt);
  ~Transfer();
  // Blocking; run it on a worker thread. Safe to call progress()/cancel()/pause() from another one.
  TransferResult run();
  Progress progress() const;
  void cancel();
  void pause(bool on);

 private:
  struct Item {
    std::string src, dst;
    uint8_t type;  // 0 file, 1 dir, 2 symlink
    uint64_t size;
    uint32_t mode;
    int64_t mtime;
    dev_t dev;
  };
  bool discover();
  bool add_tree(const std::string& src, const std::string& dst, std::vector<Item>* plan);
  bool copy_file(const Item& it, std::string* final_dst);
  // Reads `path` back and hashes it. `buf` is an aligned block_bytes buffer owned by the calling thread.
  bool verify_file(const std::string& path, uint64_t size, std::string* hex, char* buf);
  struct Pending {
    std::string tmp, final_dst, src;
    uint64_t size = 0;
    std::string want;  // checksum of the source, taken while it was read
  };
  // Checks one finished temporary file against the source checksum and gives it its real name; false when cancelled.
  bool finish_file(const Pending& p);
  // The two halves of finish_file: reading the copy back (safe to run on several threads at once) and giving it its name.
  enum class Check { Match, Mismatch, Unreadable, Cancelled };
  Check read_back(const Pending& p, std::string* got, char* buf);
  bool settle(const Pending& p, Check c);
  bool flush_batch();  // syncfs, then finish_file for every pending file
  bool name_is_pending(const std::string& final_dst) const;
  bool wait_if_paused();
  void note(uint64_t copied_delta, uint64_t verified_delta);
  void fail(const std::string& path, const std::string& msg);
  bool resolve_conflict(const Item& it, std::string* dst, bool* skip);

  std::vector<std::string> sources_;
  std::string dest_dir_;
  TransferOptions opt_;
  std::vector<Item> plan_;
  std::vector<Pending> pending_;
  uint64_t pending_bytes_ = 0;
  void* io_buf_ = nullptr;  // aligned, block_bytes: reused for every file (a fresh 1 MiB allocation per small file cost more than the copy)
  TransferResult result_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  Progress prog_;
  SpeedMeter meter_;
  std::atomic<bool> cancel_{false};
  bool paused_ = false;
  double t0_ = 0;
};

}  // namespace fleetwm::fm
