// The power menu and its passwordless permissions: what each entry runs, that the polkit rule shipped
// for it covers exactly the actions logind checks, that the installer really puts every rule in place,
// and that the pictograms are drawn as one tidy set. No display or system access needed; the rule and
// the installer are read as text from the source tree (FLEETWM_SOURCE_DIR).
#include <gtest/gtest.h>
#include <unistd.h>

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "power_actions.hpp"
#include "power_icons.hpp"

namespace {

namespace pm_fs = std::filesystem;

std::string pm_read(const pm_fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

pm_fs::path pm_root() { return pm_fs::path(FLEETWM_SOURCE_DIR); }

// Removes // and /* */ comments and the contents of string literals, so structure checks see only code.
std::string pm_code_only(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    if (text.compare(i, 2, "//") == 0) {
      while (i < text.size() && text[i] != '\n') ++i;
    } else if (text.compare(i, 2, "/*") == 0) {
      const size_t e = text.find("*/", i + 2);
      i = e == std::string::npos ? text.size() : e + 2;
    } else if (text[i] == '"') {
      out += '"';
      for (++i; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\') ++i;
      }
      out += '"';
      ++i;
    } else {
      out += text[i++];
    }
  }
  return out;
}

// Only the comments removed: string literals stay, so action ids can be checked without the prose around them.
std::string pm_no_comments(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    if (text.compare(i, 2, "//") == 0) {
      while (i < text.size() && text[i] != '\n') ++i;
    } else if (text.compare(i, 2, "/*") == 0) {
      const size_t e = text.find("*/", i + 2);
      i = e == std::string::npos ? text.size() : e + 2;
    } else if (text[i] == '"') {
      const size_t start = i++;
      while (i < text.size() && text[i] != '"') i += text[i] == '\\' ? 2 : 1;
      out += text.substr(start, i - start + 1);
      ++i;
    } else {
      out += text[i++];
    }
  }
  return out;
}

const char* const pm_polkit_rule_file = "packaging/50-fleetwm-power.rules";

}  // namespace

// ---- what each button runs ------------------------------------------------------------------------

TEST(PowerActions, SleepRebootAndShutdownGoThroughSystemctl) {
  using namespace fleetwm::power;
  EXPECT_EQ(command_for(kSleep), (std::vector<std::string>{"systemctl", "suspend"}));
  EXPECT_EQ(command_for(kReboot), (std::vector<std::string>{"systemctl", "reboot"}));
  EXPECT_EQ(command_for(kShutdown), (std::vector<std::string>{"systemctl", "poweroff"}));
}

TEST(PowerActions, LogoutNeedsASessionAndEndsThatSession) {
  using namespace fleetwm::power;
  EXPECT_EQ(command_for(kLogout, "c1"), (std::vector<std::string>{"loginctl", "terminate-session", "c1"}));
  EXPECT_TRUE(command_for(kLogout, "").empty()) << "without a session id there is nothing safe to run";
}

TEST(PowerActions, LockRunsNoCommandItGoesThroughTheCompositor) {
  EXPECT_TRUE(fleetwm::power::command_for(fleetwm::power::kLock, "c1").empty());
}

TEST(PowerActions, NoActionAsksForAPasswordOrForcesTheSystem) {
  using namespace fleetwm::power;
  const std::set<std::string> password_tools = {"sudo", "su", "pkexec", "doas", "gksu", "gksudo"};
  for (int a = 0; a < kCount; ++a) {
    const auto cmd = command_for(a, "c1");
    if (cmd.empty()) continue;
    EXPECT_EQ(password_tools.count(cmd[0]), 0u) << label(a) << " must rely on polkit, not on a password tool";
    for (const std::string& arg : cmd) {
      EXPECT_NE(arg, "-f") << label(a);
      EXPECT_NE(arg, "--force") << label(a);
      EXPECT_NE(arg, "-i") << label(a) << " would skip the inhibitors the keep-awake padlock sets";
      EXPECT_NE(arg, "--ignore-inhibitors") << label(a);
    }
  }
}

