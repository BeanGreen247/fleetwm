#!/usr/bin/env python3
# coldstart.py REPS : time from exec to the bar's first wl_surface.commit (WAYLAND_DEBUG client timestamps), warm page
# cache against pages of the bar's own files evicted with posix_fadvise(DONTNEED) first (an upper bound for any prewarm
# of the bar's files: shared libraries that other running processes map stay resident). Interleaved. Needs a nested compositor.
import os, re, subprocess, sys, time, statistics
reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
bar = '/home/dev/perf-test/gen/bin/fleetwm-bar'
env = dict(os.environ, WAYLAND_DEBUG='client')
def files_of_running():
    p = subprocess.Popen([bar], env=dict(os.environ), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    fs = set()
    for l in open('/proc/%d/maps' % p.pid):
        parts = l.split()
        if len(parts) >= 6 and parts[5].startswith('/'): fs.add(parts[5])
    p.terminate(); p.wait()
    return sorted(fs)
files = files_of_running()
def evict():
    n = 0
    for f in files:
        try:
            fd = os.open(f, os.O_RDONLY); os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED); os.close(fd); n += 1
        except OSError: pass
    return n
def once():
    t0 = time.monotonic() * 1000
    p = subprocess.Popen([bar], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    first = None
    for line in p.stderr:
        m = re.match(r'\[\s*(\d+\.\d+)\].*wl_surface#\d+\.commit\(\)', line)
        if m: first = float(m.group(1)) - t0; break  # the timestamps and t0 are on different clock bases; only differences between runs mean something
    p.terminate(); p.wait()
    return first
once()
res = {'warm': [], 'evicted': []}
for i in range(reps):
    res['warm'].append(once())
    evict(); time.sleep(0.2)
    res['evicted'].append(once())
    time.sleep(0.3)
for k, v in res.items():
    v = [x for x in v if x is not None]
    print(k, 'ms to first commit:', ' '.join('%.0f' % x for x in v), ' median %.0f' % statistics.median(v))
wm=statistics.median([x for x in res['warm'] if x is not None]); em=statistics.median([x for x in res['evicted'] if x is not None])
print('evicted minus warm (median): %.1f ms' % (em - wm))
print('files', len(files))
