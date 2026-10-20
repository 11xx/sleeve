#!/usr/bin/env bash
# Runs the benchmark natively, writing into RESULTS_DIR the C and Haskell report (native.json), the
# report of C alone built by the system's C compiler (native-c.json), and the machine's load before
# and after each to load.txt.
#   SEEDS  covers per size, 1000 by default
#   CC     the C compiler for C alone, cc by default
set -euo pipefail
out=$(realpath -m "${1:?usage: run-native.sh RESULTS_DIR}")
here=$(cd "$(dirname "$0")" && pwd)
seeds=${SEEDS:-1000}
cd "$here"
mkdir -p "$out"
cabal build --offline exe:sleeve-bench >/dev/null
bench=$(cabal list-bin --offline exe:sleeve-bench)
c_only=$(mktemp)
trap 'rm -f "$c_only"' EXIT
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -Werror -o "$c_only" c/bench.c

run() {
  local label=$1
  shift
  echo "$label before: $(cat /proc/loadavg)" >> "$out/load.txt"
  "$@" > "$out/$label.json"
  echo "$label after: $(cat /proc/loadavg)" >> "$out/load.txt"
}
"$bench" check > "$out/check-native.txt"
run native "$bench" bench --seeds "$seeds" --label native
run native-c "$c_only" --seeds "$seeds" --label native-c
