#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "backend.hpp"
#include "parsers.hpp"

// FTP, FTPS (explicit TLS on port 21, implicit when the port is 990) and WebDAV over HTTP(S), all through libcurl. One easy handle is
// kept so the FTP control connection and the HTTP keep-alive connection are reused between calls.

namespace fleetwm::netfs {

namespace {

struct CurlGlobal {
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

size_t append_cb(char* p, size_t sz, size_t n, void* ud) {
  static_cast<std::string*>(ud)->append(p, sz * n);
  return sz * n;
}

// Collects up to `cap` bytes starting at the requested offset; stops the transfer (write error) once the buffer is full.
struct Sink {
  char* buf = nullptr;
  size_t cap = 0, got = 0;
  uint64_t skip = 0;  // bytes to drop first (a server that ignored the Range header)
  bool full = false;
};
size_t sink_cb(char* p, size_t sz, size_t n, void* ud) {
  Sink* s = static_cast<Sink*>(ud);
  size_t len = sz * n;
  const size_t total = len;
  if (s->skip) {
    const size_t d = static_cast<size_t>(std::min<uint64_t>(s->skip, len));
    s->skip -= d;
    p += d;
    len -= d;
  }
  const size_t take = std::min(len, s->cap - s->got);
  std::memcpy(s->buf + s->got, p, take);
  s->got += take;
  if (take < len || s->got == s->cap) {
    s->full = true;
    return 0;  // stops the transfer on purpose; reported as CURLE_WRITE_ERROR
  }
  return total;
}

class CurlBackend : public Backend {
 public:
  explicit CurlBackend(const Target& t) : t_(t) {
    dav_ = t.scheme == "dav" || t.scheme == "davs";
    tls_ = t.scheme == "davs" || t.scheme == "ftps";
    port_ = t.port;
    if (!port_) port_ = dav_ ? (tls_ ? 443 : 80) : 21;
    base_ = t.base;
    while (base_.size() > 1 && base_.back() == '/') base_.pop_back();
    if (base_ == "/") base_.clear();
    h_ = curl_easy_init();
  }
  ~CurlBackend() override {
    if (h_) curl_easy_cleanup(h_);
  }

  bool connect(std::string* err) override {
    if (!h_) {
      *err = "libcurl could not start";
      return false;
    }
    NetStat st;
    bool missing = false;
    if (dav_) {
      if (!stat("/", &st, &missing, err)) {
        if (missing) *err = "The folder does not exist on the server";
        return false;
      }
      return true;
    }
    std::vector<NetEntry> v;
    return list("/", &v, err);
  }

  bool list(const std::string& path, std::vector<NetEntry>* out, std::string* err) override {
    out->clear();
    if (dav_) {
      std::string body;
      long code = 0;
      if (!propfind(path, 1, &body, &code, err)) return false;
      if (code == 404) {
        *err = "The folder does not exist";
        return false;
      }
      if (code != 207 && code != 200) {
        *err = http_text(code);
        return false;
      }
      *out = parse_propfind(body, base_ + (path == "/" ? "" : path));
      return true;
    }
    std::string body;
    if (!mlsd_failed_) {
      prepare(path, true);
      curl_easy_setopt(h_, CURLOPT_CUSTOMREQUEST, "MLSD");
      set_sink(&body);
      if (perform(nullptr) && !body.empty()) {
        *out = parse_mlsd(body);
        return true;
      }
      if (body.empty() && last_ok_) return true;  // an empty folder
      mlsd_failed_ = true;
      body.clear();
    }
    prepare(path, true);
    set_sink(&body);
    if (!perform(err)) return false;
    *out = parse_unix_list(body, std::time(nullptr));
    return true;
  }

  bool stat(const std::string& path, NetStat* st, bool* missing, std::string* err) override {
    *missing = false;
    if (dav_) {
      std::string body;
      long code = 0;
      if (!propfind(path, 0, &body, &code, err)) return false;
      if (code == 404) {
        *missing = true;
        *err = "Not found";
        return false;
      }
      if (code != 207 && code != 200) {
        *err = http_text(code);
        return false;
      }
      if (!parse_propfind_one(body, st)) {
        *err = "Unreadable answer from the server";
        return false;
      }
      if (path == "/") st->dir = true;
      return true;
    }
    if (path == "/") {
      *st = NetStat{};
      st->dir = true;
      return true;
    }
    const size_t slash = path.rfind('/');
    const std::string parent = slash == 0 ? "/" : path.substr(0, slash);
    const std::string name = path.substr(slash + 1);
    std::vector<NetEntry> v;
    if (!list(parent, &v, err)) {
      if (err->find("550") != std::string::npos || err->find("not found") != std::string::npos) *missing = true;
      return false;
    }
    for (const NetEntry& e : v)
      if (e.name == name) {
        *st = e.st;
        return true;
      }
    *missing = true;
    *err = "Not found";
    return false;
  }

