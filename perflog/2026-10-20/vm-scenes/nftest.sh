#!/bin/bash
# usage: nftest.sh URI   -- mounts, exercises, unmounts
NF=~/fleetwm/build-nf/src/netfs/fleetwm-netfs
URI=$1; MP=$(mktemp -d /tmp/nfmp.XXXXXX)
echo "== $URI"
echo dev | $NF --known-hosts /tmp/nf-known "$URI" "$MP"; rc=$?
[ $rc = 0 ] || { echo "mount failed rc=$rc"; rmdir $MP; exit 1; }
set -x
ls -la "$MP" | head -5
echo "hello netfs" > "$MP/hello.txt"; cat "$MP/hello.txt"
mkdir "$MP/sub dir"; echo x > "$MP/sub dir/in side.txt"; ls "$MP/sub dir"
mv "$MP/hello.txt" "$MP/hello2.txt"; ls "$MP"
head -c 20000000 /dev/urandom > /tmp/nf-big.bin
cp /tmp/nf-big.bin "$MP/big.bin"; sync
cmp /tmp/nf-big.bin "$MP/big.bin" && echo BIG-OK
dd if="$MP/big.bin" of=/dev/null bs=1M skip=5 count=2 2>&1 | tail -1
echo more >> "$MP/hello2.txt"; cat "$MP/hello2.txt"
rm "$MP/hello2.txt" "$MP/big.bin" "$MP/sub dir/in side.txt"; rmdir "$MP/sub dir"
ls -A "$MP" | head
set +x
fusermount3 -u "$MP" && rmdir "$MP" && echo UNMOUNTED