TEST(PowerActions, EveryEntryHasALabelAndOrderIsStable) {
  using namespace fleetwm::power;
  EXPECT_STREQ(label(kLock), "Lock");
  EXPECT_STREQ(label(kLogout), "Log out");
  EXPECT_STREQ(label(kSleep), "Sleep");
  EXPECT_STREQ(label(kReboot), "Reboot");
  EXPECT_STREQ(label(kShutdown), "Shut down");
  EXPECT_STREQ(label(kCount), "");
  EXPECT_STREQ(label(-1), "");
}

TEST(PowerActions, PolkitActionsAreTheLogindOnesWithAndWithoutOtherUsersLoggedIn) {
  using namespace fleetwm::power;
  EXPECT_EQ(polkit_actions_for(kShutdown), (std::vector<std::string>{"org.freedesktop.login1.power-off",
                                                                     "org.freedesktop.login1.power-off-multiple-sessions"}));
  EXPECT_EQ(polkit_actions_for(kReboot), (std::vector<std::string>{"org.freedesktop.login1.reboot",
                                                                  "org.freedesktop.login1.reboot-multiple-sessions"}));
  EXPECT_EQ(polkit_actions_for(kSleep), (std::vector<std::string>{"org.freedesktop.login1.suspend",
                                                                 "org.freedesktop.login1.suspend-multiple-sessions"}));
  EXPECT_TRUE(polkit_actions_for(kLock).empty());
  EXPECT_TRUE(polkit_actions_for(kLogout).empty());
}

// ---- the polkit rule shipped with the installer ----------------------------------------------------

TEST(PowerPolkitRule, FileIsShipped) {
  const std::string rule = pm_read(pm_root() / pm_polkit_rule_file);
  ASSERT_FALSE(rule.empty()) << pm_polkit_rule_file << " is missing or empty";
  EXPECT_NE(rule.find("polkit.addRule(function(action, subject)"), std::string::npos);
}

TEST(PowerPolkitRule, CoversEveryActionTheMenuNeeds) {
  const std::string raw = pm_no_comments(pm_read(pm_root() / pm_polkit_rule_file));
  for (int a = 0; a < fleetwm::power::kCount; ++a)
    for (const std::string& id : fleetwm::power::polkit_actions_for(a))
      EXPECT_NE(raw.find("action.id == \"" + id + "\""), std::string::npos)
          << fleetwm::power::label(a) << ": the rule does not allow " << id;
}

TEST(PowerPolkitRule, OnlyForSomeoneAtTheKeyboard) {
  const std::string code = pm_code_only(pm_read(pm_root() / pm_polkit_rule_file));
  EXPECT_NE(code.find("subject.local"), std::string::npos) << "a remote login must not be allowed to power off";
  EXPECT_NE(code.find("subject.active"), std::string::npos) << "a background session must not be allowed either";
}

TEST(PowerPolkitRule, DoesNotOverrideInhibitorsOrGrantEverything) {
  const std::string raw = pm_no_comments(pm_read(pm_root() / pm_polkit_rule_file));
  const std::string code = pm_code_only(raw);
  EXPECT_EQ(raw.find("ignore-inhibit"), std::string::npos) << "a program that asked to keep the system awake must still count";
  EXPECT_EQ(code.find("startsWith"), std::string::npos) << "no prefix matching: list the actions";
  EXPECT_EQ(code.find("indexOf"), std::string::npos);
  EXPECT_EQ(raw.find("org.freedesktop.login1.halt"), std::string::npos);
  // Every action the rule names is one of the menu's.
  std::set<std::string> allowed;
  for (int a = 0; a < fleetwm::power::kCount; ++a)
    for (const std::string& id : fleetwm::power::polkit_actions_for(a)) allowed.insert(id);
  size_t pos = 0;
  int named = 0;
  while ((pos = raw.find("action.id == \"", pos)) != std::string::npos) {
    pos += 14;
    const std::string id = raw.substr(pos, raw.find('"', pos) - pos);
    EXPECT_EQ(allowed.count(id), 1u) << id << " is allowed by the rule but not used by the power menu";
    ++named;
  }
  EXPECT_EQ(named, static_cast<int>(allowed.size()));
}

