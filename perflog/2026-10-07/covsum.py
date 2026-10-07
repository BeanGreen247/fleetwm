#!/usr/bin/env python3
"""covsum.py BUILD_DIR : functions and arcs executed per program, from the profile (.gcda) files."""
import subprocess, sys, os, re, collections
build = sys.argv[1]
res = collections.OrderedDict()
for root, _, files in sorted(os.walk(build)):
    for f in sorted(files):
        if not f.endswith('.gcda'): continue
        p = os.path.join(root, f)
        out = subprocess.run(['gcov-dump', '-l', p], capture_output=True, text=True).stdout.splitlines()
        name = os.path.basename(os.path.dirname(p)).replace('.p', '')
        r = res.setdefault(name, [0, 0, 0, 0])  # funcs, funcs hit, arcs, arcs hit
        i = 0
        while i < len(out):
            l = out[i]
            if 'FUNCTION ident' in l:
                r[0] += 1
            m = re.search(r'COUNTERS arcs (\d+) counts( \(all zero\))?', l)
            if m:
                n = int(m.group(1)); r[2] += n
                if not m.group(2):
                    hit = 0
                    j = i + 1
                    # value lines look like "   0: 12 0 5 ..." until the next COUNTERS line
                    while j < len(out) and 'COUNTERS' not in out[j] and 'FUNCTION' not in out[j]:
                        vals = out[j].split(':', 2)[-1].split() if ':' in out[j] else []
                        hit += sum(1 for v in vals if v.isdigit() and int(v) > 0)
                        j += 1
                    r[3] += hit
                    if hit: r[1] += 1
            i += 1
tf = sum(v[0] for v in res.values()); th = sum(v[1] for v in res.values())
ta = sum(v[2] for v in res.values()); tah = sum(v[3] for v in res.values())
print(f"{'program':32s} {'funcs hit':>14s} {'arcs hit':>16s}")
for k, v in res.items():
    print(f"{k:32s} {v[1]:6d}/{v[0]:<6d}{100*v[1]/max(1,v[0]):5.1f}% {v[3]:7d}/{v[2]:<7d}{100*v[3]/max(1,v[2]):5.1f}%")
print(f"{'TOTAL':32s} {th:6d}/{tf:<6d}{100*th/max(1,tf):5.1f}% {tah:7d}/{ta:<7d}{100*tah/max(1,ta):5.1f}%")
