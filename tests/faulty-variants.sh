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
  '  for (i = 0; i < n; i++) total += parts[i].weight;
  x = sleeve__below(r, total);' \
  '  for (i = 0; i < n; i++) total += parts[i].weight;
  return sleeve__below(r, n);
  x = sleeve__below(r, total);'
variant unmixed-seeds variety \
  'b.rng.state = sleeve__mix(seed ^ 0x736c65657665ull);' \
  'b.rng.state = seed;'
variant translucent-base api \
  'if (cover->layer_count > SLEEVE_MAX_LAYERS || cover->base.a != 255) return -1;' \
  'if (cover->layer_count > SLEEVE_MAX_LAYERS) return -1;'

variant rows-only-for-the-default default-style \
  'p->rows = style->mood_count > 0 || style->composition_count <= 0;' \
  'p->rows = style == d;'
variant first-weight-inclusive style-shares \
  'if (x < parts[i].weight) return i;' \
  'if (x <= parts[i].weight) return i;'
variant last-palette-hook-skipped style-hooks \
  'for (i = 0; i < s.palette_hook_count; i++)
      if (s.palette_hooks[i].run)' \
  'for (i = 0; i + 1 < s.palette_hook_count; i++)
      if (s.palette_hooks[i].run)'
variant open-disc builder \
  '  sleeve__add_band(l, 0, r, c);
  sleeve__add_stop(l, r, sleeve__alpha(c, 0));' \
  '  sleeve__add_band(l, 0, r, c);'
variant no-layers-kept-for-finishers containment \
  'b.limit = SLEEVE_MAX_LAYERS - reserved;' \
  'b.limit = SLEEVE_MAX_LAYERS + 0 * reserved;'
variant invalid-layers-kept containment \
  'if (style != &sleeve_default_style && !sleeve__trusted(&s, part)) {' \
  'if (0 && !sleeve__trusted(&s, part)) {'
variant at-most-ignored style-check \
  'if (p->at_most >= 0 && v[i] > v[p->at_most]) {' \
  'if (0 && v[i] > v[p->at_most]) {'
variant version-unhashed style-hash \
  '    h = sleeve__hash_int(h, part->version);
' \
  ''
variant deep-is-the-lightest given-palette \
  '    if (best < 0 || l[i] < l[best]) best = i;
  if (l[best] <= 450)' \
  '    if (best < 0 || l[i] > l[best]) best = i;
  if (l[best] <= 450)'
variant duotone-rounded-down style-determinism \
  '(int32_t)sleeve__div_round((int64_t)(l[i] - lo) * 1000, hi - lo) : 500;' \
  '(int32_t)((int64_t)(l[i] - lo) * 1000 / (hi - lo)) : 500;'

exit "$failed"
