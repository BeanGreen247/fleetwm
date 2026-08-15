# ADR 0005: System stats collection strategy

## Status
Accepted

## Context
The bar shows CPU%, GPU%, disk%, and volume%. These need to be collected
without busy-polling (power-efficiency goal) and without adding heavy
dependencies for a handful of numbers.

## Decision
- **CPU%**: read `/proc/stat`'s aggregate line, compute the delta between
  two samples. Piggybacks on the same 1Hz GLib timeout that already drives
  the clock -- no additional wakeup source.
- **GPU%**: best-effort, vendor-priority order: `/sys/class/drm/card*/
  device/gpu_busy_percent` (amdgpu) first, then Intel's `gt_busy_percent`
  sysfs node, then `nvidia-smi --query-gpu=utilization.gpu` via subprocess
  if the binary exists on PATH (at a reduced 3s interval, since spawning a
  process is comparatively expensive versus a sysfs read). Shows "N/A" if
  none are available rather than erroring.
- **Disk%**: `statvfs("/")`, polled every 5-10s -- doesn't need 1Hz
  freshness.
- **Volume%**: PipeWire is the default audio server on both target
  distros. Subscribe to the default sink's volume via `libpipewire-0.3-dev`
  registry/proxy events -- event-driven, zero cost at idle, instant update
  on change. Falls back to shelling out to `wpctl get-volume
  @DEFAULT_AUDIO_SINK@` only if PipeWire dev headers are unavailable at
  build time.

## Consequences
- No stat in the bar is ever busy-polled; the only recurring wakeup is the
  single 1Hz GLib timeout already required for the clock display, plus the
  independent 5-10s disk timeout and PipeWire's own event delivery (which
  costs nothing when idle).
- GPU vendor detection happens once at bar startup (probing which sysfs
  path exists / whether `nvidia-smi` is on PATH), not on every sample.