TEST(PowerPolkitRule, SaysYesAndIsWellFormed) {
  const std::string code = pm_code_only(pm_read(pm_root() / pm_polkit_rule_file));
  EXPECT_NE(code.find("return polkit.Result.YES;"), std::string::npos);
  EXPECT_EQ(code.find("AUTH_"), std::string::npos);
  EXPECT_EQ(code.find("NOT_AUTHORIZED"), std::string::npos);
  int braces = 0, parens = 0;
  for (char c : code) {
    braces += (c == '{') - (c == '}');
    parens += (c == '(') - (c == ')');
    EXPECT_GE(braces, 0);
    EXPECT_GE(parens, 0);
  }
  EXPECT_EQ(braces, 0);
  EXPECT_EQ(parens, 0);
}

// ---- the installer puts the rules where polkit reads them -------------------------------------------

TEST(PowerInstaller, InstallsPolkitItself) {
  const std::string install = pm_read(pm_root() / "install.sh");
  ASSERT_FALSE(install.empty());
  const size_t pk = install.find("apt_install polkitd");
  EXPECT_NE(pk, std::string::npos) << "without polkitd, systemctl suspend/reboot/poweroff are refused";
}

TEST(PowerInstaller, InstallsEveryRuleFileIntoPolkitsRulesDirectory) {
  const std::string install = pm_read(pm_root() / "install.sh");
  int rules = 0;
  for (const auto& entry : pm_fs::directory_iterator(pm_root() / "packaging")) {
    const std::string name = entry.path().filename().string();
    if (entry.path().extension() != ".rules") continue;
    ++rules;
    EXPECT_NE(install.find("packaging/" + name), std::string::npos) << name << " is shipped but install.sh never installs it";
    EXPECT_NE(install.find("/etc/polkit-1/rules.d/" + name), std::string::npos) << name << " does not end up in /etc/polkit-1/rules.d";
    EXPECT_TRUE(name.size() > 3 && std::isdigit(static_cast<unsigned char>(name[0])) && std::isdigit(static_cast<unsigned char>(name[1])))
        << name << " needs a two-digit prefix: polkit reads rule files in name order";
  }
  EXPECT_GE(rules, 3) << "time, locale and power rules";
}

TEST(PowerInstaller, RuleInstallsAreWorldReadableAndAfterPolkit) {
  const std::string install = pm_read(pm_root() / "install.sh");
  const size_t pk = install.find("apt_install polkitd");
  std::istringstream lines(install);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find("/etc/polkit-1/rules.d/") == std::string::npos || line.find("install ") == std::string::npos) continue;
    EXPECT_NE(line.find("-m 644"), std::string::npos) << "polkitd runs as its own user and must be able to read: " << line;
    EXPECT_GT(install.find(line), pk) << "the rule is installed before polkit itself: " << line;
  }
  EXPECT_NE(install.find("50-fleetwm-power.rules"), std::string::npos);
}

TEST(PowerInstaller, RunsTheUnityClashCheckBeforeTheBuild) {
  const std::string install = pm_read(pm_root() / "install.sh");
  const size_t check = install.find("scripts/check-unity-collisions.py");
  const size_t build = install.find("scripts/build-pgo-auto.sh\"");
  ASSERT_NE(check, std::string::npos) << "install.sh does not run the quick unity clash check";
  ASSERT_NE(build, std::string::npos);
  EXPECT_LT(check, build) << "the check has to run before the slow build, not after it";
  EXPECT_TRUE(pm_fs::exists(pm_root() / "scripts/check-unity-collisions.py"));
  const auto perms = pm_fs::status(pm_root() / "scripts/check-unity-collisions.py").permissions();
  EXPECT_NE(perms & pm_fs::perms::owner_exec, pm_fs::perms::none);
  EXPECT_NE(install.find("exit 1", check), std::string::npos) << "a clash must stop the install";
}

TEST(PowerInstaller, TheDocsNameThePowerRule) {
  const std::string readme = pm_read(pm_root() / "README.md");
  EXPECT_NE(readme.find("polkit"), std::string::npos);
}

