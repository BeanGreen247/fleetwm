#include "backend.hpp"

namespace fleetwm::netfs {

std::unique_ptr<Backend> make_curl_backend(const Target& t);
std::unique_ptr<Backend> make_sftp_backend(const Target& t);
#ifdef NETFS_HAVE_SMB
std::unique_ptr<Backend> make_smb_backend(const Target& t);
#endif

std::unique_ptr<Backend> make_backend(const Target& t) {
  if (t.scheme == "sftp") return make_sftp_backend(t);
  if (t.scheme == "ftp" || t.scheme == "ftps" || t.scheme == "dav" || t.scheme == "davs") return make_curl_backend(t);
#ifdef NETFS_HAVE_SMB
  if (t.scheme == "smb") return make_smb_backend(t);
#endif
  return nullptr;
}

}  // namespace fleetwm::netfs
