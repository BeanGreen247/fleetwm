#include "tuxthrottle.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "child_signals.hpp"
#include "config_paths.hpp"

namespace fleetwm {

namespace {

// A just-big-enough JSON reader: objects, arrays, strings, numbers, booleans, null.
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::map<std::string, Json> o;
  const Json* get(const std::string& k) const {
    if (type != Obj) return nullptr;
    auto it = o.find(k);
    return it == o.end() ? nullptr : &it->second;
  }
};

struct Reader {
  const std::string& t;
  size_t i = 0;
  bool bad = false;
  void ws() { while (i < t.size() && std::strchr(" \t\r\n", t[i])) ++i; }
  bool lit(const char* w) {
    const size_t n = std::strlen(w);
    if (t.compare(i, n, w) != 0) return false;
    i += n;
    return true;
  }
  std::string str() {
    std::string out;
    ++i;  // opening quote
    while (i < t.size() && t[i] != '"') {
      if (t[i] == '\\' && i + 1 < t.size()) {
        ++i;
        switch (t[i]) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'u': i += 4; out += '?'; break;
          default: out += t[i];
        }
      } else {
        out += t[i];
      }
      ++i;
    }
    if (i >= t.size()) bad = true;
    ++i;
    return out;
  }
  Json value(int depth = 0) {
    Json j;
    ws();
    if (i >= t.size() || depth > 32) { bad = true; return j; }
    const char c = t[i];
    if (c == '{') {
      j.type = Json::Obj;
      ++i;
      ws();
      if (i < t.size() && t[i] == '}') { ++i; return j; }
      while (!bad) {
        ws();
        if (i >= t.size() || t[i] != '"') { bad = true; break; }
        std::string k = str();
        ws();
        if (i >= t.size() || t[i] != ':') { bad = true; break; }
        ++i;
        j.o[k] = value(depth + 1);
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == '}') { ++i; break; }
        bad = true;
      }
    } else if (c == '[') {
      j.type = Json::Arr;
      ++i;
      ws();
      if (i < t.size() && t[i] == ']') { ++i; return j; }
      while (!bad) {
        j.a.push_back(value(depth + 1));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == ']') { ++i; break; }
        bad = true;
      }
    } else if (c == '"') {
      j.type = Json::Str;
      j.s = str();
    } else if (lit("true")) {
      j.type = Json::Bool;
      j.b = true;
    } else if (lit("false")) {
      j.type = Json::Bool;
    } else if (lit("null")) {
    } else {
      char* end = nullptr;
      j.n = std::strtod(t.c_str() + i, &end);
      if (end == t.c_str() + i) { bad = true; return j; }
      j.type = Json::Num;
      i = static_cast<size_t>(end - t.c_str());
    }
    return j;
  }
};

double num(const Json* j, double fallback = -1) { return j && j->type == Json::Num ? j->n : fallback; }
bool flag(const Json* j) { return j && j->type == Json::Bool && j->b; }
std::string text(const Json* j) { return j && j->type == Json::Str ? j->s : ""; }

bool on_path(const char* prog) {
  const char* path = std::getenv("PATH");
  std::string p = path ? path : "/usr/local/bin:/usr/bin:/bin";
  size_t pos = 0;
  while (pos <= p.size()) {
    size_t end = p.find(':', pos);
    if (end == std::string::npos) end = p.size();
    const std::string cand = p.substr(pos, end - pos) + "/" + prog;
    if (access(cand.c_str(), X_OK) == 0) return true;
    pos = end + 1;
  }
  return false;
}

}  // namespace

TuxThrottleStatus parse_tuxthrottle_status(const std::string& json) {
  TuxThrottleStatus st;
  Reader r{json};
  const Json root = r.value();
  if (r.bad || root.type != Json::Obj) return st;
  st.model = text(root.get("model"));
  st.platform_profile = text(root.get("platform_profile"));
  if (const Json* cpu = root.get("cpu")) {
    st.cpu_temp_c = num(cpu->get("temp_c"));
    st.cpu_power_w = num(cpu->get("power_w"));
  }
  if (const Json* dg = root.get("dgpu"); dg && dg->type == Json::Obj) {
    st.dgpu_present = true;
    st.dgpu_awake = flag(dg->get("awake"));
    st.dgpu_temp_c = num(dg->get("temp_c"));
    st.dgpu_power_w = num(dg->get("power_w"));
  }
  st.game_mode = flag(root.get("game_mode"));
  if (const Json* gm = root.get("gamemode")) {
    st.gamemoded_installed = flag(gm->get("installed"));
    st.gamemoded_clients = static_cast<int>(num(gm->get("clients"), 0));
  }
  st.ok = root.get("platform_profile") != nullptr || root.get("cpu") != nullptr;
  return st;
}

bool tuxthrottle_installed() { return on_path("tuxthrottlectl"); }

TuxThrottleStatus read_tuxthrottle_status() {
  if (!tuxthrottle_installed()) return {};
  std::unique_ptr<FILE, int (*)(FILE*)> p(popen("tuxthrottlectl status --json 2>/dev/null", "r"), pclose);
  if (!p) return {};
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p.get())) > 0 && out.size() < (1u << 20)) out.append(buf, n);
  return parse_tuxthrottle_status(out);
}

void tuxthrottle_game_mode(const std::string& action) {
  if ((action != "on" && action != "off" && action != "toggle") || !tuxthrottle_installed()) return;
  const pid_t pid = fork();
  if (pid != 0) return;  // the parent (or a failed fork) carries on; the child is reaped by the process's SIGCHLD handling
  reset_signals_for_exec();
  const int null = open("/dev/null", 0);
  if (null >= 0) {
    dup2(null, 1);
    dup2(null, 2);
  }
  execlp("tuxthrottlectl", "tuxthrottlectl", "gamemode", action.c_str(), static_cast<char*>(nullptr));
  _exit(1);
}

bool tuxthrottle_bridged_through_gamemode() {
  const std::string user_ini = (config_internal::config_home() / "gamemode.ini").string();
  for (const std::string& path : {user_ini, std::string("/etc/gamemode.ini")}) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("start", 0) == 0 && (line.find("gaming-performance") != std::string::npos || line.find("tuxthrottle") != std::string::npos)) return true;
    }
  }
  return false;
}

}  // namespace fleetwm

namespace fleetwm {
std::string everyday_power_profile(const std::string& observed, const std::string& remembered, const std::string& configured) {
  auto valid = [](const std::string& p) { return p == "balanced" || p == "power-saver" || p == "performance"; };
  if (valid(observed) && observed != "performance") return observed;
  if (valid(remembered) && remembered != "performance") return remembered;
  if (valid(configured) && configured != "performance") return configured;
  if (observed == "performance" || remembered == "performance" || configured == "performance") return "performance";  // only ever seen performance: nothing else to go back to
  return "balanced";
}
}  // namespace fleetwm
