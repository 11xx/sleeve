#!/usr/bin/env bash
# Runs the benchmark as WebAssembly in a headless Chromium-family browser, writing the page's report
# to RESULTS_DIR/web-LABEL.json and the machine's load before and after to RESULTS_DIR/load.txt.
# The browser gets a fresh profile and is stopped by it afterwards; nothing opens on screen.
#   CHROMIUM  the browser command, chromium by default
#   LAUNCH    a command that runs the browser command given as its arguments
#   SEEDS     covers per size, 1000 by default
#   WAIT      seconds to wait for the report, 3600 by default
set -euo pipefail
out=$(realpath -m "${1:?usage: run-browser.sh RESULTS_DIR [LABEL]}")
label=${2:-chromium}
here=$(cd "$(dirname "$0")" && pwd)
seeds=${SEEDS:-1000}
port=${PORT:-8419}
# shellcheck disable=SC1091
source "${GHC_WASM_ENV:-$HOME/.ghc-wasm/env}"
cd "$here"
wasm32-wasi-cabal --builddir=dist-newstyle/wasm build exe:sleeve-bench >/dev/null
module=$(wasm32-wasi-cabal --builddir=dist-newstyle/wasm list-bin exe:sleeve-bench 2>/dev/null)

# the page and the browser's profile; the profile's path names the browser's processes
scratch=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/sleeve-browser.XXXXXX")
cleanup() {
  local tries=0
  pkill -f -- "--user-data-dir=$scratch/profile" 2>/dev/null || true
  while pgrep -f -- "--user-data-dir=$scratch/profile" >/dev/null && [ "$tries" -lt 20 ]; do
    sleep 0.5
    tries=$((tries + 1))
  done
  kill "${server:-}" 2>/dev/null || true
  rm -rf "$scratch" || true
}
trap cleanup EXIT
mkdir -p "$out" "$scratch/site" "$scratch/profile"
cp web/index.html web/main.mjs web/worker.mjs "$scratch/site/"
cp "$module" "$scratch/site/sleeve-bench.wasm"
rm -f "$out/web-$label.json"
python3 -I web/serve.py "$scratch/site" "$out" --port "$port" >"$scratch/serve.log" 2>&1 &
server=$!
sleep 1

echo "$label before: $(cat /proc/loadavg)" >> "$out/load.txt"
url="http://127.0.0.1:$port/?label=$label&args=bench+--seeds+$seeds+--label+$label"
# shellcheck disable=SC2086
${LAUNCH:-} "${CHROMIUM:-chromium}" --headless=new --user-data-dir="$scratch/profile" --no-first-run \
  --no-default-browser-check "$url" >"$scratch/browser.log" 2>&1 &
waited=0
while [ ! -s "$out/web-$label.json" ] && [ "$waited" -lt "${WAIT:-3600}" ]; do
  sleep 5
  waited=$((waited + 5))
done
echo "$label after: $(cat /proc/loadavg)" >> "$out/load.txt"
[ -s "$out/web-$label.json" ] || { echo "no report after $waited s" >&2; tail -20 "$scratch/browser.log" >&2; exit 1; }
python3 -I -c 'import json, sys; r = json.load(open(sys.argv[1])); sys.exit(0 if r["exit"] == 0 and r["report"] else 1)' \
  "$out/web-$label.json" || { echo "the page's run failed; see $out/web-$label.json" >&2; exit 1; }
