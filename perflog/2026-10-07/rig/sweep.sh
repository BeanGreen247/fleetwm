#!/bin/bash
for t in "$@"; do
  /w/trainrun.sh /w/srcT $t | tail -1
  python3 /w/covsum.py /w/srcT/build-pgo > /w/cov_t$t.txt
  echo "== $t s: $(tail -1 /w/cov_t$t.txt)"; grep -E "fleetwm-bar|^fleetwm  " /w/cov_t$t.txt
  grep WARNING /w/srcT/train.out | head -3
done
