#include "tls.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <filesystem>

namespace fleetremote {

SSL_CTX* make_tls_context(const std::string& dir) {
  namespace fs = std::filesystem;
  const std::string cert_path = dir + "/cert.pem", key_path = dir + "/key.pem";
  std::error_code ec;
  fs::create_directories(dir, ec);
  fs::permissions(dir, fs::perms::owner_all, ec);
  if (!fs::exists(cert_path, ec) || !fs::exists(key_path, ec)) {
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    X509* x = X509_new();
    if (!pkey || !x) return nullptr;
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), static_cast<long>(time(nullptr)));
    X509_gmtime_adj(X509_getm_notBefore(x), -86400);
    X509_gmtime_adj(X509_getm_notAfter(x), 60L * 60 * 24 * 365 * 10);
    X509_set_pubkey(x, pkey);
    char host[256] = "fleetwm";
    gethostname(host, sizeof host - 1);
    X509_NAME* name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char*>(host), -1, -1, 0);
    X509_set_issuer_name(x, name);
    X509_sign(x, pkey, EVP_sha256());
    FILE* kf = std::fopen(key_path.c_str(), "w");
    if (kf) {
      fchmod(fileno(kf), 0600);
      PEM_write_PrivateKey(kf, pkey, nullptr, nullptr, 0, nullptr, nullptr);
      std::fclose(kf);
    }
    FILE* cf = std::fopen(cert_path.c_str(), "w");
    if (cf) {
      PEM_write_X509(cf, x);
      std::fclose(cf);
    }
    X509_free(x);
    EVP_PKEY_free(pkey);
  }
  SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
  if (!ctx) return nullptr;
  if (SSL_CTX_use_certificate_chain_file(ctx, cert_path.c_str()) != 1 || SSL_CTX_use_PrivateKey_file(ctx, key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
    std::fprintf(stderr, "fleetwm-remote: cannot load the TLS certificate from %s\n", dir.c_str());
    SSL_CTX_free(ctx);
    return nullptr;
  }
  return ctx;
}

}  // namespace fleetremote
