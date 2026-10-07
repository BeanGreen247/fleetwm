# The 2026-10-07 test rig (so another machine can rebuild it)

- `Dockerfile`: Debian 13 with the installer's build dependencies (image `fleetwm-build-trixie`); the later RUN lines add foot, wtype, wlrctl, grim, PipeWire and WirePlumber (tag the same image `fleetwm-train-trixie`). `wlr-protocols` is not in Debian 13 (the virtual-pointer XML is vendored in `scripts/`).
- `mk.sh SRC BUILD`: the installer's meson options and flags with `-march=goldmont-plus` (the laptop's N4020) and no PGO; `STRIP=false EXTRA="-g -fno-omit-frame-pointer"` gives a profiling build. Sync the source with `rsync --exclude '/build*'` (a plain `build*` would drop `scripts/build-pgo*.sh`).
- `drun.sh`: runs a container with `SCRATCH` mounted at `/w` and your uid (mounts `/etc/passwd` so dbus works).
- `trainrun.sh SRC SECONDS`, `covsum.py BUILD`, `sweep.sh N...`: run the PGO training in the container and count functions/arcs executed from the `.gcda` files (`gcov-dump`; `gcov` itself does not work on the PGO build).
- The compositor does NOT build on the dev PC (wlroots 0.19 there): compile it in this container before committing compositor changes.
- Laptop side (not in git, recreate by `scp`): `~/perf-test/{A,B,C,E}/` binaries, `nest.sh` (throwaway nested compositor, pidfile kills), `glassbench.sh`, `glassgpu.sh`, `barbench.sh`, `/tmp/gb/pgo-pointer` (build from `scripts/pgo-pointer.c`), `/tmp/membench_sse`, `/tmp/procstat.py`. All copies are in `perflog/2026-10-07/`.
