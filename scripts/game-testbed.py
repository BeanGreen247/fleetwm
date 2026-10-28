#!/usr/bin/env python3
"""Runs one game for a few minutes inside the real Fleetwm session and writes a report: did it start, did it stay up, did the screen go black, how evenly were
frames presented (fleetwm-ctl frame-stats), what did the compositor log, what happened on exit (the stutter on closing a game shows up as hitched frames there).

  scripts/game-testbed.py --name "GTA III" --steam 1546970 --minutes 6
  scripts/game-testbed.py --name "osu!lazer" --cmd "osu-lazer-launcher run" --window-match osu --minutes 5
  scripts/game-testbed.py --name Rayman2 --heroic "legendary/<appname>" ...   (heroic://launch/<runner>/<appname>)
  scripts/game-testbed.py --name WoW --lutris net.lutris.battlenet-1 ...

Run it on the machine that has the session, as the session's user (over ssh it finds the session itself). It opens the game on the real screen and takes screenshots of it
(grim) while the test runs: that is the point of the test. Reports go to perflog/testbed/<date>/<name>.txt.
"""
import argparse, glob, os, re, signal, subprocess, sys, time, datetime, shlex

def session_env():
    env = dict(os.environ)
    uid = os.getuid()
    rt = env.get("XDG_RUNTIME_DIR") or f"/run/user/{uid}"
    env["XDG_RUNTIME_DIR"] = rt
    if "WAYLAND_DISPLAY" not in env:
        socks = sorted(glob.glob(os.path.join(rt, "wayland-*")))
        socks = [s for s in socks if not s.endswith(".lock")]
        if socks:
            env["WAYLAND_DISPLAY"] = os.path.basename(socks[0])
    env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path={rt}/bus")
    if "DISPLAY" not in env:
        x = sorted(glob.glob("/tmp/.X11-unix/X*"))
        if x:
            env["DISPLAY"] = ":" + os.path.basename(x[0])[1:]
    return env

ENV = session_env()

def run(cmd, timeout=15):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=ENV).stdout
    except Exception as e:  # noqa: BLE001
        return f"ERR {e}"

def ctl(*args):
    return run(["fleetwm-ctl", *args], 6)

def windows():
    out = ctl("windows")
    rows = []
    for line in out.splitlines():
        if line.startswith("WINDOWS"):
            for part in line.split("\t")[1:]:
                pass
            f = line.split("\t")[1:]
            for i in range(0, len(f) - 4, 5):
                rows.append({"id": f[i], "flags": f[i + 1], "where": f[i + 2], "app": f[i + 3], "title": f[i + 4]})
    return rows

def frame_stats(reset=True):
    out = ctl("frame-stats", "reset") if reset else ctl("frame-stats")
    d = {}
    for line in out.splitlines():
        if line.startswith("FRAME_STATS"):
            for kv in line.split()[2:]:
                k, _, v = kv.partition("=")
                try: d[k] = float(v)
                except ValueError: pass
            break
    return d

def shot(path):
    run(["grim", path], 10)
    try:
        from PIL import Image, ImageStat
        im = Image.open(path).convert("RGB").resize((160, 90))
        st = ImageStat.Stat(im)
        mean = sum(st.mean) / 3
        colours = len(set(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()))
        return mean, colours
    except Exception:  # noqa: BLE001
        return None, None

def session_log():
    for p in glob.glob(os.path.expanduser("~/.local/share/plasmalogin/wayland-session.log")) + glob.glob(os.path.expanduser("~/fleetwm-session.log")):
        return p
    return None

def read_tail(path, offset):
    try:
        with open(path, "rb") as f:
            f.seek(offset)
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""

def tux():
    out = run(["tuxthrottlectl", "status", "--json"], 10)
    m = re.search(r'"game_mode":\s*(true|false)', out)
    prof = re.search(r'"platform_profile":\s*"([^"]*)"', out)
    return (m.group(1) if m else "?"), (prof.group(1) if prof else "?")

def gpu():
    out = run(["nvidia-smi", "--query-gpu=utilization.gpu,power.draw,temperature.gpu", "--format=csv,noheader"], 6)
    return out.strip() or "-"

