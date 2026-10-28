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

# ---- which processes belong to the game, which GPU they use, what they cost --------------------------------------------------------------------------------
def game_pids(steam_id=None, match=None):
    """Pids of this user's processes that belong to the game: Steam's SteamAppId / SteamGameId in the environment, or a command line containing `match`."""
    me = os.getuid()
    out = []
    for d in os.listdir("/proc"):
        if not d.isdigit(): continue
        try:
            if os.stat(f"/proc/{d}").st_uid != me: continue
            ok = False
            if steam_id:
                env = open(f"/proc/{d}/environ", "rb").read()
                ok = (f"SteamAppId={steam_id}\0".encode() in env) or (f"SteamGameId={steam_id}\0".encode() in env)
            if not ok and match:
                ok = match.lower() in open(f"/proc/{d}/cmdline", "rb").read().decode("utf-8", "replace").lower()
            if ok: out.append(int(d))
        except OSError:
            continue
    return out

def cpu_ticks(pid):
    try:
        f = open(f"/proc/{pid}/stat").read().rsplit(")", 1)[1].split()
        return int(f[11]) + int(f[12])
    except (OSError, IndexError, ValueError):
        return 0

def rss_mb(pid):
    try:
        return int(open(f"/proc/{pid}/statm").read().split()[1]) * os.sysconf("SC_PAGE_SIZE") / 1048576
    except (OSError, IndexError, ValueError):
        return 0

def amd_gfx_ns(pids):
    """Summed amdgpu graphics engine time (ns) of the processes: it grows while the iGPU draws for them."""
    total = 0
    for pid in pids:
        try:
            for fd in os.listdir(f"/proc/{pid}/fdinfo"):
                t = open(f"/proc/{pid}/fdinfo/{fd}").read()
                if "drm-driver:\tamdgpu" in t:
                    m = re.search(r"drm-engine-gfx:\s+(\d+) ns", t)
                    if m: total += int(m.group(1))
        except OSError:
            continue
    return total

def nvidia_users(pids):
    """(pids from `pids` that nvidia-smi lists as using the card, sm %, mem MB)"""
    out = run(["nvidia-smi", "pmon", "-c", "1", "-s", "um"], 8)
    hit, sm = [], 0
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 4 and f[1].isdigit() and int(f[1]) in pids:
            hit.append(int(f[1]))
            try: sm = max(sm, int(f[3]))
            except ValueError: pass
    vram = 0
    for line in run(["nvidia-smi", "--query-compute-apps=pid,used_memory", "--format=csv,noheader,nounits"], 6).splitlines():
        a = [x.strip() for x in line.split(",")]
        if len(a) == 2 and a[0].isdigit() and int(a[0]) in pids and a[1].isdigit(): vram += int(a[1])
    return hit, sm, vram

class Meter:
    """Samples the game's processes: CPU %, memory, and which GPU(s) they use."""
    def __init__(self, steam_id, match):
        self.steam_id, self.match = steam_id, match
        self.t = time.time()
        self.pids = game_pids(steam_id, match)
        self.ticks = sum(cpu_ticks(p) for p in self.pids)
        self.amd = amd_gfx_ns(self.pids)
        self.used = set()
    def sample(self):
        now = time.time()
        pids = game_pids(self.steam_id, self.match)
        ticks = sum(cpu_ticks(p) for p in pids)
        amd = amd_gfx_ns(pids)
        dt = max(0.1, now - self.t)
        cpu = 100.0 * (ticks - self.ticks) / os.sysconf("SC_CLK_TCK") / dt if ticks >= self.ticks else 0
        amd_pct = 100.0 * (amd - self.amd) / 1e9 / dt if amd >= self.amd else 0
        nv_hit, nv_sm, nv_mem = nvidia_users(pids)
        if nv_hit: self.used.add("NVIDIA")
        if amd_pct > 1: self.used.add("AMD")
        self.t, self.pids, self.ticks, self.amd = now, pids, ticks, amd
        return {"procs": len(pids), "cpu": cpu, "rss": sum(rss_mb(p) for p in pids), "nv": bool(nv_hit), "nv_sm": nv_sm, "nv_mem": nv_mem, "amd_pct": amd_pct}
    def gpu_text(self):
        return " + ".join(sorted(self.used)) + (" (RTX 3050 Ti)" if "NVIDIA" in self.used else "") if self.used else "unknown"

