// Folder-listing benchmark: the same folder read four ways, results as median of N runs.
//   fm       fleetfm: getdents64 into one arena + statx per entry, natural-name sort
//   fm-names fleetfm without statx (kinds only, from d_type): what a lazy listing reads before the first frame
//   fm-lazy  fm-names plus the natural-name sort: what the window now waits for (sizes and dates are read for the visible rows only)
//   stdfs    std::filesystem::directory_iterator + status/size/mtime per entry, sorted by name with the same natural compare
//   posix    opendir/readdir + lstat per entry, sorted with strcasecmp
// build: g++ -O2 -std=c++20 -I src/fleetfm dirbench.cpp build-bench/src/fleetfm/libfleetfm-core.a -lcrypto -o dirbench
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "dir_listing.hpp"
#include "natural_sort.hpp"

using namespace fleetwm::fm;
namespace fs = std::filesystem;

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

template <class F>
static double median_ms(int runs, F f) {
  std::vector<double> t;
  for (int i = 0; i < runs; ++i) {
    const double a = now();
    f();
    t.push_back((now() - a) * 1000);
  }
  std::sort(t.begin(), t.end());
  return t[t.size() / 2];
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  const int runs = argc > 2 ? std::atoi(argv[2]) : 15;
  size_t sink = 0;
  DirListing l;
  list_dir(dir, {}, &l);
  const size_t n = l.entries.size();
  const double fm = median_ms(runs, [&] {
    DirListing x;
    list_dir(dir, {true, true}, &x);
    sort_listing(&x, SortKey::Name);
    sink += x.entries.size();
  });
  const double fm_names = median_ms(runs, [&] {
    DirListing x;
    list_dir(dir, {false, true}, &x);
    sink += x.entries.size();
  });
  const double fm_lazy = median_ms(runs, [&] {
    DirListing x;
    list_dir(dir, {false, true}, &x);
    sort_listing(&x, SortKey::Name);
    sink += x.entries.size();
  });
  const double stdfs = median_ms(runs, [&] {
    struct E {
      std::string name;
      bool dir;
      uintmax_t size;
      long mtime;
    };
    std::vector<E> v;
    for (const auto& e : fs::directory_iterator(dir)) {
      std::error_code ec;
      E x{e.path().filename().string(), e.is_directory(ec), e.is_regular_file(ec) ? e.file_size(ec) : 0, static_cast<long>(e.last_write_time(ec).time_since_epoch().count())};
      v.push_back(std::move(x));
    }
    std::sort(v.begin(), v.end(), [](const E& a, const E& b) {
      if (a.dir != b.dir) return a.dir;
      return natural_compare(a.name, b.name) < 0;
    });
    sink += v.size();
  });
  const double posix = median_ms(runs, [&] {
    struct E {
      std::string name;
      bool dir;
      long size, mtime;
    };
    std::vector<E> v;
    DIR* d = opendir(dir);
    while (dirent* de = readdir(d)) {
      if (de->d_name[0] == '.' && (!de->d_name[1] || (de->d_name[1] == '.' && !de->d_name[2]))) continue;
      struct stat st;
      const std::string p = std::string(dir) + "/" + de->d_name;
      if (lstat(p.c_str(), &st) != 0) continue;
      v.push_back({de->d_name, S_ISDIR(st.st_mode), st.st_size, st.st_mtime});
    }
    closedir(d);
    std::sort(v.begin(), v.end(), [](const E& a, const E& b) {
      if (a.dir != b.dir) return a.dir;
      return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    sink += v.size();
  });
  std::printf("%-9s %8zu entries  median of %d runs (ms):  fm %8.2f   fm-names %8.2f   fm-lazy %8.2f   stdfs %8.2f   posix %8.2f   (stdfs/fm %.2fx, posix/fm %.2fx, stdfs/fm-lazy %.2fx)  [%zu]\n", dir, n, runs, fm,
              fm_names, fm_lazy, stdfs, posix, stdfs / fm, posix / fm, stdfs / fm_lazy, sink % 7);
}
