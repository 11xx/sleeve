#!/usr/bin/env bash
# Runs the benchmark on an Android device: the C and Haskell report under WAMR, compiled ahead of
# time (android-wamr-aot*.json), and C alone built natively by the NDK (android-c*.json), each
# unpinned and pinned to the big cores, with the device's power and thermal state before and after
# each run in device.jsonl. Everything goes to /data/local/tmp/sleeve-bench; nothing is installed.
# Needs:
#   ANDROID_SERIAL  the device, as adb names it; every adb command names it
#   ADB             the adb command, adb by default
#   IWASM           WAMR's iwasm built for the device
#   WAMRC           WAMR's ahead-of-time compiler
#   WAMRC_CPU       the device's baseline core for wamrc, cortex-a55 by default
#   NDK             an Android NDK
#   BIG             the big cores' CPU mask for taskset, c0 by default
#   SEEDS           covers per size, 1000 by default
#   LOCK            a file to hold with flock around each use of the device, when given
set -euo pipefail
out=$(realpath -m "${1:?usage: run-android.sh RESULTS_DIR}")
here=$(cd "$(dirname "$0")" && pwd)
seeds=${SEEDS:-1000}
big=${BIG:-c0}
: "${ANDROID_SERIAL:?}" "${IWASM:?}" "${WAMRC:?}" "${NDK:?}"
mkdir -p "$out"
cd "$here"
# shellcheck disable=SC1091
source "${GHC_WASM_ENV:-$HOME/.ghc-wasm/env}"

"$here/../scripts/drop-stale-objects" dist-newstyle/wasm >&2
wasm32-wasi-cabal --builddir=dist-newstyle/wasm build exe:sleeve-bench >/dev/null
module=$(wasm32-wasi-cabal --builddir=dist-newstyle/wasm list-bin exe:sleeve-bench 2>/dev/null)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
"$WAMRC" --target=aarch64v8 --cpu="${WAMRC_CPU:-cortex-a55}" -o "$build/sleeve-bench.aot" "$module" >/dev/null
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang" -std=c99 -O2 -Wall -Wextra \
  -Werror -o "$build/sleeve-bench-c" c/bench.c

dir=/data/local/tmp/sleeve-bench
device() {
  "${ADB:-adb}" -s "$ANDROID_SERIAL" "$@" </dev/null
}
locked() {
  if [ -n "${LOCK:-}" ]; then flock "$LOCK" "$@"; else "$@"; fi
}
# the device's power and thermal state as one JSON line, labelled
state() {
  local saver level powered thermal
  saver=$(device shell settings get global low_power | tr -d '\r')
  level=$(device shell dumpsys battery | sed -n 's/^ *level: //p' | tr -d '\r')
  powered=$(device shell dumpsys battery | grep -c -E '(AC|USB|Wireless) powered: true' || true)
  thermal=$(device shell dumpsys thermalservice | sed -n 's/^Thermal Status: //p' | tr -d '\r')
  printf '{"label": "%s", "at": "%s", "battery_saver": %s, "battery": %s, "charging": %s, "thermal_status": %s}\n' \
    "$1" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$([ "$saver" = 1 ] && echo true || echo false)" "${level:-null}" \
    "$([ "$powered" -gt 0 ] && echo true || echo false)" "${thermal:-null}"
}
# a run: the device's state before and after, refusing to measure under Battery Saver
run() {
  local label=$1
  shift
  echo "android: $label" >&2
  if [ "$(device shell settings get global low_power | tr -d '\r')" = 1 ]; then
    echo "Battery Saver is on; turn it off to measure the device" >&2
    exit 1
  fi
  state "$label before" >> "$out/device.jsonl"
  device shell "cd $dir && $*" > "$out/$label.json"
  state "$label after" >> "$out/device.jsonl"
}
install() {
  device shell "mkdir -p $dir"
  device push "$IWASM" "$dir/iwasm" >/dev/null
  device push "$build/sleeve-bench.aot" "$dir/sleeve-bench.aot" >/dev/null
  device push "$build/sleeve-bench-c" "$dir/sleeve-bench-c" >/dev/null
  device shell "chmod 755 $dir/iwasm $dir/sleeve-bench-c"
}

export -f device state run install
export dir out build IWASM ANDROID_SERIAL
[ -z "${ADB:-}" ] || export ADB
locked bash -c install
locked bash -c "device shell 'cd $dir && ./iwasm sleeve-bench.aot check'" > "$out/check-android-wamr-aot.txt"
locked bash -c "run android-wamr-aot-big taskset $big ./iwasm sleeve-bench.aot bench --seeds $seeds --label android-wamr-aot-big"
locked bash -c "run android-c-big taskset $big ./sleeve-bench-c --seeds $seeds --label android-c-big"
locked bash -c "run android-wamr-aot ./iwasm sleeve-bench.aot bench --seeds $seeds --label android-wamr-aot"
locked bash -c "run android-c ./sleeve-bench-c --seeds $seeds --label android-c"