  int64_t read(const std::string& path, uint64_t off, void* buf, size_t len, std::string* err) override {
    if (len == 0) return 0;
    Sink s;
    s.buf = static_cast<char*>(buf);
    s.cap = len;
    prepare(path, false);
    char range[64];
    if (dav_) {
      std::snprintf(range, sizeof range, "%llu-%llu", static_cast<unsigned long long>(off), static_cast<unsigned long long>(off + len - 1));
      curl_easy_setopt(h_, CURLOPT_RANGE, range);
    } else if (off > 0) {
      // REST only: a range with an end makes libcurl fail with "bytes missing" when the file ends sooner (the last block of every file)
      curl_easy_setopt(h_, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(off));
    }
    curl_easy_setopt(h_, CURLOPT_WRITEFUNCTION, sink_cb);
    curl_easy_setopt(h_, CURLOPT_WRITEDATA, &s);
    const CURLcode rc = curl_easy_perform(h_);
    long code = 0;
    curl_easy_getinfo(h_, CURLINFO_RESPONSE_CODE, &code);
    if (dav_ && code == 416) return 0;  // past the end
    if (rc == CURLE_OK || (rc == CURLE_WRITE_ERROR && s.full) || (rc == CURLE_PARTIAL_FILE && s.got > 0)) return static_cast<int64_t>(s.got);
    if (!dav_ && rc == CURLE_FTP_COULDNT_USE_REST) return 0;
    *err = describe(rc, code);
    return -1;
  }

