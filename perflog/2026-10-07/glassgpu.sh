#!/bin/bash
# usage: glassgpu.sh VARIANT ROUNDS -- GPU render busy % (intel_gpu_top) and compositor+bar CPU with glass on and off, same drag workload
V=$1; R=${2:-3}
PT=/tmp/gb/pgo-pointer
for r in $(seq 1 $R); do
  for GL in true false; do
    /home/dev/perf-test/nest.sh $V >/dev/null
    HM=/tmp/ptest-home-$V; RT=/tmp/ptest-run-$V; CP=$(cat $RT/pid)
    sed -i "s/^glass_effects.*/glass_effects = $GL/" $HM/.config/fleetwm/theme.toml; grep -q glass_effects $HM/.config/fleetwm/theme.toml || echo "glass_effects = $GL" >> $HM/.config/fleetwm/theme.toml
    sleep 1.5
    export XDG_RUNTIME_DIR=$RT WAYLAND_DISPLAY=wayland-0
    /home/dev/perf-test/E/apps/settings/fleetwm-settings --page theme >/dev/null 2>&1 & SP=$!
    sleep 3
    echo dev | sudo -S -p "" timeout 14 intel_gpu_top -J -s 500 > /tmp/gt_$GL.json 2>/dev/null &
    sleep 1
    for i in $(seq 1 8); do $PT drag 645 70 445 250 8 drag 445 250 645 70 8 drag 1049 300 1149 300 8 drag 1149 300 1049 300 8; done
    sleep 1
    python3 - $GL <<'PY'
import json,sys,re
t=open(f"/tmp/gt_{sys.argv[1]}.json").read()
objs=re.findall(r'\{\s*"period".*?\n\}', t, re.S)
vals=[]
for o in objs:
    try:
        j=json.loads(o); vals.append((j["engines"]["Render/3D"]["busy"], j["frequency"]["actual"], j["rc6"]["value"]))
    except Exception: pass
vals=vals[1:-1] or vals
if vals: print(f"gpu glass={sys.argv[1]}: render busy mean {sum(v[0] for v in vals)/len(vals):.1f}% max {max(v[0] for v in vals):.1f}%, freq {sum(v[1] for v in vals)/len(vals):.0f} MHz, rc6 {sum(v[2] for v in vals)/len(vals):.1f}% over {len(vals)} samples")
else: print("no gpu samples", len(objs))
PY
    kill $SP $CP 2>/dev/null; sleep 2
  done
done