// ---- the pictograms ---------------------------------------------------------------------------------

namespace {

struct PmInk {
  int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
  long pixels = 0;
  bool empty() const { return x1 < 0; }
  double w() const { return x1 - x0 + 1; }
  double h() const { return y1 - y0 + 1; }
  double cx() const { return (x0 + x1 + 1) / 2.0; }
  double cy() const { return (y0 + y1 + 1) / 2.0; }
};

PmInk pm_render_icon(int action, int size = 64) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(s);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  fleetwm::power::draw_icon(cr, action, size / 2.0, size / 2.0);
  cairo_destroy(cr);
  cairo_surface_flush(s);
  const unsigned char* d = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s);
  PmInk ink;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      if (d[y * stride + x * 4 + 3] > 48) {
        ink.x0 = std::min(ink.x0, x);
        ink.y0 = std::min(ink.y0, y);
        ink.x1 = std::max(ink.x1, x);
        ink.y1 = std::max(ink.y1, y);
        ++ink.pixels;
      }
  cairo_surface_destroy(s);
  return ink;
}

}  // namespace

TEST(PowerIcons, EveryIconDrawsSomething) {
  for (int a = 0; a < fleetwm::power::kCount; ++a)
    EXPECT_FALSE(pm_render_icon(a).empty()) << fleetwm::power::label(a);
}

TEST(PowerIcons, EveryIconFitsItsCell) {
  for (int a = 0; a < fleetwm::power::kCount; ++a) {
    const PmInk ink = pm_render_icon(a);
    EXPECT_LE(ink.w(), fleetwm::power::kIconCell) << fleetwm::power::label(a) << " is wider than its cell";
    EXPECT_LE(ink.h(), fleetwm::power::kIconCell) << fleetwm::power::label(a) << " is taller than its cell";
  }
}

TEST(PowerIcons, EveryIconIsCentredOnItsPoint) {
  for (int a = 0; a < fleetwm::power::kCount; ++a) {
    const PmInk ink = pm_render_icon(a);
    EXPECT_NEAR(ink.cx(), 32.0, 0.9) << fleetwm::power::label(a) << " sits off to the side";
    EXPECT_NEAR(ink.cy(), 32.0, 0.9) << fleetwm::power::label(a) << " sits too high or too low";
  }
}

TEST(PowerIcons, TheSetHasOneSizeAndOneWeight) {
  std::vector<double> extents;
  std::vector<double> weights;
  for (int a = 0; a < fleetwm::power::kCount; ++a) {
    const PmInk ink = pm_render_icon(a);
    extents.push_back(std::max(ink.w(), ink.h()));
    weights.push_back(static_cast<double>(ink.pixels) / (ink.w() * ink.h()));
  }
  const auto [lo, hi] = std::minmax_element(extents.begin(), extents.end());
  EXPECT_GE(*lo, 17.0) << "an icon is too small next to the others";
  EXPECT_LE(*hi, 21.0);
  EXPECT_LE(*hi - *lo, 2.5) << "icons differ too much in size";
  const auto [wl, wh] = std::minmax_element(weights.begin(), weights.end());
  EXPECT_LE(*wh / *wl, 1.5) << "one icon is much heavier than another (a filled shape among outlines?)";
}

TEST(PowerIcons, DrawingIsRepeatable) {
  for (int a = 0; a < fleetwm::power::kCount; ++a) {
    const PmInk one = pm_render_icon(a), two = pm_render_icon(a);
    EXPECT_EQ(one.pixels, two.pixels) << fleetwm::power::label(a);
  }
}

// ---- every shipped polkit rule: the same safety properties ------------------------------------------

