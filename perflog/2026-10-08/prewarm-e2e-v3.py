#!/usr/bin/env python3
# e2e.py REPS: compositor exec -> first buffer of the bar (strace: first ftruncate by the bar's pid), helper files evicted before each start.
#  off   FLEETWM_PREWARM=off (no manifests used)
#  on    manifests present (made by a first run), the compositor prewarms the helpers while it starts
import os, re, shutil, statistics, subprocess, sys, tempfile, time, signal
reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
B = '/home/dev/perf-test/' + (sys.argv[2] if len(sys.argv) > 2 else 'gen') + '/bin'
cache = tempfile.mkdtemp(prefix='e2e-cache-')
home = '/tmp/ptest-home-e2e'; rt = '/tmp/ptest-run-e2e'
def setup():
    shutil.rmtree(rt, ignore_errors=True); shutil.rmtree(home, ignore_errors=True)
    os.makedirs(rt, mode=0o700); os.makedirs(home + '/.config')
    shutil.copytree('/home/dev/.config/fleetwm', home + '/.config/fleetwm')
env0 = dict(os.environ, XDG_RUNTIME_DIR=rt, HOME=home, XDG_CACHE_HOME=cache, PATH=B + ':' + os.environ['PATH'], FLEETWM_PREWARM_BINDIR=B,
            WLR_BACKENDS='headless', WLR_RENDERER='pixman', WLR_HEADLESS_OUTPUTS='1')
def files_of(prog):
    return None
def evict(paths):
    for f in paths:
        try:
            fd = os.open(f, os.O_RDONLY); os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED); os.close(fd)
        except OSError: pass
def run(extra, seconds, trace=False):
    setup()
    env = dict(env0, **extra)
    log = open(rt + '/ex.st', 'w')
    cmd = ['strace', '-f', '-tt', '-e', 'trace=execve,ftruncate', '-o', rt + '/ex.st', B + '/fleetwm'] if trace else [B + '/fleetwm']
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    time.sleep(seconds)
    # collect children's pids from the process group, mapped files
    files = set()
    pids = []
    out = subprocess.run(['pgrep', '-g', str(p.pid)], capture_output=True, text=True).stdout.split()
    for pid in out:
        try:
            for l in open('/proc/%s/maps' % pid):
                parts = l.split()
                if len(parts) >= 6 and parts[5].startswith('/'): files.add(parts[5])
            pids.append(int(pid))
        except OSError: pass
    os.killpg(p.pid, signal.SIGTERM); time.sleep(0.5)
    try: os.killpg(p.pid, signal.SIGKILL)
    except OSError: pass
    p.wait()
    return files
def measure(extra):
    files = None
    if extra.get('evict'):
        pass
def delta():
    t_comp = t_bar = t_ft = None
    for l in open(rt + '/ex.st'):
        m = re.match(r'(\d+)\s+(\d+):(\d+):(\d+\.\d+)\s+(.*)', l)
        if not m: continue
        t = int(m.group(2)) * 3600 + int(m.group(3)) * 60 + float(m.group(4))
        rest = m.group(5)
        if t_comp is None and re.match(r'execve\("[^"]*/fleetwm"', rest): t_comp = t; continue
        if re.match(r'execve\("[^"]*/fleetwm-bar"', rest) and 'ENOENT' not in rest and t_bar is None:
            t_bar = t; bar_pid = m.group(1); continue
        if t_bar is not None and t_ft is None and m.group(1) == bar_pid and rest.startswith('ftruncate'):
            t_ft = t
    return (None if None in (t_comp, t_bar, t_ft) else ((t_bar - t_comp) * 1000, (t_ft - t_comp) * 1000))
# learn: first run makes manifests (each program records itself at +4 s)
files = run({}, 7)
print('manifests:', sorted(os.listdir(cache + '/fleetwm/prewarm')))
res = {'off': [], 'on': []}
for i in range(reps):
    for mode in ('off', 'on'):
        (None if os.environ.get('NOEVICT') else evict(files)); time.sleep(0.3)
        extra = {'FLEETWM_PREWARM': 'off'} if mode == 'off' else {}
        run(extra, 3, trace=True)
        d = delta()
        if d: res[mode].append(d)
for k, v in res.items():
    if v:
        print('%-4s bar exec at +%.0f ms (median), first buffer at +%.0f ms  [%s]' % (k, statistics.median(x[0] for x in v), statistics.median(x[1] for x in v),
              ' '.join('%.0f' % x[1] for x in v)))
shutil.rmtree(cache, ignore_errors=True)
