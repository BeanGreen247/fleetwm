#!/usr/bin/env bash
# Checks src/common/fleettoml.* against toml++ (a test tool only: toml++ is NOT a dependency of Fleetwm). Needs toml++ headers, for example
#   curl -sL https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0 | tar xz     (gives tomlplusplus-3.4.0/include)
# usage: run.sh TOMLPP_INCLUDE_DIR [seeds...]
# For each seed it builds ~20,000 documents (a hand-written list, real config files, test snippets and random single-character mutations),
# parses them with both libraries and reports every document where one accepts and the other rejects, or the values differ (dates are
# kept as text by fleettoml, so those are skipped); then the round trip parse(print(parse(x))) and 200,000 random strings under ASan/UBSan.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd); INC=${1:?toml++ include dir}; shift
SEEDS=${*:-"1 2 3"}; W=$(mktemp -d); cd "$W"; cp "$HERE"/* .
sed -e 's#<toml++/toml.h>#"fleettoml.hpp"#' a_tomlpp.cpp > b_mini.cpp
sed -e 's#<toml++/toml.h>#"fleettoml.hpp"#' p_tomlpp.cpp > p_mini.cpp
g++ -std=c++20 -O1 -DNDEBUG -I"$INC" -o a_tomlpp a_tomlpp.cpp
g++ -std=c++20 -O1 -I"$ROOT/src/common" -o b_mini b_mini.cpp "$ROOT/src/common/fleettoml.cpp"
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$ROOT/src/common" -o rt rt.cpp "$ROOT/src/common/fleettoml.cpp"
for seed in $SEEDS; do
  python3 gen.py "$seed" 40 > corpus.txt 2>/dev/null
  ./a_tomlpp < corpus.txt > out_a_tomlpp.txt; ./b_mini < corpus.txt > out_b_mini.txt; cp out_a_tomlpp.txt out_a_tomlpp33.txt
  python3 - <<'PY'
c=open('corpus.txt',encoding='utf-8',errors='replace').read().split('\x01\x01\x01\n')
A=open('out_a_tomlpp.txt',encoding='utf-8',errors='replace').read().split('\n'); B=open('out_b_mini.txt',encoding='utf-8',errors='replace').read().split('\n')
bad=0
for i in range(len(c)):
    if A[i]==B[i] or ('OTHER' in A[i] and B[i]!='ERROR' and A[i]!='ERROR'): continue
    bad+=1
    if bad<5: print(repr(c[i][:100]),'| toml++:',A[i][:60],'| fleettoml:',B[i][:60])
print('cases',len(c),'differences',bad)
PY
  ./rt < corpus.txt | tail -1
done
echo "benchmark: g++ -O3 -DNDEBUG -I$INC -o bench_a bench_a.cpp; sed 's#<toml++/toml.h>#\"fleettoml.hpp\"#' bench_a.cpp > bench_b.cpp; g++ -O3 -I$ROOT/src/common bench_b.cpp $ROOT/src/common/fleettoml.cpp -o bench_b; ./bench_a cfg_small.toml 20000; ./bench_b cfg_small.toml 20000"