def kill_steam_games():
    """Steam runs one game at a time: a game left over from an earlier test (even one with no window) makes every later launch do nothing. Stops every
    process of this user that carries SteamGameId (the games and their reapers, not the Steam client)."""
    me = os.getuid()
    pids = []
    for d in os.listdir("/proc"):
        if not d.isdigit(): continue
        try:
            if os.stat(f"/proc/{d}").st_uid != me: continue
            if b"SteamGameId=" in open(f"/proc/{d}/environ", "rb").read(): pids.append(int(d))
        except OSError:
            continue
    if pids:
        stop_game(pids)
    return len(pids)

def stop_game(pids):
    """Anything of the game still running after its window is closed is asked to stop, then forced."""
    for sig, wait in ((signal.SIGTERM, 8), (signal.SIGKILL, 3)):
        left = [p for p in pids if os.path.exists(f"/proc/{p}")]
        if not left: return
        for p in left:
            try: os.kill(p, sig)
            except OSError: pass
        time.sleep(wait)

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
    ap.add_argument("--pid-match", help="substring of the command line that identifies the game's processes (Steam games are found by their SteamAppId)")
    ap.add_argument("--skip-done", action="store_true", help="do nothing when a report for this game already exists today")
    ap.add_argument("--attach", action="store_true", help="the game is already running: do not launch it, watch the windows that match --window-match")
    a = ap.parse_args()
    if a.minutes is None: a.minutes = 1.0 if a.kind == "native" else 2.0

    outdir = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "perflog", "testbed", datetime.date.today().isoformat())
    os.makedirs(outdir, exist_ok=True)
    slug = re.sub(r"[^A-Za-z0-9]+", "-", a.name).strip("-")
    if a.skip_done and os.path.exists(os.path.join(outdir, slug + ".txt")):
        print(f"skipping {a.name}: already tested today")
        return
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
        left = kill_steam_games()
        if left: say(f"stopped {left} leftover game process(es) from an earlier run")
        subprocess.Popen(cmd, env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

    ERR = re.compile(r"unsupported|error|crash|fatal|failed|cannot|can't|could not|exception|not found|unable|problem|stopped working", re.I)
    INSTALL = re.compile(r"install|setup|redistributable|prerequisite|vcredist|directx|\.net|runtime|dependenc|updating|downloading|extracting|unpack|first[- ]time", re.I)
    PLAUNCHER = ("steamwebhelper", "steam", "heroic", "lutris", "net.lutris.lutris")
    seen_dialogs = {}
    error_titles, install_titles, steam_dialogs = [], [], []

    def kind_of(w):
        """game | error | install | steam-dialog | other (not ours)"""
        if w["id"] in before_windows: return "other"
        t = w["title"]
        if ERR.search(t): return "error"
        if INSTALL.search(t): return "install"
        if w["app"].lower() == "steam" and t.strip() not in ("", "Steam"): return "steam-dialog"
        if w["app"].lower() in PLAUNCHER: return "other"
        if a.window_match and a.window_match.lower() not in (w["app"] + " " + t).lower(): return "other"
        return "game"

    def handle_dialogs(now_s):
        """Error dialogs are photographed and closed, install dialogs are waited out, Steam's own dialogs (launch options, first-run) are answered with Enter."""
        for w in windows():
            k = kind_of(w)
            if k in ("game", "other") or seen_dialogs.get(w["id"]) == k: continue
            seen_dialogs[w["id"]] = k
            shot(os.path.join(outdir, f"{slug}-{k}-{w['id']}.png"))
            say(f"t={now_s:5.0f}s {k.upper()} DIALOG: app={w['app']!r} title={w['title']!r} (screenshot {slug}-{k}-{w['id']}.png)")
            if k == "error":
                error_titles.append(w["title"])
                ctl("close", w["id"])
            elif k == "install":
                install_titles.append(w["title"])
            elif k == "steam-dialog":
                steam_dialogs.append(w["title"])
                run(["wtype", "-k", "Return"], 5)
        return any(kind_of(w) == "install" for w in windows())

    def alive():
        return [w for w in windows() if kind_of(w) == "game"]

    meter = Meter(a.steam, a.pid_match)
    game = None
    deadline = t0 + a.start_timeout
    extended = False
    while time.time() < deadline:
        time.sleep(2)
        installing = handle_dialogs(time.time() - t0)
        if installing and not extended:   # an installer is running (redistributables, first run): give it up to five more minutes
            deadline += 300
            extended = True
            say("an installer is running: waiting up to 5 more minutes for it")
        cur = alive()
        if cur:
            game = cur[0]
            break
    if not game:
        say(f"RESULT: FAIL no game window within {int(time.time() - t0)}s; windows: " + "; ".join(f"{w['app']}/{w['title']}" for w in windows()))
        stop_game(meter.pids)
    else:
        say(f"window after {time.time() - t0:.0f}s: id={game['id']} flags={game['flags']} {game['where']} app={game['app']!r} title={game['title']!r}")

    samples = []
    black = 0
    end = time.time() + a.minutes * 60
    crashed_at = None
    n = 0
    meter.sample()
    while game and time.time() < end:
        time.sleep(10)
        n += 1
        handle_dialogs(time.time() - t0)
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
        fs = frame_stats(True)
        m = meter.sample()
        mean, colours = shot(os.path.join(outdir, f"{slug}-{n:03d}.png")) if n % 3 == 1 else (None, None)
        if mean is not None and (mean < 4 or colours < 6): black += 1
        samples.append((fs, m))
        say(f"t={time.time() - t0:5.0f}s procs={m['procs']:2d} cpu={m['cpu']:5.0f}% ram={m['rss']:5.0f}MB nvidia={'yes sm=%d%% vram=%dMB' % (m['nv_sm'], m['nv_mem']) if m['nv'] else 'no'} amd_gfx={m['amd_pct']:3.0f}% "
            f"card={gpu()}" + (f" fps={fs['fps']:.0f} p99={fs.get('p99', 0):.1f}ms hitch={int(fs.get('hitch', 0))}" if fs.get("n") else "") +
            (f" shot_mean={mean:.0f} colours={colours}" if mean is not None else ""))
        if mean is not None and n > 1 and mean >= 4 and colours >= 6 and n % 6 != 1:
            try: os.remove(os.path.join(outdir, f"{slug}-{n:03d}.png"))
            except OSError: pass

    # exit: ask the window to close and watch the frame pacing while the game goes away
    if game and crashed_at is None:
        frame_stats(True)
        say("closing the game window...")
        for w in alive(): ctl("close", w["id"])
        t1 = time.time()
        while time.time() - t1 < 25:
            time.sleep(1)
            if not alive(): break
        exit_stats = frame_stats(True)
        say(f"exit: window gone after {time.time() - t1:.0f}s" + (f"; frame pacing during exit: fps={exit_stats.get('fps', 0):.1f} p99={exit_stats.get('p99', 0):.2f} max={exit_stats.get('max', 0):.2f} late={int(exit_stats.get('late', 0))} hitch={int(exit_stats.get('hitch', 0))}" if exit_stats.get("n") else ""))
    stop_game(meter.pids or game_pids(a.steam, a.pid_match))
    if a.steam: kill_steam_games()
    if game: time.sleep(8)  # the boost ends six seconds after the last game window

    gm, prof = tux()
    say(f"after: tuxthrottle game_mode={gm} platform_profile={prof}")
    if gm == "true":
        say("WARNING: Game Mode is still on; switching it off")
        run(["tuxthrottlectl", "gamemode", "off"], 30)
    new_log = read_tail(log, log_off) if log else ""
    bad = [l for l in new_log.splitlines() if re.search(r"\[ERROR\]|assert|Assertion|page-flip|segfault|crash|fleetwm: .*refused", l, re.I) and "xwm.c" not in l]
    for l in bad[:15]: say("log: " + l[:200])
    for l in [l for l in new_log.splitlines() if l.startswith("fleetwm: game boost")][:6]: say("log: " + l)

    verdict = None
    if samples:
        cpu = [m["cpu"] for _, m in samples]; ram = [m["rss"] for _, m in samples]
        sm = [m["nv_sm"] for _, m in samples if m["nv"]]
        say(f"GPU used: {meter.gpu_text()}")
        say(f"stats: avg cpu {sum(cpu) / len(cpu):.0f}%, peak ram {max(ram):.0f} MB, nvidia sm avg {sum(sm) / max(1, len(sm)):.0f}%, black screenshots {black}, log problems {len(bad)}")
        verdict = "PASS" if crashed_at is None and black == 0 and not error_titles else "FAIL"
    if verdict:
        why = [("black screen" if black else ""), ("crash" if crashed_at is not None else ""), (f"error dialog {error_titles[0]!r}" if error_titles else "")]
        say(f"RESULT: {verdict}" + ("" if verdict == "PASS" else " (" + ", ".join(x for x in why if x) + ")"))
    if install_titles: say("install dialogs seen: " + "; ".join(sorted(set(install_titles))))
    if steam_dialogs: say("Steam dialogs answered: " + "; ".join(sorted(set(steam_dialogs))))
    with open(os.path.join(outdir, slug + ".txt"), "w") as f:
        f.write("\n".join(report) + "\n")

if __name__ == "__main__":
    main()
