#!/bin/bash
# Runs a command in a throwaway Debian 13 container with SCRATCH mounted at /w (the build rig of the 2026-10-07 round).
#   docker build -t fleetwm-build-trixie perflog/2026-10-07/rig     (once; the Dockerfile is also the "train" image: it has foot, wtype,
#   wlrctl, grim, pipewire, wireplumber; tag it fleetwm-train-trixie as well)
#   SCRATCH=~/fleetwm-rig ./drun.sh fleetwm-build-trixie /w/mk.sh /w/src /w/build     (mk.sh: the installer's flags, -march=goldmont-plus, no PGO)
#   SCRATCH=~/fleetwm-rig ./drun.sh fleetwm-train-trixie bash /w/sweep.sh 85          (training + coverage; needs trainrun.sh and covsum.py in SCRATCH)
S="${SCRATCH:?set SCRATCH to a directory that holds src/, the scripts and the build dirs}"
exec docker run --rm -u "$(id -u):$(id -g)" -v /etc/passwd:/etc/passwd:ro -v /etc/group:/etc/group:ro -v "$S":/w -e HOME=/tmp "$@"