  bool put(const std::string& path, const std::string& local, std::string* err) override {
    FILE* f = std::fopen(local.c_str(), "rb");
    if (!f) {
      *err = "Cannot read the buffered file";
      return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    prepare(path, false);
    curl_easy_setopt(h_, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(h_, CURLOPT_READDATA, f);
    curl_easy_setopt(h_, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size));
    std::string sink;
    set_sink(&sink);
    const bool ok = perform(err);
    std::fclose(f);
    return ok && (!dav_ || (last_code_ >= 200 && last_code_ < 300) || (*err = http_text(last_code_), false));
  }

  bool mkdir(const std::string& path, std::string* err) override {
    if (dav_) return dav_request("MKCOL", path, "", err);
    return ftp_quote({"MKD " + cmd_path(path)}, err);
  }
  bool remove_file(const std::string& path, std::string* err) override {
    if (dav_) return dav_request("DELETE", path, "", err);
    return ftp_quote({"DELE " + cmd_path(path)}, err);
  }
  bool remove_dir(const std::string& path, std::string* err) override {
    if (dav_) return dav_request("DELETE", path, "", err);
    return ftp_quote({"RMD " + cmd_path(path)}, err);
  }
  bool rename(const std::string& from, const std::string& to, std::string* err) override {
    if (dav_) return dav_request("MOVE", from, to, err);
    return ftp_quote({"RNFR " + cmd_path(from), "RNTO " + cmd_path(to)}, err);
  }

 private:
  std::string abs(const std::string& path) const { return base_ + (path == "/" ? "" : path); }
  // The path as an FTP command wants it: the URL's path is relative to the login directory, so is this.
  std::string cmd_path(const std::string& path) const {
    std::string p = abs(path);
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    return p.empty() ? "." : p;
  }

  std::string url_for(const std::string& path, bool dir) const {
    std::string u = dav_ ? (tls_ ? "https://" : "http://") : ((tls_ && port_ == 990) ? "ftps://" : "ftp://");
    u += t_.host.find(':') != std::string::npos ? "[" + t_.host + "]" : t_.host;
    u += ":" + std::to_string(port_);
    std::string p = abs(path);
    if (p.empty()) p = "/";
    u += url_encode_path(p);
    if (dir && u.back() != '/') u += '/';
    return u;
  }

  void prepare(const std::string& path, bool dir) {
    curl_easy_reset(h_);
    errbuf_[0] = 0;
    curl_easy_setopt(h_, CURLOPT_URL, url_for(path, dir).c_str());
    curl_easy_setopt(h_, CURLOPT_ERRORBUFFER, errbuf_);
    curl_easy_setopt(h_, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h_, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(h_, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(h_, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h_, CURLOPT_USERAGENT, "fleetwm-netfs");
    const std::string user = t_.anonymous || t_.user.empty() ? (dav_ ? "" : "anonymous") : t_.user;
    const std::string pass = t_.anonymous ? "anonymous@" : t_.password;
    if (!user.empty()) {
      curl_easy_setopt(h_, CURLOPT_USERNAME, user.c_str());
      curl_easy_setopt(h_, CURLOPT_PASSWORD, pass.c_str());
    }
    if (dav_) {
      curl_easy_setopt(h_, CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_ANY));
      curl_easy_setopt(h_, CURLOPT_FOLLOWLOCATION, 1L);
    } else {
      if (tls_ && port_ != 990) curl_easy_setopt(h_, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
      curl_easy_setopt(h_, CURLOPT_FTP_FILEMETHOD, static_cast<long>(CURLFTPMETHOD_NOCWD));
    }
  }

  void set_sink(std::string* s) {
    curl_easy_setopt(h_, CURLOPT_WRITEFUNCTION, append_cb);
    curl_easy_setopt(h_, CURLOPT_WRITEDATA, s);
  }

  bool perform(std::string* err) {
    const CURLcode rc = curl_easy_perform(h_);
    curl_easy_getinfo(h_, CURLINFO_RESPONSE_CODE, &last_code_);
    last_ok_ = rc == CURLE_OK;
    if (rc == CURLE_OK) return true;
    if (err) *err = describe(rc, last_code_);
    return false;
  }

  std::string describe(CURLcode rc, long code) const {
    if (rc == CURLE_LOGIN_DENIED || code == 401 || code == 530) return "Wrong user name or password";
    if (rc == CURLE_COULDNT_RESOLVE_HOST) return "The server name could not be found";
    if (rc == CURLE_COULDNT_CONNECT) return "The server refused the connection";
    if (rc == CURLE_OPERATION_TIMEDOUT) return "The server did not answer in time";
    if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE) return "The server's certificate is not trusted";
    std::string m = errbuf_[0] ? errbuf_ : curl_easy_strerror(rc);
    if (code) m += " (" + std::to_string(code) + ")";
    return m;
  }

  static std::string http_text(long code) {
    if (code == 401 || code == 403) return "Wrong user name or password, or no permission";
    if (code == 404) return "Not found";
    if (code == 405) return "The server does not allow this (WebDAV may be off)";
    if (code == 409) return "The parent folder does not exist";
    if (code == 507) return "The server is out of space";
    return "The server answered " + std::to_string(code);
  }

  bool propfind(const std::string& path, int depth, std::string* body, long* code, std::string* err) {
    prepare(path, depth == 1 || path == "/");
    static const char* kBody =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/>"
        "<d:getlastmodified/></d:prop></d:propfind>";
    curl_easy_setopt(h_, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, depth ? "Depth: 1" : "Depth: 0");
    hdr = curl_slist_append(hdr, "Content-Type: application/xml");
    curl_easy_setopt(h_, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(h_, CURLOPT_POSTFIELDS, kBody);
    set_sink(body);
    const bool ok = perform(err);
    curl_slist_free_all(hdr);
    *code = last_code_;
    return ok || (last_code_ == 404 || last_code_ == 405);
  }

  bool dav_request(const char* verb, const std::string& path, const std::string& dest, std::string* err) {
    prepare(path, false);
    curl_easy_setopt(h_, CURLOPT_CUSTOMREQUEST, verb);
    curl_slist* hdr = nullptr;
    if (!dest.empty()) {
      hdr = curl_slist_append(hdr, ("Destination: " + url_for(dest, false)).c_str());
      hdr = curl_slist_append(hdr, "Overwrite: T");
      curl_easy_setopt(h_, CURLOPT_HTTPHEADER, hdr);
    }
    std::string sink;
    set_sink(&sink);
    const bool ok = perform(err);
    if (hdr) curl_slist_free_all(hdr);
    if (!ok) return false;
    if (last_code_ >= 200 && last_code_ < 300) return true;
    *err = http_text(last_code_);
    return false;
  }

  bool ftp_quote(const std::vector<std::string>& cmds, std::string* err) {
    prepare("/", true);
    curl_slist* q = nullptr;
    for (const std::string& c : cmds) q = curl_slist_append(q, c.c_str());
    curl_easy_setopt(h_, CURLOPT_QUOTE, q);
    curl_easy_setopt(h_, CURLOPT_NOBODY, 1L);
    const bool ok = perform(err);
    curl_slist_free_all(q);
    return ok;
  }

  Target t_;
  bool dav_ = false, tls_ = false;
  int port_ = 0;
  std::string base_;
  CURL* h_ = nullptr;
  char errbuf_[CURL_ERROR_SIZE] = {};
  long last_code_ = 0;
  bool last_ok_ = false;
  bool mlsd_failed_ = false;
};

}  // namespace

std::unique_ptr<Backend> make_curl_backend(const Target& t) {
  static CurlGlobal g;
  (void)g;
  return std::make_unique<CurlBackend>(t);
}

}  // namespace fleetwm::netfs
