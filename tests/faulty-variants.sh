#!/usr/bin/env bash
# Shows that each check catches what it is for: builds the check program against a copy of
# sleeve.h with one deliberate fault, runs the check that should catch it, and requires that
# check, and only through its own failure line, to fail. The unaltered library must pass the
# same checks first.
#
# usage: tests/faulty-variants.sh BUILD_DIR
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
build=${1:?usage: faulty-variants.sh BUILD_DIR}
mkdir -p "$build"
build=$(cd "$build" && pwd)
cc=${CC:-cc}
read -r -a cflags <<< "${CFLAGS:--std=c99 -O2 -Wall -Wextra}"
cd "$here"

# replaces the one occurrence of OLD with NEW in FILE, or fails
replace() {
  python3 -I - "$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1:]
text = open(path).read()
if text.count(old) != 1:
    sys.exit(f"faulty-variants: expected one occurrence of {old!r}, found {text.count(old)}")
open(path, "w").write(text.replace(old, new))
PY
}

build_check() {
  "$cc" "${cflags[@]}" -I "$1" -isystem third_party/stb -o "$1/check" tests/check.c -lm
}

failed=0
mkdir -p "$build/base"
cp sleeve.h "$build/base/sleeve.h"
build_check "$build/base"

# variant NAME CHECK OLD NEW: the check must pass on the library and fail on the variant
variant() {
  local name=$1 check=$2 old=$3 new=$4 dir="$build/$1" output
  mkdir -p "$dir"
  cp sleeve.h "$dir/sleeve.h"
  replace "$dir/sleeve.h" "$old" "$new"
  build_check "$dir"
  if ! "$build/base/check" --scratch "$dir" "$check" >/dev/null; then
    echo "faulty-variants: $check fails on the library itself" >&2
    failed=1
    return
  fi
  if output=$("$dir/check" --scratch "$dir" "$check"); then
    echo "NOT CAUGHT $name: $output"
    failed=1
  elif grep -q "^FAIL $check:" <<< "$output"; then
    echo "caught $name: $(grep "^FAIL $check:" <<< "$output")"
  else
    echo "NOT CAUGHT $name (failed otherwise): $output"
    failed=1
  fi
}

variant mood-weights descriptions \
  '{&sleeve_moods[SLEEVE_DUOTONE], 10, 0}, {&sleeve_moods[SLEEVE_EMBER], 9, 0},' \
  '{&sleeve_moods[SLEEVE_DUOTONE], 9, 0}, {&sleeve_moods[SLEEVE_EMBER], 10, 0},'
variant squared-interpolation rasters \
  'out[ch] = g->c[i][ch] + (g->c[i + 1][ch] - g->c[i][ch]) * f;' \
  'out[ch] = g->c[i][ch] + (g->c[i + 1][ch] - g->c[i][ch]) * f * f;'
variant no-antialiasing antialiasing \
  '  h = 0.5f * (a + b);' \
  '  a = b = 0;
  h = 0.5f * (a + b);'
variant wrong-filter-byte png \
  'int v = x < 0 ? best : sleeve__filtered(rgba, stride, y, x, best);' \
  'int v = x < 0 ? (best + 1) % 5 : sleeve__filtered(rgba, stride, y, x, best);'
variant circles-for-ellipses svg \
  'if (l->ex != l->ey) {' \
  'if (0) {'
variant unweighted-compositions variety \
  'composition = row ? sleeve__pick(&b.rng, row, s.composition_count)
              : sleeve__pick_part(&b.rng, s.compositions, s.composition_count);' \
  'composition = row ? sleeve__below(&b.rng, s.composition_count)
              : sleeve__below(&b.rng, s.composition_count);'
variant unmixed-seeds variety \
  'b.rng.state = sleeve__mix(seed ^ 0x736c65657665ull);' \
  'b.rng.state = seed;'
variant translucent-base api \
  'if (cover->layer_count > SLEEVE_MAX_LAYERS || cover->base.a != 255) return -1;' \
  'if (cover->layer_count > SLEEVE_MAX_LAYERS) return -1;'

exit "$failed"
