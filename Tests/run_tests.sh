#!/bin/bash
# Local runner: regression suite, ASan+UBSan pass, and the ThreadSanitizer ring check (Linux/macOS, g++ or clang++).
set -e
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
OUT=$(mktemp -d)
FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Wno-mismatched-new-delete"
echo "== regression + stress suite =="
$CXX $FLAGS Tests/test_main.cpp -o $OUT/tests && $OUT/tests
echo "== suite under AddressSanitizer + UBSan =="
$CXX $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined Tests/test_main.cpp -o $OUT/asan && $OUT/asan | tail -1
echo "== analyzer ring: new design under ThreadSanitizer (expect no race) =="
$CXX -std=c++17 -O1 -g -fsanitize=thread -pthread Tests/ring_race.cpp -o $OUT/ring_new && $OUT/ring_new
echo "== analyzer ring: ORIGINAL design under ThreadSanitizer (expect data-race reports) =="
$CXX -std=c++17 -O1 -g -fsanitize=thread -pthread -DLEGACY Tests/ring_race.cpp -o $OUT/ring_old && ( $OUT/ring_old 2>&1 | grep -c "data race" | sed 's/^/original design: data-race reports = /' ) || true
