#!/bin/bash
# usage: trainrun.sh SRC_DIR SECONDS [script]   (inside the container)
SRC=$1; SEC=$2; SCR=${3:-$SRC/scripts/pgo-train-session.sh}
BUILD=$SRC/build-pgo
find $BUILD -name '*.gcda' -delete
RT=$(mktemp -d /tmp/fleetwm-pgo-train.XXXXXX); chmod 700 $RT
start=$SECONDS
dbus-run-session -- env WLR_BACKENDS=headless WLR_RENDERER=pixman XDG_RUNTIME_DIR=$RT HOME=$RT LANG=C.UTF-8 LC_ALL=C.UTF-8 \
  bash $SCR $RT $SEC $BUILD > $SRC/train.out 2>&1
echo "training wall time $((SECONDS-start)) s (asked $SEC)"
rm -rf $RT
