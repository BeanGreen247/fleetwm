#include <gtest/gtest.h>

#include "sessions.hpp"

using namespace fleetwm;

TEST(Sessions, ParsesNameExecAndDesktopNames) {
  const auto s = parse_session_desktop("plasma", SessionKind::Wayland,
                                       "[Desktop Entry]\nName=Plasma (Wayland)\nExec=/usr/lib/plasma-dbus-run-session-if-needed startplasma-wayland %u\n"
                                       "DesktopNames=KDE;Plasma;\n");
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->id, "wayland:plasma");
  EXPECT_EQ(s->name, "Plasma (Wayland)");
  EXPECT_EQ(s->exec, "/usr/lib/plasma-dbus-run-session-if-needed startplasma-wayland");
  EXPECT_EQ(s->desktop_names, "KDE:Plasma");
}

TEST(Sessions, FallsBackToStemAndKeepsPercentPercent) {
  const auto s = parse_session_desktop("mate", SessionKind::X11, "[Desktop Entry]\nExec=mate-session 100%%\n");
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->id, "x11:mate");
  EXPECT_EQ(s->name, "mate");
  EXPECT_EQ(s->exec, "mate-session 100%");
  EXPECT_EQ(s->desktop_names, "mate");
}

TEST(Sessions, SkipsHiddenMissingExecAndMissingTryExec) {
  EXPECT_FALSE(parse_session_desktop("a", SessionKind::X11, "[Desktop Entry]\nExec=x\nHidden=true\n"));
  EXPECT_FALSE(parse_session_desktop("a", SessionKind::X11, "[Desktop Entry]\nName=A\n"));
  EXPECT_FALSE(parse_session_desktop("a", SessionKind::X11, "[Desktop Entry]\nExec=x\nTryExec=/nonexistent/binary\n"));
  EXPECT_TRUE(parse_session_desktop("a", SessionKind::X11, "[Desktop Entry]\nExec=x\nTryExec=sh\n"));
}

TEST(Sessions, IgnoresKeysOutsideDesktopEntry) {
  EXPECT_FALSE(parse_session_desktop("a", SessionKind::X11, "[Other]\nExec=x\n"));
}

TEST(Sessions, FleetwmIsAlwaysFirstEvenWithoutDirectories) {
  const auto list = discover_sessions({"/nonexistent"}, {"/nonexistent"}, true, "/usr/local/bin/fleetwm");
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, kFleetwmSessionId);
  EXPECT_EQ(list[0].exec, "/usr/local/bin/fleetwm");
}

TEST(Sessions, LaunchCommands) {
  SessionEntry wl;
  wl.id = "wayland:sway";
  wl.exec = "sway --unsupported-gpu";
  EXPECT_EQ(session_argv(wl, "/c", 0, 1, true), (std::vector<std::string>{"/bin/sh", "-c", "exec sway --unsupported-gpu"}));

  SessionEntry fl;
  fl.id = kFleetwmSessionId;
  EXPECT_EQ(session_argv(fl, "/usr/local/bin/fleetwm", 0, 1, true), (std::vector<std::string>{"/usr/local/bin/fleetwm"}));

  SessionEntry x;
  x.id = "x11:mate";
  x.kind = SessionKind::X11;
  x.exec = "mate-session";
  EXPECT_EQ(session_argv(x, "/c", 2, 3, true),
            (std::vector<std::string>{"/usr/bin/xinit", "/etc/X11/Xsession", "mate-session", "--", "/usr/bin/Xorg", ":2",
                                      "vt3", "-keeptty", "-noreset"}));
}

TEST(Sessions, VtFromTty) {
  EXPECT_EQ(vt_from_tty("tty1"), 1);
  EXPECT_EQ(vt_from_tty("tty12"), 12);
  EXPECT_EQ(vt_from_tty("ttyS0"), 0);
  EXPECT_EQ(vt_from_tty(""), 0);
}

TEST(Sessions, FindSession) {
  const auto list = discover_sessions({}, {}, false, "/c");
  EXPECT_NE(find_session(list, kFleetwmSessionId), nullptr);
  EXPECT_EQ(find_session(list, "x11:evil;rm"), nullptr);
}