TEST(PolkitRules, EveryRuleIsLocalActiveOnlyAndNeverAsksForAnythingElse) {
  int seen = 0;
  for (const auto& entry : pm_fs::directory_iterator(pm_root() / "packaging")) {
    if (entry.path().extension() != ".rules") continue;
    ++seen;
    const std::string name = entry.path().filename().string();
    const std::string code = pm_code_only(pm_read(entry.path()));
    EXPECT_NE(code.find("polkit.addRule(function(action, subject)"), std::string::npos) << name;
    EXPECT_NE(code.find("subject.local"), std::string::npos) << name << " must not apply to remote logins";
    EXPECT_NE(code.find("subject.active"), std::string::npos) << name << " must not apply to background sessions";
    EXPECT_NE(code.find("polkit.Result.YES"), std::string::npos) << name;
    EXPECT_EQ(code.find("polkit.Result.AUTH"), std::string::npos) << name;
    EXPECT_EQ(code.find("polkit.Result.NOT_AUTHORIZED"), std::string::npos) << name;
    EXPECT_EQ(code.find("startsWith"), std::string::npos) << name << ": list exact actions, never prefixes";
    int braces = 0, parens = 0;
    for (char c : code) {
      braces += (c == '{') - (c == '}');
      parens += (c == '(') - (c == ')');
    }
    EXPECT_EQ(braces, 0) << name;
    EXPECT_EQ(parens, 0) << name;
  }
  EXPECT_GE(seen, 3);
}

TEST(PolkitRules, TimeRuleOnlyTouchesTheTimeZoneAndNtpAndNeedsAnAdminGroup) {
  const std::string rule = pm_no_comments(pm_read(pm_root() / "packaging/50-fleetwm-time.rules"));
  EXPECT_NE(rule.find("org.freedesktop.timedate1.set-timezone"), std::string::npos);
  EXPECT_NE(rule.find("org.freedesktop.timedate1.set-ntp"), std::string::npos);
  EXPECT_EQ(rule.find("set-time\""), std::string::npos) << "setting the clock by hand is not part of the Date & Time tab";
  EXPECT_NE(rule.find("isInGroup(\"sudo\")"), std::string::npos);
  EXPECT_NE(rule.find("isInGroup(\"wheel\")"), std::string::npos);
  EXPECT_NE(rule.find("isInGroup(\"adm\")"), std::string::npos);
}

TEST(PolkitRules, LocaleRuleNamesOneProgramThatTheInstallerAndTheLanguagePickerUse) {
  const std::string rule = pm_no_comments(pm_read(pm_root() / "packaging/50-fleetwm-locale.rules"));
  EXPECT_NE(rule.find("org.freedesktop.policykit.exec"), std::string::npos);
  EXPECT_NE(rule.find("action.lookup(\"program\") == \"/usr/local/bin/fleetwm-locale-build\""), std::string::npos);
  const std::string build = pm_read(pm_root() / "meson.build");
  const size_t at = build.find("'packaging/fleetwm-locale-build'");
  ASSERT_NE(at, std::string::npos) << "the program the rule names must be installed by the build";
  EXPECT_NE(build.find("install_dir: get_option('bindir')", at), std::string::npos) << "into bindir, where the rule looks for it";
  EXPECT_TRUE(pm_fs::exists(pm_root() / "packaging/fleetwm-locale-build"));
  const std::string picker = pm_read(pm_root() / "apps/langpicker/main.cpp");
  EXPECT_NE(picker.find("pkexec \" FLEETWM_BINDIR \"/fleetwm-locale-build"), std::string::npos)
      << "the picker must run exactly the program the rule allows";
}

// ---- what the rules actually decide ------------------------------------------------------------------
//
// polkit evaluates rules with its own JavaScript engine, which is not available on a fresh install, so the
// tests carry a tiny evaluator for the exact shape the shipped rules use: one `if` whose condition is built
// from `action.id == "..."`, `action.lookup("program") == "..."`, `subject.local`, `subject.active`,
// `subject.isInGroup("...")`, `&&`, `||` and parentheses. A rule that uses anything else makes the evaluator
// throw, which fails the test and says what to add. Where node happens to be installed, one more test runs
// the real JavaScript and checks the evaluator agrees with it.