def steam_games():
    """[(kind, appid, name)] of the installed Steam games: kind is "native" when the folder has no .exe within three levels, else "proton". Tools are left out."""
    libs = [os.path.expanduser("~/.local/share/Steam")]
    try:
        text = open(os.path.expanduser("~/.local/share/Steam/steamapps/libraryfolders.vdf")).read()
        libs += re.findall(r'"path"\s+"([^"]+)"', text)
    except OSError:
        pass
    seen, out = set(), []
    for lib in dict.fromkeys(libs):
        for f in glob.glob(os.path.join(lib, "steamapps", "appmanifest_*.acf")):
            t = open(f, errors="replace").read()
            m_id, m_nm, m_dir = (re.search(r'"%s"\s+"([^"]*)"' % k, t) for k in ("appid", "name", "installdir"))
            if not (m_id and m_nm and m_dir) or m_id.group(1) in seen: continue
            name = m_nm.group(1)
            if name.startswith(("Proton", "Steam Linux", "Steamworks", "Steam Controller", "Steam.dll", "Steam Deck")): continue
            path = os.path.join(lib, "steamapps", "common", m_dir.group(1))
            if not os.path.isdir(path): continue
            seen.add(m_id.group(1))
            has_exe = False
            for root, dirs, files in os.walk(path):
                if root[len(path):].count(os.sep) >= 3: dirs[:] = []
                if any(x.lower().endswith(".exe") for x in files): has_exe = True; break
            out.append(("proton" if has_exe else "native", m_id.group(1), name))
    return sorted(out, key=lambda r: (r[0], r[2].lower()))

