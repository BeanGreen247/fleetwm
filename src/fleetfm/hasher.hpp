#pragma once

#include <openssl/evp.h>

#include <cstddef>
#include <string>

// Streaming checksum for the transfer verification and the Properties -> Checksums page. libcrypto uses the CPU's SHA
// instructions where it has them, which is what lets a verify pass keep up with an SSD.

namespace fleetwm::fm {

// Auto is only a request: resolve_algo() turns it into the fastest strong one on this machine (SHA-256 on CPUs with SHA instructions,
// SHA-512 or BLAKE2b on CPUs without them, where they run 1.5 to 2 times faster).
enum class HashAlgo { Sha256, Sha1, Md5, Sha512, Blake2b, Auto };
const char* hash_name(HashAlgo a);
// "sha256" / "sha512" / "blake2b" / "sha1" / "md5" / "auto" (any case); false when unknown.
bool parse_hash_name(const std::string& s, HashAlgo* out);

// A concrete algorithm for `a`: Auto is measured once per process (about 25 ms) and remembered.
HashAlgo resolve_algo(HashAlgo a);
const char* hash_label(HashAlgo a);  // "SHA-256", "BLAKE2b" ...

class Hasher {
 public:
  explicit Hasher(HashAlgo algo = HashAlgo::Sha256);
  ~Hasher();
  Hasher(const Hasher&) = delete;
  Hasher& operator=(const Hasher&) = delete;
  void update(const void* data, size_t n);
  // Lower-case hex; the hasher is spent afterwards (reset() to reuse).
  std::string finish_hex();
  void reset();

 private:
  HashAlgo algo_;
  EVP_MD_CTX* ctx_ = nullptr;
};

// Checksum of a whole file, read in 1 MiB blocks. Empty string on read error (errno set).
std::string hash_file(const std::string& path, HashAlgo algo = HashAlgo::Sha256);

}  // namespace fleetwm::fm
