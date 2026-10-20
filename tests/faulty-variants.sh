#!/usr/bin/env bash
# Shows that each check catches what it is for: builds the check program against a copy of
# sleeve.h with one deliberate fault, runs the check that should catch it, and requires that
# check, and only through its own failure line, to fail. The unaltered library must pass the
# same checks first. A fault that is undefined behaviour shows only under UBSan: with
# SLEEVE_FAULTS=ub, and CC and CFLAGS building with it, only those faults run, each caught
# by a runtime error in its check.
#
# SLEEVE_FAULTS_ONLY, a list of variant names, runs only those.
#
# usage: [SLEEVE_FAULTS=ub] [SLEEVE_FAULTS_ONLY=NAMES] tests/faulty-variants.sh BUILD_DIR
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
build=${1:?usage: faulty-variants.sh BUILD_DIR}
mkdir -p "$build"
build=$(cd "$build" && pwd)
cc=${CC:-cc}
read -r -a cflags <<< "${CFLAGS:--std=c99 -O2 -Wall -Wextra}"
mode=${SLEEVE_FAULTS:-plain}
read -r -a only <<< "${SLEEVE_FAULTS_ONLY:-}"

# whether the variant NAME runs in this mode, MODE being its own
wanted() {
  local name
  [[ $mode == "$2" ]] || return 1
  (( ${#only[@]} == 0 )) && return 0
  for name in "${only[@]}"; do [[ $name == "$1" ]] && return 0; done
  return 1
}
cd "$here"

# replaces the one occurrence of each OLD with its NEW in FILE, or fails
replace() {
  python3 -I - "$@" <<'PY'
import sys
path, pairs = sys.argv[1], sys.argv[2:]
text = open(path).read()
for old, new in zip(pairs[::2], pairs[1::2]):
    if text.count(old) != 1:
        sys.exit(f"faulty-variants: expected one occurrence of {old!r}, found {text.count(old)}")
    text = text.replace(old, new)
open(path, "w").write(text)
PY
}

build_check() {
  "$cc" "${cflags[@]}" -I "$1" -isystem third_party/stb -o "$1/check" tests/check.c -lm
}

failed=0
mkdir -p "$build/base"
cp sleeve.h "$build/base/sleeve.h"
build_check "$build/base"

# variant NAME CHECK OLD NEW [OLD NEW]...: the check must pass on the library and fail on the
# variant
variant() {
  local name=$1 check=$2 dir="$build/$1" output
  wanted "$name" plain || return 0
  shift 2
  mkdir -p "$dir"
  cp sleeve.h "$dir/sleeve.h"
  replace "$dir/sleeve.h" "$@"
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

# ub_variant NAME CHECK OLD NEW [OLD NEW]...: the same for undefined behaviour, which the check
# must report as a runtime error
ub_variant() {
  local name=$1 check=$2 dir="$build/$1" output
  wanted "$name" ub || return 0
  shift 2
  mkdir -p "$dir"
  cp sleeve.h "$dir/sleeve.h"
  replace "$dir/sleeve.h" "$@"
  build_check "$dir"
  if ! "$build/base/check" --scratch "$dir" "$check" >/dev/null 2>&1; then
    echo "faulty-variants: $check fails on the library itself" >&2
    failed=1
    return
  fi
  if output=$("$dir/check" --scratch "$dir" "$check" 2>&1); then
    echo "NOT CAUGHT $name: $output"
    failed=1
  elif grep -q "runtime error:" <<< "$output"; then
    echo "caught $name: $(grep -m1 -o "runtime error:.*" <<< "$output")"
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

variant angle-subtracted-unreduced extremes \
  'return sleeve__cos(90 - d % 360);' \
  'return sleeve__cos(90 - d);'
variant ratio-scaled-whole extremes \
  'scaled = whole * scale + sleeve__div_round(num % den * scale, den);' \
  'scaled = sleeve__div_round(num * scale, den);' \
  '  if (r >= 0) return r >= d - r ? q + 1 : q;
  return -r > d + r ? q - 1 : q;' \
  '  q = (2 * n + d) / (2 * d), r = (2 * n + d) % (2 * d);
  return r < 0 ? q - 1 : q;'
variant periods-counted-in-floats extremes \
  'k.t = g->repeating ? local : t;' \
  'k.t = t;' \
  '      float first = sleeve__floor((local - k.h1) / g->period);
      float last = sleeve__floor((local + k.h1) / g->period);
      int period, periods;
      if (!(last - first <= 32 && first >= -1048576.0f && last <= 1048576.0f)) {
        for (ch = 0; ch < 4; ch++) out[ch] = g->mean[ch];
        return;
      }
      periods = (int)(last - first);
      for (period = (int)first; periods >= 0; period++, periods--)
        sleeve__pieces(g, &k, (float)period * g->period, acc);' \
  '      float first = sleeve__floor((t - k.h1) / g->period);
      float last = sleeve__floor((t + k.h1) / g->period);
      if (last - first > 32) {
        for (ch = 0; ch < 4; ch++) out[ch] = g->mean[ch];
        return;
      }
      for (; first <= last; first += 1) sleeve__pieces(g, &k, first * g->period, acc);'
variant named-indices-swapped parameters \
  '  SLEEVE_HORIZON_SUN_MIN,             /* its radius */
  SLEEVE_HORIZON_SUN_MAX,' \
  '  SLEEVE_HORIZON_SUN_MAX,             /* its radius */
  SLEEVE_HORIZON_SUN_MIN,'
variant parameters-unclamped parameters \
  'for (i = 0; i < n; i++) v[i] = sleeve__clamp(given[i], list[i].lo, list[i].hi);' \
  'for (i = 0; i < n; i++) v[i] = given[i];'
ub_variant png-width-multiplied-first extremes \
  '  int y, x, n, run = 0, previous = -1;
  if (width < 1 || height < 1 || width > 16384 || height > 16384) return 0;
  n = width * 3;' \
  '  int y, x, n = width * 3, run = 0, previous = -1;
  if (width < 1 || height < 1 || width > 16384 || height > 16384) return 0;'
ub_variant angle-overflowing extremes \
  'return sleeve__cos(90 - d % 360);' \
  'return sleeve__cos(90 - d);'

exit "$failed"