def main():
    if "--list-steam" in sys.argv:
        for k, i, n in steam_games(): print(f"{k}|{i}|{n}")
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--steam"); ap.add_argument("--heroic"); ap.add_argument("--lutris"); ap.add_argument("--cmd")
    ap.add_argument("--window-match", help="substring of the app id or title that identifies the game's window (default: any new window)")
    ap.add_argument("--kind", choices=["native", "proton"], default="proton", help="native Linux games are only checked that they start and stay up (1 minute); Proton and Wine games get 2 minutes")
    ap.add_argument("--minutes", type=float, default=None)
    ap.add_argument("--start-timeout", type=int, default=150)
    ap.add_argument("--out", default=None)
    ap.add_argument("--attach", action="store_true", help="the game is already running: do not launch it, watch the windows that match --window-match")
    a = ap.parse_args()
    if a.minutes is None: a.minutes = 1.0 if a.kind == "native" else 2.0

    outdir = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "perflog", "testbed", datetime.date.today().isoformat())
    os.makedirs(outdir, exist_ok=True)
    slug = re.sub(r"[^A-Za-z0-9]+", "-", a.name).strip("-")
    report = []
    def say(s):
        print(s, flush=True)
        report.append(s)

    log = session_log()
    log_off = os.path.getsize(log) if log else 0
    before_windows = {w["id"] for w in windows()}
    before_game_mode, before_profile = tux()
    say(f"# {a.name} - {datetime.datetime.now().isoformat(timespec='seconds')}")
    say(f"before: tuxthrottle game_mode={before_game_mode} platform_profile={before_profile}")

    if a.steam: cmd = ["steam", f"steam://rungameid/{a.steam}"]
    elif a.heroic: cmd = ["heroic", f"heroic://launch/{a.heroic}"]
    elif a.lutris: cmd = ["lutris", f"lutris:rungame/{a.lutris}" if not a.lutris.isdigit() else f"lutris:rungameid/{a.lutris}"]
    elif a.cmd: cmd = shlex.split(a.cmd)
    elif a.attach: cmd = []
    else: sys.exit("give --steam, --heroic, --lutris or --cmd")
    if a.attach: before_windows.clear()
    say("launch: " + (" ".join(cmd) if cmd else "(attached to the running game)"))
    frame_stats(True)
    t0 = time.time()
    if cmd and not a.attach:
        subprocess.Popen(cmd, env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

    def mine(w):
        if w["id"] in before_windows: return False
        if a.window_match: return a.window_match.lower() in (w["app"] + " " + w["title"]).lower()
        return w["app"].lower() not in ("steamwebhelper", "steam", "heroic", "lutris", "net.lutris.lutris")

    def alive():
        return [w for w in windows() if mine(w)]

    game = None
    while time.time() - t0 < a.start_timeout:
        time.sleep(2)
        cur = alive()
        if cur:
            game = cur[0]
            break
    if not game:
        say(f"RESULT: FAIL no game window within {a.start_timeout}s")
    else:
        say(f"window after {time.time() - t0:.0f}s: id={game['id']} flags={game['flags']} {game['where']} app={game['app']!r} title={game['title']!r}")

    ERR = re.compile(r"unsupported|error|crash|fatal|failed|cannot|can't|could not|exception|not found|unable", re.I)
    error_titles = []
    samples = []
    black = 0
    end = time.time() + a.minutes * 60
    crashed_at = None
    n = 0
    while game and time.time() < end:
        time.sleep(10)
        n += 1
        cur = alive()
        if not cur:
            # a launcher closing as the game opens is normal: wait up to 30 s for the next window of the same program
            gone_at = time.time()
            while time.time() - gone_at < 30 and not cur:
                time.sleep(2)
                cur = alive()
            if not cur:
                crashed_at = time.time() - t0
                say(f"RESULT: no window of the game for 30 s after {crashed_at - 30:.0f}s (crash or exit)")
                break
        new_game = max(cur, key=lambda w: int(w["id"]))
        if new_game["id"] != game["id"] or new_game["title"] != game["title"]:
            say(f"t={time.time() - t0:5.0f}s window now: id={new_game['id']} app={new_game['app']!r} title={new_game['title']!r}")
        game = new_game
        ids = {w["id"]: w for w in cur}
        for w in cur:
            if ERR.search(w["title"]) and w["title"] not in error_titles:
                error_titles.append(w["title"])
                say(f"t={time.time() - t0:5.0f}s ERROR WINDOW: app={w['app']!r} title={w['title']!r}")
        fs = frame_stats(True)
        mean, colours = shot(os.path.join(outdir, f"{slug}-{n:03d}.png")) if n % 3 == 1 else (None, None)
        if mean is not None and (mean < 4 or colours < 6): black += 1
        samples.append(fs)
        say(f"t={time.time() - t0:5.0f}s fps={fs.get('fps', 0):6.1f} p99={fs.get('p99', 0):6.2f} max={fs.get('max', 0):7.2f} late={int(fs.get('late', 0)):3d} hitch={int(fs.get('hitch', 0)):3d} "
            f"work_max={fs.get('work_max', 0):5.2f} flags={ids[game['id']]['flags']} gpu={gpu()}" + (f" shot_mean={mean:.0f} colours={colours}" if mean is not None else ""))
        # drop the screenshots after the first and last of the run: they are only kept for the report
        if mean is not None and n > 1 and mean >= 4 and colours >= 6 and n % 6 != 1:
            try: os.remove(os.path.join(outdir, f"{slug}-{n:03d}.png"))
            except OSError: pass

    # exit: ask the window to close and watch the frame pacing while the game goes away
    exit_stats = None
    if game and crashed_at is None:
        frame_stats(True)
        say("closing the game window...")
        for w in alive(): ctl("close", w["id"])
        t1 = time.time()
        while time.time() - t1 < 25:
            time.sleep(1)
            if not alive(): break
        exit_stats = frame_stats(True)
        say(f"exit: window gone after {time.time() - t1:.0f}s; frame pacing during exit: fps={exit_stats.get('fps', 0):.1f} p99={exit_stats.get('p99', 0):.2f} max={exit_stats.get('max', 0):.2f} "
            f"late={int(exit_stats.get('late', 0))} hitch={int(exit_stats.get('hitch', 0))} work_max={exit_stats.get('work_max', 0):.2f}")
        time.sleep(10)  # the boost ends six seconds after the last game window
        tail_fs = frame_stats(True)
        say(f"after exit: hitch={int(tail_fs.get('hitch', 0))} max={tail_fs.get('max', 0):.2f}")

    gm, prof = tux()
    say(f"after: tuxthrottle game_mode={gm} platform_profile={prof}")
    if gm == "true":
        say("WARNING: Game Mode is still on; switching it off")
        run(["tuxthrottlectl", "gamemode", "off"], 30)
    new_log = read_tail(log, log_off) if log else ""
    bad = [l for l in new_log.splitlines() if re.search(r"\[ERROR\]|assert|Assertion|page-flip|segfault|crash|fleetwm: .*refused", l, re.I)]
    for l in bad[:15]: say("log: " + l[:200])
    for l in [l for l in new_log.splitlines() if l.startswith("fleetwm: game boost")][:6]: say("log: " + l)

    if samples:
        fps = [s.get("fps", 0) for s in samples if s]
        hitch = sum(int(s.get("hitch", 0)) for s in samples)
        late = sum(int(s.get("late", 0)) for s in samples)
        say(f"summary: windows ok for {len(samples) * 10}s, avg fps {sum(fps) / max(1, len(fps)):.1f}, late frames {late}, hitched frames {hitch}, black screenshots {black}, log problems {len(bad)}")
        verdict = "PASS" if crashed_at is None and black == 0 and not error_titles else "FAIL"
        say(f"RESULT: {verdict}" + ("" if verdict == "PASS" else " (" + ("black screen " if black else "") + ("crash " if crashed_at is not None else "") + (f"error window: {error_titles[0]!r}" if error_titles else "") + ")"))
    with open(os.path.join(outdir, slug + ".txt"), "w") as f:
        f.write("\n".join(report) + "\n")

if __name__ == "__main__":
    main()
