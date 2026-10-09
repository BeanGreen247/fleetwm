// Copy benchmark for the fleetfm transfer engine (no window): one big file and many small files, with and without the verification
// pass, to compare against `cp` and `cp` followed by sha256sum. Run from run-copybench.sh, which makes the data and times the tools.
//   fleetwm-fm-copybench SRC DST_DIR verify(0|1) direct(0|1) [sync_mode 0|1|2]
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "transfer.hpp"

using namespace fleetwm::fm;

int main(int argc, char** argv) {
  if (argc < 5) return 2;
  TransferOptions o;
  o.verify = std::atoi(argv[3]) != 0;
  o.direct_verify = std::atoi(argv[4]) != 0;
  o.sync = argc > 5 ? static_cast<SyncMode>(std::atoi(argv[5])) : SyncMode::Auto;
  const auto t0 = std::chrono::steady_clock::now();
  Transfer t({argv[1]}, argv[2], o);
  // a poller notes when each phase started, to see where the time goes
  std::atomic<bool> done{false};
  std::string trace;
  std::thread poller([&] {
    Phase last = Phase::Idle;
    while (!done) {
      const Phase p = t.progress().phase;
      if (p != last && trace.size() < 160) {  // the first few transitions are enough to see where the time goes
        char buf[64];
        std::snprintf(buf, sizeof buf, " %d@%.2fs", static_cast<int>(p), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        trace += buf;
      }
      last = p;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  const TransferResult r = t.run();
  done = true;
  poller.join();
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("%.3f s  %.1f MB/s  files %llu  verified %zu  mismatches %llu  errors %zu  phases(1 discover 2 copy 3 verify 4 flush 5 done):%s\n", s, static_cast<double>(r.bytes_copied) / 1e6 / s,
              static_cast<unsigned long long>(r.files_copied), r.verified.size(), static_cast<unsigned long long>(r.mismatches), r.errors.size(), trace.c_str());
  return r.ok() ? 0 : 1;
}