namespace {

struct PmQuery {
  std::string action_id, program;
  bool local = false, active = false;
  std::set<std::string> groups;
};

class PmRuleEval {
 public:
  PmRuleEval(const std::string& rule_source, const PmQuery& q) : q_(q) {
    const std::string code = pm_no_comments(rule_source);
    const size_t open = code.find("if (");
    if (open == std::string::npos) throw std::runtime_error("no if (...) in the rule");
    size_t i = open + 4, depth = 1;
    const size_t start = i;
    for (; i < code.size() && depth > 0; ++i) {
      if (code[i] == '"') {
        for (++i; i < code.size() && code[i] != '"'; ++i) {
        }
      } else if (code[i] == '(') {
        ++depth;
      } else if (code[i] == ')') {
        --depth;
      }
    }
    if (depth != 0) throw std::runtime_error("unbalanced if condition");
    cond_ = code.substr(start, i - 1 - start);
    yes_ = code.find("return polkit.Result.YES;", i) != std::string::npos;
  }

  // "yes", or "undecided" when polkit would carry on to its defaults.
  std::string decide() {
    pos_ = 0;
    const bool match = or_expr();
    skip();
    if (pos_ != cond_.size()) throw std::runtime_error("cannot evaluate: " + cond_.substr(pos_, 40));
    return match && yes_ ? "yes" : "undecided";
  }

 private:
  void skip() {
    while (pos_ < cond_.size() && std::isspace(static_cast<unsigned char>(cond_[pos_]))) ++pos_;
  }
  bool eat(const std::string& tok) {
    skip();
    if (cond_.compare(pos_, tok.size(), tok) != 0) return false;
    pos_ += tok.size();
    return true;
  }
  std::string string_literal() {
    skip();
    if (pos_ >= cond_.size() || cond_[pos_] != '"') throw std::runtime_error("string expected near: " + cond_.substr(pos_, 30));
    const size_t e = cond_.find('"', pos_ + 1);
    const std::string v = cond_.substr(pos_ + 1, e - pos_ - 1);
    pos_ = e + 1;
    return v;
  }
  bool or_expr() {
    bool v = and_expr();
    while (eat("||")) v = and_expr() || v;  // both sides are parsed even when decided
    return v;
  }
  bool and_expr() {
    bool v = primary();
    while (eat("&&")) v = primary() && v;
    return v;
  }
  bool primary() {
    if (eat("(")) {
      const bool v = or_expr();
      if (!eat(")")) throw std::runtime_error("missing )");
      return v;
    }
    if (eat("subject.local")) return q_.local;
    if (eat("subject.active")) return q_.active;
    if (eat("subject.isInGroup(")) {
      const std::string g = string_literal();
      if (!eat(")")) throw std::runtime_error("missing ) after isInGroup");
      return q_.groups.count(g) > 0;
    }
    std::string value;
    if (eat("action.id")) {
      value = q_.action_id;
    } else if (eat("action.lookup(")) {
      const std::string key = string_literal();
      if (!eat(")")) throw std::runtime_error("missing ) after lookup");
      value = key == "program" ? q_.program : "";
    } else {
      throw std::runtime_error("cannot evaluate: " + cond_.substr(pos_, 40));
    }
    if (!eat("==")) throw std::runtime_error("only == comparisons are supported near: " + cond_.substr(pos_, 30));
    return value == string_literal();
  }

