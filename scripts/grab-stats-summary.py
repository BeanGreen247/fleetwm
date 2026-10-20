#!/usr/bin/env python3
"""Median of every GRABSTATS field per mode, and the compositor's Render/3D busy per phase from intel_gpu_top JSON (see grab-stats-run.sh)."""
import glob, json, re, statistics, sys

log, out, tag, pid = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
rows = {}
phase = "idle"
for line in open(log, errors="replace"):
    if line.startswith("PHASE "):
        phase = line.split()[1]
        continue
    if not line.startswith("GRABSTATS"):
        continue
    f = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
    d = {"dt": float(f.get("dt", 1.0))}
    for k, v in f.items():
        if k in ("mode", "out", "dt"):
            continue
        if "/" in v:
            a, b = v.split("/")
            d[k + "_avg"], d[k + "_max"] = float(a), float(b)
        else:
            d[k] = float(v)
    # idle and idle2 are one control; the script's own phase decides, not the grab mode
    rows.setdefault(re.sub(r"2$", "", phase), []).append(d)

print(f"== {tag}: GRABSTATS, rates per second over each phase (all rounds together; the seconds are the time the lines cover)")
counts = ["events", "pos", "resize_req", "snap", "frames", "cap_wait", "cap_idle", "commits", "present", "missed", "titlebar_renders", "strip_renders", "glass_builds", "glass_ms"]
for mode in ("idle", "move", "resize"):
    rs = rows.get(mode, [])
    secs = sum(r["dt"] for r in rs)
    if secs <= 0:
        print(f"{mode:7s} no lines")
        continue
    rate = " ".join(f"{k}/s={sum(r.get(k, 0) for r in rs) / secs:.1f}" for k in counts)
    commits = sum(r.get("commits", 0) for r in rs)
    cms = sum(r.get("commit_ms_avg", 0) * r.get("commits", 0) for r in rs) / commits if commits else 0
    dpx = sum(r.get("damage_px_avg", 0) * r.get("commits", 0) for r in rs) / commits if commits else 0
    print(f"{mode:7s} {secs:5.1f}s {rate} commit_ms_avg={cms:.2f} commit_ms_max={max((r.get('commit_ms_max', 0) for r in rs), default=0):.2f} damage_px_avg={dpx:.0f} damage_px_max={max((r.get('damage_px_max', 0) for r in rs), default=0):.0f}")

def samples(path):
    t = open(path).read()
    res = []
    for o in re.findall(r'\{\s*"period".*?\n\}', t, re.S):
        try:
            j = json.loads(o)
        except Exception:
            continue
        mine = None
        for c in (j.get("clients") or {}).values():
            if str(c.get("pid")) == str(pid):
                mine = float(c["engine-classes"]["Render/3D"]["busy"])
        dev = float(j["engines"]["Render/3D"]["busy"])
        res.append((mine, dev, j["frequency"]["actual"], j["rc6"]["value"]))
    return res[1:-1] or res

gp = sorted(glob.glob(f"{out}/gpu-{tag}-*.json"))
if gp:
    print(f"== {tag}: Render/3D busy % per phase (compositor pid {pid}; device-wide in brackets), 500 ms samples")
    by = {}
    for p in gp:
        name = re.sub(r".*-", "", p[:-5])
        by.setdefault(name, []).extend(samples(p))
    for name in ("idle", "move", "resize", "idle2"):
        s = by.get(name, [])
        if not s:
            continue
        mine = [x[0] for x in s if x[0] is not None]
        dev = [x[1] for x in s]
        fr = [x[2] for x in s]
        m = f"mean {statistics.mean(mine):.1f} median {statistics.median(mine):.1f} max {max(mine):.1f}" if mine else "no per-process record"
        print(f"{name:6s} n={len(s):3d} compositor {m}  [device mean {statistics.mean(dev):.1f} max {max(dev):.1f}]  freq {statistics.mean(fr):.0f} MHz")
