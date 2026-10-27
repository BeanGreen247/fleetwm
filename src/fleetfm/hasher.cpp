#include "hasher.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <strings.h>
#include <vector>

namespace fleetwm::fm {

namespace {
// libcrypto is opened on the first checksum instead of being linked: the desktop, which never hashes anything, otherwise maps it
// (and pays its relocation pages) all session. The header is only used for the types.
struct Crypto {
  const EVP_MD* (*sha1)();
  const EVP_MD* (*md5)();
  const EVP_MD* (*sha256)();
  const EVP_MD* (*sha512)();
  const EVP_MD* (*blake2b512)();
  EVP_MD_CTX* (*ctx_new)();
  void (*ctx_free)(EVP_MD_CTX*);
  int (*init)(EVP_MD_CTX*, const EVP_MD*, ENGINE*);
  int (*update)(EVP_MD_CTX*, const void*, size_t);
  int (*final)(EVP_MD_CTX*, unsigned char*, unsigned*);
};

const Crypto& crypto() {
  static const Crypto c = [] {
    Crypto r{};
    void* h = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!h) std::abort();  // hashing is impossible without it; it was a hard link dependency before
    auto sym = [h](const char* n) { return dlsym(h, n); };
    r.sha1 = reinterpret_cast<decltype(r.sha1)>(sym("EVP_sha1"));
    r.md5 = reinterpret_cast<decltype(r.md5)>(sym("EVP_md5"));
    r.sha256 = reinterpret_cast<decltype(r.sha256)>(sym("EVP_sha256"));
    r.sha512 = reinterpret_cast<decltype(r.sha512)>(sym("EVP_sha512"));
    r.blake2b512 = reinterpret_cast<decltype(r.blake2b512)>(sym("EVP_blake2b512"));
    r.ctx_new = reinterpret_cast<decltype(r.ctx_new)>(sym("EVP_MD_CTX_new"));
    r.ctx_free = reinterpret_cast<decltype(r.ctx_free)>(sym("EVP_MD_CTX_free"));
    r.init = reinterpret_cast<decltype(r.init)>(sym("EVP_DigestInit_ex"));
    r.update = reinterpret_cast<decltype(r.update)>(sym("EVP_DigestUpdate"));
    r.final = reinterpret_cast<decltype(r.final)>(sym("EVP_DigestFinal_ex"));
    return r;
  }();
  return c;
}
const EVP_MD* md_for(HashAlgo a) {
  switch (a) {
    case HashAlgo::Sha1: return crypto().sha1();
    case HashAlgo::Md5: return crypto().md5();
    case HashAlgo::Sha512: return crypto().sha512();
    case HashAlgo::Blake2b: return crypto().blake2b512();
    default: return crypto().sha256();
  }
}

double seconds_now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

const char* hash_name(HashAlgo a) {
  switch (a) {
    case HashAlgo::Sha1: return "sha1";
    case HashAlgo::Md5: return "md5";
    case HashAlgo::Sha512: return "sha512";
    case HashAlgo::Blake2b: return "blake2b";
    case HashAlgo::Auto: return "auto";
    default: return "sha256";
  }
}

const char* hash_label(HashAlgo a) {
  switch (a) {
    case HashAlgo::Sha1: return "SHA-1";
    case HashAlgo::Md5: return "MD5";
    case HashAlgo::Sha512: return "SHA-512";
    case HashAlgo::Blake2b: return "BLAKE2b";
    case HashAlgo::Auto: return "automatic";
    default: return "SHA-256";
  }
}

bool parse_hash_name(const std::string& s, HashAlgo* out) {
  if (!strcasecmp(s.c_str(), "sha256")) *out = HashAlgo::Sha256;
  else if (!strcasecmp(s.c_str(), "sha1")) *out = HashAlgo::Sha1;
  else if (!strcasecmp(s.c_str(), "md5")) *out = HashAlgo::Md5;
  else if (!strcasecmp(s.c_str(), "sha512")) *out = HashAlgo::Sha512;
  else if (!strcasecmp(s.c_str(), "blake2b")) *out = HashAlgo::Blake2b;
  else if (!strcasecmp(s.c_str(), "auto")) *out = HashAlgo::Auto;
  else return false;
  return true;
}

bool cpu_flags_have_sha(const std::string& line) {
  const size_t colon = line.find(':');
  const std::string flags = " " + (colon == std::string::npos ? line : line.substr(colon + 1)) + " ";
  return flags.find(" sha_ni ") != std::string::npos || flags.find(" sha2 ") != std::string::npos;
}

namespace {
bool this_cpu_has_sha() {
  std::ifstream f("/proc/cpuinfo");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind("flags", 0) == 0 || line.rfind("Features", 0) == 0) return cpu_flags_have_sha(line);
  return false;
}
}  // namespace

HashAlgo resolve_algo(HashAlgo a) {
  if (a != HashAlgo::Auto) return a;
  static const HashAlgo chosen = [] {
    if (this_cpu_has_sha()) return HashAlgo::Sha256;  // the benchmark below always ends here on such a CPU; skip its 25 ms (it ran at every file manager start)
    std::vector<char> buf(1 << 20, 'x');
    HashAlgo best = HashAlgo::Sha256;
    double best_t = 1e9;
    for (HashAlgo c : {HashAlgo::Sha256, HashAlgo::Sha512, HashAlgo::Blake2b}) {
      double t = 1e9;
      for (int rep = 0; rep < 3; ++rep) {  // the best of three: one slow run (a context switch) must not decide
        Hasher h(c);
        const double t0 = seconds_now();
        for (int i = 0; i < 2; ++i) h.update(buf.data(), buf.size());  // 18 MiB in all: about 25 ms on a CPU without SHA instructions
        t = std::min(t, seconds_now() - t0);
      }
      // SHA-256 wins ties and anything within 5 %: it is the one people compare checksums of
      if (t < best_t * (c == HashAlgo::Sha256 ? 1.0 : 0.95)) {
        best_t = t;
        best = c;
      }
    }
    return best;
  }();
  return chosen;
}

Hasher::Hasher(HashAlgo algo) : algo_(resolve_algo(algo)), ctx_(crypto().ctx_new()) { crypto().init(ctx_, md_for(algo_), nullptr); }
Hasher::~Hasher() { crypto().ctx_free(ctx_); }
void Hasher::reset() { crypto().init(ctx_, md_for(algo_), nullptr); }
void Hasher::update(const void* data, size_t n) { crypto().update(ctx_, data, n); }

std::string Hasher::finish_hex() {
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned len = 0;
  crypto().final(ctx_, md, &len);
  static const char* hex = "0123456789abcdef";
  std::string out(len * 2, '0');
  for (unsigned i = 0; i < len; ++i) {
    out[i * 2] = hex[md[i] >> 4];
    out[i * 2 + 1] = hex[md[i] & 15];
  }
  return out;
}

std::string hash_file(const std::string& path, HashAlgo algo) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
  Hasher h(algo);
  std::vector<char> buf(1 << 20);
  for (;;) {
    const ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n < 0) {
      if (errno == EINTR) continue;
      const int e = errno;
      ::close(fd);
      errno = e;
      return {};
    }
    if (n == 0) break;
    h.update(buf.data(), static_cast<size_t>(n));
  }
  ::close(fd);
  return h.finish_hex();
}

}  // namespace fleetwm::fm