  PmQuery q_;
  std::string cond_;
  bool yes_ = false;
  size_t pos_ = 0;
};

std::set<std::string> pm_split_groups(const std::string& csv) {
  std::set<std::string> out;
  std::stringstream ss(csv);
  for (std::string g; std::getline(ss, g, ',');)
    if (!g.empty()) out.insert(g);
  return out;
}

// Runs one rule file for an action and a subject; returns "yes" or "undecided".
std::string pm_decide(const std::string& rule_file, const std::string& action_id, bool local, bool active,
                      const std::string& groups, const std::string& program = "") {
  PmQuery q;
  q.action_id = action_id;
  q.program = program;
  q.local = local;
  q.active = active;
  q.groups = pm_split_groups(groups);
  try {
    return PmRuleEval(pm_read(pm_root() / "packaging" / rule_file), q).decide();
  } catch (const std::exception& e) {
    return std::string("evaluator error in ") + rule_file + ": " + e.what();
  }
}

bool pm_have_node() { return std::system("command -v node >/dev/null 2>&1") == 0; }

// The same questions put to real JavaScript in one node run: one line per query ("id|local|active|groups|program"),
// one answer line each.
std::vector<std::string> pm_decide_in_node(const std::string& rule_file, const std::vector<std::string>& queries) {
  static const char* const kHarness =
      "const fs=require('fs');const src=fs.readFileSync(process.argv[2],'utf8');let rule=null;\n"
      "const polkit={addRule:f=>{rule=f;},Result:{YES:'yes',NO:'no',AUTH_ADMIN:'auth_admin'}};\n"
      "new Function('polkit',src)(polkit);\n"
      "for(const line of fs.readFileSync(process.argv[3],'utf8').split('\\n')){\n"
      "  if(line==='')continue;const a=line.split('|');\n"
      "  const action={id:a[0],lookup:k=>k==='program'?a[4]:undefined};\n"
      "  const subject={local:a[1]==='1',active:a[2]==='1',isInGroup:g=>a[3].split(',').includes(g)};\n"
      "  const r=rule(action,subject);console.log(r===undefined?'undecided':r);}\n";
  const pm_fs::path dir = pm_fs::temp_directory_path();
  const std::string tag = std::to_string(getpid());
  const std::string harness = (dir / ("fleetwm-rule-harness-" + tag + ".js")).string();
  const std::string input = (dir / ("fleetwm-rule-queries-" + tag + ".txt")).string();
  std::ofstream(harness) << kHarness;
  {
    std::ofstream in(input);
    for (const std::string& q : queries) in << q << "\n";
  }
  const std::string cmd = "node '" + harness + "' '" + (pm_root() / "packaging" / rule_file).string() + "' '" + input + "' 2>&1";
  std::vector<std::string> out;
  if (FILE* p = popen(cmd.c_str(), "r")) {
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) {
      std::string line(buf);
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
      out.push_back(line);
    }
    pclose(p);
  }
  std::error_code ec;
  pm_fs::remove(harness, ec);
  pm_fs::remove(input, ec);
  return out;
}

const char* const pm_power_rule = "50-fleetwm-power.rules";

}  // namespace

TEST(PolkitRuleBehaviour, PowerMenuActionsAreAllowedWithoutAPasswordAtTheKeyboard) {
  for (int a = 0; a < fleetwm::power::kCount; ++a)
    for (const std::string& id : fleetwm::power::polkit_actions_for(a)) {
      EXPECT_EQ(pm_decide(pm_power_rule, id, true, true, ""), "yes") << id << " (user in no special group)";
      EXPECT_EQ(pm_decide(pm_power_rule, id, true, true, "sudo,adm"), "yes") << id << " (administrator)";
    }
}

TEST(PolkitRuleBehaviour, PowerActionsStayClosedToRemoteAndBackgroundSessions) {
  for (const char* id : {"org.freedesktop.login1.power-off", "org.freedesktop.login1.reboot",
                         "org.freedesktop.login1.suspend", "org.freedesktop.login1.power-off-multiple-sessions"}) {
    EXPECT_EQ(pm_decide(pm_power_rule, id, false, true, "sudo"), "undecided") << id << " over ssh";
    EXPECT_EQ(pm_decide(pm_power_rule, id, true, false, "sudo"), "undecided") << id << " from an inactive session";
    EXPECT_EQ(pm_decide(pm_power_rule, id, false, false, "sudo"), "undecided") << id;
  }
}

TEST(PolkitRuleBehaviour, PowerRuleLeavesEverythingElseToPolkit) {
  for (const char* id : {"org.freedesktop.login1.power-off-ignore-inhibit", "org.freedesktop.login1.reboot-ignore-inhibit",
                         "org.freedesktop.login1.suspend-ignore-inhibit", "org.freedesktop.login1.halt",
                         "org.freedesktop.login1.set-wall-message", "org.freedesktop.login1.lock-sessions",
                         "org.freedesktop.NetworkManager.settings.modify.system", "org.freedesktop.systemd1.manage-units",
                         "org.freedesktop.policykit.exec", ""})
    EXPECT_EQ(pm_decide(pm_power_rule, id, true, true, "sudo,wheel,adm"), "undecided") << id;
}

