#!/usr/bin/env python3
"""Per-thread idle baseline: CPU ticks, context switches per second, RSS, PSS over a window.
usage: procstat.py SECONDS PID [PID...]"""
import os, sys, time

def threads(pid):
    out = {}
    for t in os.listdir(f"/proc/{pid}/task"):
        try:
            st = open(f"/proc/{pid}/task/{t}/stat").read()
            comm = st[st.index("(") + 1:st.rindex(")")]
            f = st[st.rindex(")") + 2:].split()
            ticks = int(f[11]) + int(f[12])  # utime + stime
            sw = 0
            for l in open(f"/proc/{pid}/task/{t}/status"):
                if "ctxt_switches" in l:
                    sw += int(l.split()[1])
            out[t] = (comm, ticks, sw)
        except (FileNotFoundError, ProcessLookupError):
            pass
    return out

def mem(pid):
    rss = pss = 0
    try:
        for l in open(f"/proc/{pid}/smaps_rollup"):
            if l.startswith("Rss:"): rss = int(l.split()[1])
            elif l.startswith("Pss:"): pss = int(l.split()[1])
    except OSError:
        pass
    return rss, pss

win = float(sys.argv[1])
pids = [int(p) for p in sys.argv[2:]]
a = {p: threads(p) for p in pids}
sys_a = [int(x) for x in open("/proc/stat").readline().split()[1:]]
t0 = time.time()
time.sleep(win)
dt = time.time() - t0
b = {p: threads(p) for p in pids}
sys_b = [int(x) for x in open("/proc/stat").readline().split()[1:]]
print(f"window {dt:.1f} s, clk_tck {os.sysconf('SC_CLK_TCK')}, governor {open('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor').read().strip()}")
d = [y - x for x, y in zip(sys_a, sys_b)]
tot = sum(d)
print(f"system: busy {100*(tot-d[3]-d[4])/tot:.2f}% (user {d[0]} nice {d[1]} sys {d[2]} idle {d[3]} iowait {d[4]} irq {d[5]} softirq {d[6]} ticks)")
print(f"{'pid':>6} {'tid':>6} {'comm':<16} {'ticks':>6} {'cpu%':>6} {'sw/s':>6}")
for p in pids:
    try: name = open(f"/proc/{p}/comm").read().strip()
    except OSError: name = "?"
    rss, pss = mem(p)
    tt = ts = 0
    rows = []
    for t, (comm, tk, sw) in b[p].items():
        tk0, sw0 = a[p].get(t, (comm, tk, sw))[1:]
        rows.append((t, comm, tk - tk0, (sw - sw0) / dt))
        tt += tk - tk0; ts += (sw - sw0) / dt
    for t, comm, tk, sws in sorted(rows, key=lambda r: int(r[0])):
        print(f"{p:>6} {t:>6} {comm:<16} {tk:>6} {100*tk/100/dt:>6.2f} {sws:>6.1f}")
    print(f"{p:>6} {'TOTAL':>6} {name:<16} {tt:>6} {100*tt/100/dt:>6.2f} {ts:>6.1f}   threads {len(b[p])}  RSS {rss/1024:.1f} MB  PSS {pss/1024:.1f} MB")
