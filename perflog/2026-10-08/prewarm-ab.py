#!/usr/bin/env python3
# prewarm-ab.py PROGRAM REPS : time from exec to the first wl_surface.commit of a Fleetwm client, three conditions interleaved
#   warm    page cache as it is, prewarm off
#   evicted the program's own files dropped from the page cache (posix_fadvise DONTNEED), prewarm off
#   replay  the same eviction, prewarm on with the manifest the program recorded itself
# Needs a nested compositor (WAYLAND_DISPLAY, XDG_RUNTIME_DIR set). The timestamps and t0 are on different clock bases (WAYLAND_DEBUG
# prints its own), so only differences between conditions mean something; they are printed against the warm median.
import os, re, shutil, statistics, subprocess, sys, tempfile, time
prog = sys.argv[1] if len(sys.argv) > 1 else 'fleetwm-bar'
reps = int(sys.argv[2]) if len(sys.argv) > 2 else 10
exe = '/home/dev/perf-test/gen/bin/' + prog
cache = tempfile.mkdtemp(prefix='prewarm-cache-')
base = dict(os.environ, XDG_CACHE_HOME=cache)
def run_for(seconds, extra):
    p = subprocess.Popen([exe], env=dict(base, **extra), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(seconds)
    files = set()
    for l in open('/proc/%d/maps' % p.pid):
        parts = l.split()
        if len(parts) >= 6 and parts[5].startswith('/'): files.add(parts[5])
    p.terminate(); p.wait()
    return sorted(files)
files = run_for(6, {'FLEETWM_PREWARM': 'record'})   # makes the manifest (recorded 4 s after start)
man = [f for f in os.listdir(cache + '/fleetwm/prewarm')] if os.path.isdir(cache + '/fleetwm/prewarm') else []
print('files', len(files), 'total bytes', sum(os.path.getsize(f) for f in files if os.path.exists(f)))
print('manifest:', man, 'ranges:', sum(1 for _ in open(cache + '/fleetwm/prewarm/' + man[0])) if man else 0,
      'bytes:', sum(int(l.split('\t')[2]) for l in open(cache + '/fleetwm/prewarm/' + man[0])) if man else 0)
def evict():
    for f in files:
        try:
            fd = os.open(f, os.O_RDONLY); os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED); os.close(fd)
        except OSError: pass
def once(extra):
    t0 = time.monotonic() * 1000
    p = subprocess.Popen([exe], env=dict(base, WAYLAND_DEBUG='client', **extra), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    first = None
    for line in p.stderr:
        m = re.match(r'\[\s*(\d+\.\d+)\].*wl_surface#\d+\.commit\(\)', line)
        if m: first = float(m.group(1)) - t0; break
    p.terminate(); p.wait()
    return first
once({'FLEETWM_PREWARM': 'off'})
def parent_replay():
    # what a parent (the compositor before it forks the bar) would do: read the manifest and ask for the ranges
    mf = cache + '/fleetwm/prewarm/' + man[0]
    cur, fd = None, -1
    for l in open(mf):
        path, off, ln = l.rstrip('\n').split('\t')
        if path != cur:
            if fd >= 0: os.close(fd)
            try: fd = os.open(path, os.O_RDONLY)
            except OSError: fd = -1
            cur = path
        if fd >= 0: os.posix_fadvise(fd, int(off), int(ln), os.POSIX_FADV_WILLNEED)
    if fd >= 0: os.close(fd)
def read_all():
    for f in files:
        try:
            with open(f, 'rb') as fh:
                while fh.read(1 << 20): pass
        except OSError: pass
def willneed_whole():
    tot = 0
    for f in files:
        try:
            fd = os.open(f, os.O_RDONLY); n = os.fstat(fd).st_size; tot += n; os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_WILLNEED); os.close(fd)
        except OSError: pass
    return tot
def willneed_heads():
    for f in files:
        try:
            fd = os.open(f, os.O_RDONLY); os.posix_fadvise(fd, 0, 262144, os.POSIX_FADV_WILLNEED); os.close(fd)
        except OSError: pass
res = {'whole': [], 'manifest+heads': [], 'full': [], 'warm': [], 'warm_idle': [], 'evicted': [], 'replay': [], 'parent0': [], 'parent500': []}
for i in range(reps):
    res['warm'].append(once({'FLEETWM_PREWARM': 'off'}))
    evict(); time.sleep(0.2)
    willneed_whole(); time.sleep(0.15)
    res['whole'].append(once({'FLEETWM_PREWARM': 'off'}))
    evict(); time.sleep(0.2)
    parent_replay(); willneed_heads(); time.sleep(0.15)
    res['manifest+heads'].append(once({'FLEETWM_PREWARM': 'off'}))
    evict(); time.sleep(0.2); read_all(); time.sleep(0.2)
    res['full'].append(once({'FLEETWM_PREWARM': 'off'}))  # every byte of those files read back in before the start: a perfect prewarm
    time.sleep(0.2)
    res['warm_idle'].append(once({'FLEETWM_PREWARM': 'off'}))  # same pause as after an eviction, nothing evicted
    evict(); time.sleep(0.2)
    res['evicted'].append(once({'FLEETWM_PREWARM': 'off'}))
    evict(); time.sleep(0.2)
    res['replay'].append(once({}))   # default mode: replays the manifest
    time.sleep(0.2)
    evict(); time.sleep(0.2)
    parent_replay()
    res['parent0'].append(once({'FLEETWM_PREWARM': 'off'}))
    evict(); time.sleep(0.2)
    parent_replay(); time.sleep(0.5)
    res['parent500'].append(once({'FLEETWM_PREWARM': 'off'}))
    time.sleep(0.2)
w = statistics.median(res['warm'])
for k, v in res.items():
    print('%-8s %s   median %+.1f ms vs warm' % (k, ' '.join('%+.0f' % (x - w) for x in v), statistics.median(v) - w))
shutil.rmtree(cache, ignore_errors=True)