TEST(PolkitRuleBehaviour, TimeRuleNeedsAnAdminGroupAndALocalActiveSession) {
  for (const char* id : {"org.freedesktop.timedate1.set-timezone", "org.freedesktop.timedate1.set-ntp"}) {
    EXPECT_EQ(pm_decide("50-fleetwm-time.rules", id, true, true, "sudo"), "yes") << id;
    EXPECT_EQ(pm_decide("50-fleetwm-time.rules", id, true, true, "wheel"), "yes") << id;
    EXPECT_EQ(pm_decide("50-fleetwm-time.rules", id, true, true, "adm"), "yes") << id;
    EXPECT_EQ(pm_decide("50-fleetwm-time.rules", id, true, true, "users"), "undecided") << id << " for someone who is no administrator";
    EXPECT_EQ(pm_decide("50-fleetwm-time.rules", id, false, true, "sudo"), "undecided") << id << " over ssh";
    EXPECT_EQ(pm_decide("50-fleetwm-time.rules", id, true, false, "sudo"), "undecided") << id << " in the background";
  }
  EXPECT_EQ(pm_decide("50-fleetwm-time.rules", "org.freedesktop.timedate1.set-time", true, true, "sudo"), "undecided");
}

TEST(PolkitRuleBehaviour, LocaleRuleAllowsOnlyTheLocaleBuilder) {
  const std::string exec = "org.freedesktop.policykit.exec", good = "/usr/local/bin/fleetwm-locale-build";
  EXPECT_EQ(pm_decide("50-fleetwm-locale.rules", exec, true, true, "sudo", good), "yes");
  EXPECT_EQ(pm_decide("50-fleetwm-locale.rules", exec, true, true, "users", good), "undecided");
  EXPECT_EQ(pm_decide("50-fleetwm-locale.rules", exec, true, true, "sudo", "/usr/bin/rm"), "undecided");
  EXPECT_EQ(pm_decide("50-fleetwm-locale.rules", exec, true, true, "sudo", "/tmp/fleetwm-locale-build"), "undecided");
  EXPECT_EQ(pm_decide("50-fleetwm-locale.rules", exec, false, true, "sudo", good), "undecided");
  EXPECT_EQ(pm_decide("50-fleetwm-locale.rules", "org.freedesktop.login1.reboot", true, true, "sudo", good), "undecided");
}

TEST(PolkitRuleBehaviour, TheEvaluatorAgreesWithRealJavaScriptWhereNodeIsInstalled) {
  if (!pm_have_node()) return;  // nothing to compare against here; the tests above need no node
  const std::vector<std::string> actions = {"org.freedesktop.login1.power-off", "org.freedesktop.login1.reboot-multiple-sessions",
                                            "org.freedesktop.login1.suspend-ignore-inhibit", "org.freedesktop.timedate1.set-ntp",
                                            "org.freedesktop.timedate1.set-time", "org.freedesktop.policykit.exec", ""};
  for (const char* file : {"50-fleetwm-power.rules", "50-fleetwm-time.rules", "50-fleetwm-locale.rules"}) {
    std::vector<std::string> queries, ours;
    for (const std::string& action : actions)
      for (int local = 0; local < 2; ++local)
        for (int active = 0; active < 2; ++active)
          for (const char* groups : {"", "sudo", "users"})
            for (const char* program : {"", "/usr/local/bin/fleetwm-locale-build", "/usr/bin/rm"}) {
              queries.push_back(action + "|" + std::to_string(local) + "|" + std::to_string(active) + "|" + groups + "|" + program);
              ours.push_back(pm_decide(file, action, local, active, groups, program));
            }
    const std::vector<std::string> theirs = pm_decide_in_node(file, queries);
    ASSERT_EQ(theirs.size(), queries.size()) << file << ": node did not answer every query";
    for (size_t i = 0; i < queries.size(); ++i) ASSERT_EQ(ours[i], theirs[i]) << file << " " << queries[i];
  }
}
