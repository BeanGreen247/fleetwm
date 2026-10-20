#include "backend.hpp"

namespace fleetwm::netfs {

std::unique_ptr<Backend> make_curl_backend(const Target& t);
std::unique_ptr<Backend> make_sftp_backend(const Target& t);

std::unique_ptr<Backend> make_backend(const Target& t) {
  if (t.scheme == "sftp") return make_sftp_backend(t);
  if (t.scheme == "ftp" || t.scheme == "ftps" || t.scheme == "dav" || t.scheme == "davs") return make_curl_backend(t);
  return nullptr;
}

}  // namespace fleetwm::netfs
