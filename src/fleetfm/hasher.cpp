#include "hasher.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <strings.h>
#include <vector>

namespace fleetwm::fm {

namespace {
const EVP_MD* md_for(HashAlgo a) {
  switch (a) {
    case HashAlgo::Sha1: return EVP_sha1();
    case HashAlgo::Md5: return EVP_md5();
    case HashAlgo::Sha512: return EVP_sha512();
    case HashAlgo::Blake2b: return EVP_blake2b512();
    default: return EVP_sha256();
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

HashAlgo resolve_algo(HashAlgo a) {
  if (a != HashAlgo::Auto) return a;
  static const HashAlgo chosen = [] {
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

Hasher::Hasher(HashAlgo algo) : algo_(resolve_algo(algo)), ctx_(EVP_MD_CTX_new()) { EVP_DigestInit_ex(ctx_, md_for(algo_), nullptr); }
Hasher::~Hasher() { EVP_MD_CTX_free(ctx_); }
void Hasher::reset() { EVP_DigestInit_ex(ctx_, md_for(algo_), nullptr); }
void Hasher::update(const void* data, size_t n) { EVP_DigestUpdate(ctx_, data, n); }

std::string Hasher::finish_hex() {
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned len = 0;
  EVP_DigestFinal_ex(ctx_, md, &len);
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
