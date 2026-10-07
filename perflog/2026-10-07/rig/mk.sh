#!/bin/bash
# usage: mk.sh SRC BUILD [extra flags]   (run inside container)
set -e
SRC=$1; B=$2; shift 2
FL="-w -march=goldmont-plus -ffunction-sections -fdata-sections -fno-semantic-interposition -fno-plt -fno-math-errno -fno-trapping-math -fomit-frame-pointer -fno-stack-protector -fno-stack-clash-protection -fcf-protection=none -fno-pie -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DG_DISABLE_ASSERT $EXTRA"
LF='-no-pie -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,--sort-common -Wl,-z,lazy -Wl,-z,norelro -Wl,-z,noseparate-code'
meson setup $B $SRC --prefix=/usr/local --buildtype=release -Db_ndebug=true -Db_lto=true -Dtests=false -Dunity=on -Dwarning_level=0 -Ddefault_library=static -Dc_args="$FL" -Dcpp_args="$FL" -Dc_link_args="$LF" -Dcpp_link_args="$LF" -Dstrip=${STRIP:-true} >/dev/null
ninja -C $B 2>&1 | tail -3
