# sleeve

sleeve makes stand-in album covers: abstract, flat-colour artwork for a record that has none. A
64-bit seed becomes a cover's *description*, a small plain-data value: an opaque base colour and up
to twelve paint layers of linear, radial and checker gradients with hard or soft colour stops.
The description rasterizes to any size, anti-aliased, and writes out as SVG and PNG.

It is one C file, `sleeve.h`, in the manner of single-file libraries such as clay.h. It has no
dependencies beyond the C standard library, allocates nothing, and keeps no global state.

![96 covers from seed 0](results/20261020T1936Z-contact-sheets/seeds-0-95.png)

## Using it

```c
#define SLEEVE_IMPLEMENTATION   /* in exactly one translation unit */
#include "sleeve.h"

sleeve_cover cover;
static uint8_t pixels[256 * 256 * 4];
sleeve_generate(sleeve_hash("Album\tArtist", 12), &cover);
sleeve_render(&cover, 256, 256, pixels, 256 * 4);
```

| Call | What it does |
| --- | --- |
| `sleeve_hash(bytes, length)` | A stable 64-bit seed for any identity: FNV-1a, then MurmurHash3's finalizer. |
| `sleeve_generate(seed, &cover)` | The cover for a seed. |
| `sleeve_check(&cover)` | 0 when the renderers accept a description, -1 otherwise; for descriptions made by hand. |
| `sleeve_render(&cover, w, h, rgba, stride)` | Rasterizes into the caller's RGBA8 buffer, opaque, the square stretched to w × h (1 to 16384 each). |
| `sleeve_render_rows(&cover, w, h, row0, row1, rgba, stride)` | The same for a range of rows, to spread a large cover over frames or threads. |
| `sleeve_svg(&cover, w, h, out, capacity)` | The cover as an SVG document; returns its length as `snprintf` does, so a capacity of 0 sizes the buffer. |
| `sleeve_png_bound(w, h)`, `sleeve_png(rgba, w, h, stride, out, capacity)` | Encodes pixels as an 8-bit RGB PNG into the caller's buffer; the bound is always enough. |
| `sleeve_describe(&cover, out, capacity)` | The description as text, one line per layer. |
| `sleeve_composition_name(i)`, `sleeve_mood_name(i)` | The default style's names for a generated cover's composition and mood. |
| `sleeve_generate_styled(seed, &style, &cover)` | The cover for a seed in a style; see [Styles](#styles). |
| `sleeve_generate_with_palette(seed, &style, colours, n, &cover)` | The same, drawing from 1 to 16 given colours in place of the style's moods. |
| `sleeve_style_check(&style, reason, capacity)`, `sleeve_style_hash(&style)` | Whether generation accepts a style, and why not; a style's fingerprint for keying caches. |
| `sleeve_describe_styled(&cover, &style, out, capacity)`, `sleeve_style_composition_name(&style, i)`, `sleeve_style_mood_name(&style, i)` | Names and text taken from the style a cover was drawn in. |
| `sleeve_write_png(path, &cover, w, h)`, `sleeve_write_svg(...)` | Render and write a file; left out when `SLEEVE_NO_STDIO` is defined, which also drops `<stdio.h>` and `<stdlib.h>`. |

## The description

Lengths are integers in units of the cover's side, `SLEEVE_UNIT` (10000) being the whole side;
angles are whole degrees. Each layer paints over everything before it:

- **linear** is CSS's `linear-gradient`: its line runs through the centre at `angle` degrees
  clockwise from straight up, `|sin| + |cos|` sides long, and a stop's `at` is its distance
  from the line's start;
- **radial** has rings around (`cx`, `cy`), a point's distance being `sqrt(dx² + (dy·ex/ey)²)`,
  so `ex` and `ey` set an ellipse's proportions;
- **checker** has squares `ex` on a side, the square with its corner at (`cx`, `cy`) in the
  first stop's colour and its neighbours in the second's.

Colours are 8-bit sRGB with straight alpha. Between two stops the colour is interpolated in
premultiplied sRGB; two stops at one position make a hard edge. A repeating layer starts at 0 and
repeats every last stop's distance. Every generated description passes `sleeve_check`.

`sleeve describe` prints the text form:

```
sleeve 1 disc duotone
palette #023e5b #037cb6 #efe2cc #e19342 #aabde8
base #037cb6
radial 5000 5000 10000 10000 : #e19342ff@0 #e19342ff@2104 #efe2ccff@2104 #efe2ccff@2403 #efe2cc00@2403
linear 141 : #efe2cc00@0 #efe2cc3c@14065
```

### What the generator draws

In the default style, a seed picks one of ten *moods*, each a palette recipe in OKLCH (dusk, sea, earth, warm and cool
paper, neon, mist, garden, duotone, ember), and draws five colours from it: deep, mid, pale and two
accents, each pulled into the sRGB gamut by lowering its chroma. It then picks one of twelve
*compositions* and draws its parameters: a horizon with a sun and lines in the ground; a glow
rising from the bottom edge; rings or a record with grooves and a label; a pair of reels;
repeating stripes; proportional bands or a translucent sheen; a hard diagonal split; thin neon
bars; a constellation of dots; repeating grooves from an off-centre point; two large orbs; a
checkerboard under a disc or a band. The neon mood keeps to the compositions where its bright
accents stay small marks on a dark ground.

## Styles

A style shapes what generation draws. `sleeve_generate(seed, &cover)` draws in
`sleeve_default_style`, the ten moods and twelve compositions above; any other style goes to
`sleeve_generate_styled(seed, &style, &cover)`. A style is plain data the caller owns: arrays of
parts and their counts, with optional function pointers for C callers who add pieces of their
own. It allocates nothing and holds no state, so one style serves any number of threads.

| Part | What it holds | When its count is 0 |
| --- | --- | --- |
| `moods` | each a mood definition (`sleeve_mood_def`), a weight, and optionally a weight for each of the style's compositions that replaces theirs under this mood | the default moods |
| `compositions` | each a name, a function (`run`), its parameters, and a weight | the default compositions |
| `palette_hooks` | functions run in order on the five colours before composing | none |
| `finishers` | functions run in order after composing, each with the layers it keeps free | none |

A generated cover's `composition` and `mood` index the style's tables;
`sleeve_style_composition_name` and `sleeve_style_mood_name` give their names, and
`sleeve_describe_styled` writes them into the text form. The default moods' composition
weights, such as neon's, index the default compositions, so they apply only when the
compositions are the default ones as well.

```c
static const sleeve_split_params steep = {30, 150, 20, 80, 1200, 2200};
static const sleeve_style_part parts[] = {
  {"orbs", sleeve_compose_orbs, 0, 3, 0, 0},                     /* NULL: the defaults */
  {"split", sleeve_compose_split, &steep, 2, 0, 0},
};
static const sleeve_style_part frame[] = {
  {"frame", sleeve_finish_frame, 0, 0, 2, 0},                    /* keeps 2 layers free */
};
static const sleeve_style framed = {"framed", 0, 0, parts, 2, 0, 0, frame, 1};

char reason[200];
if (sleeve_style_check(&framed, reason, sizeof reason) != 0) fprintf(stderr, "%s\n", reason);
sleeve_generate_styled(seed, &framed, &cover);
```

### Moods

A mood is a hue range and a recipe for each of the five roles, deep, mid, pale, accent and
second accent (`enum sleeve_role`): a hue offset from the mood's hue (−360 to 360 degrees) and an
OKLCH lightness (0 to 1000) and chroma (0 to 400), both in thousandths, each a range from which a
value is drawn. A range of one value pins it; a role whose `exact` colour has alpha 255 is that
sRGB colour as it is, which is how a style holds a brand's colours. The built-in moods are
`sleeve_moods`, found by name with `sleeve_find_mood`: `dusk`, `sea`, `earth`, `paper-warm`,
`paper-cool`, `neon`, `mist`, `garden`, `duotone`, `ember`.

### Built-in pieces

Every built-in composition, palette hook and finisher has a stable name, a function, and a
parameter struct of `int32_t` fields only (`sleeve_horizon_params` and so on, declared in
`sleeve.h`). `sleeve_pieces(kind, &count)` lists a kind's pieces and `sleeve_find_piece(kind,
name)` finds one; each piece lists its parameters in field order with their ranges and
defaults, and `sleeve_find_param` finds one by name. A skin compiler therefore resolves a
declared style from names and numbers alone: it looks a piece up, starts from its `defaults`,
sets the fields it names, and lets `sleeve_style_check` judge the result. A parameter with
`at_most` set must not exceed the parameter it names, as each `_min` its `_max`. `NULL`
parameters are the defaults.

Below, a bare number is a percentage chance (0 to 100); other ranges follow in brackets. Lengths
are in side units, colours `0xRRGGBB`, amounts and lightness in thousandths, alpha 0 to 255,
roles 0 to 4 in the order above.

| Kind | Name | Parameters and defaults |
| --- | --- | --- |
| composition | `horizon` | `night` 40, `level` 60, `tilt` 8 (0–30), `horizon_min` 420 (200–800), `horizon_max` 650 (200–800), `sun` 85, `setting` 30, `sun_min` 700 (300–2500), `sun_max` 1500 (300–2500), `band` 55, `lines` 35 |
| composition | `glow` | `dusk` 60, `radius_min` 4500 (2000–9000), `radius_max` 6500 (2000–9000), `alpha_min` 170 (0–255), `alpha_max` 220 (0–255), `sun` 55 |
| composition | `disc` | `record` 35, `centred` 60, `label_min` 900 (300–2500), `label_max` 1400 (300–2500), `rings_min` 2 (1–3), `rings_max` 3 (1–3), `halo` 40, `wash` 40 |
| composition | `pair` | `dark` 60, `spread_min` 1800 (1000–3000), `spread_max` 2300 (1000–3000), `hub_min` 800 (300–1800), `hub_max` 1300 (300–1800), `strip` 50, `spots` 60 |
| composition | `stripes` | `solid` 65, `colours_min` 2 (2–4), `colours_max` 4 (2–4), `wobble` 4 (0–20), `disc` 40 |
| composition | `bands` | `sheen` 25, `bands_min` 3 (2–4), `bands_max` 4 (2–4), `thin` 40, `tilt` 30, `disc` 30 |
| composition | `split` | `angle_min` 20 (0–180), `angle_max` 160 (0–180), `cross` 45, `disc` 55, `disc_min` 1000 (300–3500), `disc_max` 2200 (300–3500) |
| composition | `bars` | `vertical` 70, `bars_min` 2 (1–6), `bars_max` 3 (1–6), `tilt` 25 |
| composition | `constellation` | `stars_min` 4 (1–9), `stars_max` 9 (1–9), `moon` 35, `star_min` 200 (100–800), `star_max` 450 (100–800) |
| composition | `grooves` | `lines` 25, `two_tone` 50, `disc` 20 |
| composition | `orbs` | `wash` 50, `edge` 50, `oval` 40 |
| composition | `checks` | `tile_min` 400 (150–2500), `tile_max` 1000 (150–2500), `dark` 50, `disc` 60 |
| palette hook | `tint` | `colour` #f2e3c6, `amount` 250 (0–1000): every colour mixed toward one |
| palette hook | `chroma` | `scale` 500 (0–2000): every colour's chroma scaled, in thousandths of itself |
| palette hook | `monochrome` | `hue` −1 (−1–359): every colour turned to one hue, −1 for the palette's most chromatic colour's |
| palette hook | `lightness` | `lo` 150, `hi` 900 (0–1000): lightness 0 to 1000 mapped onto `lo` to `hi` |
| palette hook | `duotone` | `dark` #1d2b4f, `light` #f3e8d2: the darkest colour becomes the dark ink, the lightest the light one, the rest fall between by lightness |
| finisher, 1 layer | `vignette` | `role` 0, `alpha` 110, `inner` 4500 (0–14000), `outer` 7500 (0–14000): the edges toward a colour, from `inner` out from the centre to full at `outer` |
| finisher, 1 layer | `wash` | `role` 2, `alpha` 40: a flat translucent colour |
| finisher, 2 layers | `frame` | `role` 2, `alpha` 255, `width` 600 (50–2500): a border |
| finisher, 1 layer | `lines` | `role` 0, `alpha` 36, `angle` 45 (0–359), `pitch` 160 (40–2000), `width` 60 (10–2000, at most `pitch`): fine repeating lines |

The compositions' chances are those of the descriptions in [What the generator
draws](#what-the-generator-draws): `night` is the share of horizons at night, `record` of discs
drawn as records, and so on; `sleeve.h` comments each field.

### Checking a style

`sleeve_style_check(&style, reason, capacity)` returns 0 for a style generation accepts, or −1
with the first problem as text, such as `compositions[1] (split): disc_min 4000 is outside 300 to
3500` or `moods[0] (brand): mid light_hi 400 is outside 420 to 1000`. It refuses counts beyond
`SLEEVE_STYLE_MAX_MOODS` and `SLEEVE_STYLE_MAX_COMPOSITIONS` (32) or `SLEEVE_STYLE_MAX_HOOKS` (8),
missing names or functions, ranges out of bounds or reversed, moods' or compositions' weights
summing to 0, a built-in piece's parameters out of range, a built-in of one kind used as
another, a finisher keeping fewer layers free than its piece adds, and finishers keeping more
than `SLEEVE_STYLE_MAX_RESERVED` (6) layers free in all. Generation expects a style that passes;
a code hook's parameters are its owner's to check.

### Writing a composition on the builder

A composition, palette hook or finisher is a `sleeve_hook`: `void (*)(sleeve_builder *b, const
void *params)`. It reads `b->palette`, the five colours, and `b->cover`; draws every choice from
the builder (`sleeve_below`, `sleeve_range`, `sleeve_chance`, `sleeve_pick`); and paints with
`sleeve_add_linear`, `sleeve_add_radial`, `sleeve_add_layer`, `sleeve_add_disc`,
`sleeve_add_glow`, `sleeve_add_stop`, `sleeve_add_band` and `sleeve_along`, mixing colours with
`sleeve_alpha`, `sleeve_mix`, `sleeve_oklch` and `sleeve_to_oklch`. It can call any built-in
piece's function first, a built-in horizon and then its own overlay:

```c
/* A horizon with a ring hung over it. */
static void ringed_horizon(sleeve_builder *b, const void *params) {
  sleeve_layer *ring;
  int32_t r;
  (void)params;
  sleeve_compose_horizon(b, NULL);
  if (sleeve_layers_left(b) == 0) return;
  r = sleeve_range(b, 1200, 2000);
  ring = sleeve_add_radial(b, 5000, sleeve_range(b, 2500, 4000));
  sleeve_add_stop(ring, r, sleeve_alpha(b->palette[SLEEVE_PALE], 0));
  sleeve_add_band(ring, r, r + 150, b->palette[SLEEVE_PALE]);
  sleeve_add_stop(ring, r + 150, sleeve_alpha(b->palette[SLEEVE_PALE], 0));
}

static const sleeve_style_part parts[] = {{"ringed-horizon", ringed_horizon, 0, 1, 0, 1}};
```

A built-in called first draws exactly what it draws alone, since the hook's own draws come
after it; `sleeve_layers_left` then says whether there is room for more.

Whatever a hook does through the builder, the cover stays within `SLEEVE_MAX_LAYERS` and
`SLEEVE_MAX_STOPS` and passes `sleeve_check`:

- a layer added beyond the hook's limit is a spare that is never part of the cover, so a hook
  need not check, and `sleeve_layers_left` says how many more it may add;
- a composition's limit leaves free the layers the style's finishers keep, and finishers then
  share whatever the composition left, up to `SLEEVE_MAX_LAYERS`; palette hooks may add none;
- a stop beyond `SLEEVE_MAX_STOPS` is dropped, and one before the layer's last stop moves up to
  it;
- after a code hook has run, layers `sleeve_check` would refuse are dropped, the base and the
  palette are made opaque, and the layer count is kept within bounds.

### A palette for one call

```c
sleeve_colour album[3] = {{0x26, 0x46, 0x53, 255}, {0xe9, 0xc4, 0x6a, 255}, {0xe7, 0x6f, 0x51, 255}};
sleeve_generate_with_palette(seed, &sleeve_default_style, album, 3, &cover);
```

An app with an album's own colours passes 1 to `SLEEVE_MAX_GIVEN` (16) of them in place of the
style's moods; composition choice still follows the style, by the compositions' own weights,
the palette hooks do not run, and the cover's mood is `SLEEVE_NO_MOOD`, named `given`. The
colours are assigned to the five roles by their OKLCH lightness and chroma, and the cover's
`palette` holds them as assigned, so a caller tinting from a cover gets the album's colours back.
`sleeve_assign_palette` returns the same five roles without generating a cover.

- The darkest colour is deep, if its lightness is at most 450.
- The lightest of the rest is pale, if its lightness is at least 750.
- The most chromatic of the rest is the accent, if its chroma is at least 50.
- The one of the rest nearest in lightness to the middle of deep and pale is mid.
- The most chromatic left is the second accent, if its chroma is at least 50; colours beyond
  these go unused.

A role left empty is derived in OKLCH from the colours given, in the order deep, pale, mid,
accent, second accent. The *key* is the most chromatic colour given.

| Role | Lightness | Hue | Chroma |
| --- | --- | --- | --- |
| deep | 240 | the key's | the key's, at most 80 |
| pale | 930 | the key's | a third of the key's, at most 40 |
| mid | halfway between deep and pale | the key's | three quarters of the key's |
| accent | 720 | the key's | the key's |
| second accent | the accent's, moved 160 toward the middle of deep and pale | the accent's | four fifths of the accent's |

So one colour takes the role its lightness and chroma give it and the other four are its tones:
a saturated red is the accent, under a dark red, a pale pink, a muted red mid and a lighter red.
Two colours such as black and white are deep and pale, with greys between; a navy and an orange
are deep and accent, with an orange-tinted pale and mid.

### Fingerprints and caches

`sleeve_style_hash(&style)` is a 64-bit fingerprint of everything in a style that decides its
covers: the moods' recipes, exact colours and weights, the compositions' weights, the moods'
composition weights where they apply, each built-in piece by name with its parameters (`NULL`
hashing as the defaults), the layers finishers keep free, and `SLEEVE_GENERATOR`. Equal style
data hashes equal on every platform and in every build; the names of the style and of its moods
do not count. A function pointer is not stable from build to build, so a code hook counts by its
part's `name` and `version`, which its owner changes whenever the hook or its parameters would
draw differently. An app caching covers keys them on the seed, the style's fingerprint, the
palette given for the call if any, and the size.

### Example styles

`tools/styles.h` holds three styles beside the default, each shaping covers through a different
part, which the command-line tool and the browser demo offer by name:

- **riso**: every palette printed in two inks, a deep blue and a warm paper, by the `duotone`
  palette hook, under fine diagonal lines from the `lines` finisher;
- **brand**: one mood whose deep, pale and accent roles are the exact colours of a navy, cream
  and amber identity, and whose mid and second accent are drawn around them;
- **gallery**: three compositions, an arch written on the builder as a code hook, and the
  built-in orbs and split retuned, each on a mat from the `frame` finisher.

### Determinism per style

The same seed, the same style data and the same given palette give the same description, byte
for byte, on every platform, provided every code hook draws its choices only from the builder's
generator and computes in integers. The built-in pieces do, and with the palette assignment and
the colour conversions they use only integer arithmetic and the lookup tables. `SLEEVE_GENERATOR`
covers the default style and every built-in piece; a style made of built-in pieces draws the
same covers while it stays the same, and its fingerprint changes with it.

## Determinism

- The same seed gives the same description, byte for byte, on every platform and compiler, for
  as long as `SLEEVE_GENERATOR` stays the same: generation uses only integer arithmetic, a
  SplitMix64 stream seeded through MurmurHash3's finalizer, and lookup tables (cosines, the sRGB
  transfer function). The version changes whenever any seed's cover would.
- `sleeve_hash` is fixed: `sleeve_hash("", 0)` is `0xefd01f60ba992926` and
  `sleeve_hash("sleeve", 6)` is `0xf6f05973edcde22c`, on every platform.
- A raster is the same within one 8-bit level on every platform. It is computed in single
  precision with the operations in a fixed order; a compiler that fuses multiplies and adds into
  FMA instructions can move a channel by one level. Built without FMA, x86-64 natively and
  WebAssembly give identical bytes.

## The rasterizer

Each pixel takes each layer's exact average over the pixel's square. Along a gradient a square's
footprint is a box convolved with a box, a trapezoid, and the colour between stops is linear, so
the average is an integral of a piecewise-quadratic product: Simpson's rule evaluates it exactly,
piece by piece, and where a footprint lies inside one piece the colour at its centre is the
average. Radial layers are treated as locally straight, and pixels within two footprints of a
centre are split 4 × 4. Repeating layers integrate whole periods, up to 32, and use the period's
mean beyond. Checkers are exact. Layers composite with source-over in premultiplied sRGB, which
leaves one known error: where two layers' edges cross one pixel, their coverages conflate, as in
browsers.

## File formats

- **SVG**: a `viewBox` of `0 0 10000 10000` stretched to the requested size, one full-cover
  `rect` per layer filled with a `linearGradient` or `radialGradient` in user space (ellipses
  through a `gradientTransform`, repetition through `spreadMethod="repeat"`), and checkers as
  square paths (a `<pattern>` beyond 4096 squares). It renders the same picture as the raster;
  the SVG check states how close.
- **PNG**: 8-bit RGB, one zlib stream of fixed-Huffman deflate with run-length matches, each row
  filtered with whichever of the five filters gives the smallest absolute sum. Covers come out at
  roughly a fifth of their raw size.

## The command-line tool

```sh
make                                   # build/sleeve and build/check
build/sleeve cover --size 512 --out cover.png key:'Album Title'
build/sleeve cover --svg --out covers/ 1 2 3
build/sleeve sheet --size 160 --columns 8 --first 1000 96 sheet.png
build/sleeve sheet --style gallery 48 gallery.png
build/sleeve cover --palette '#264653,#e9c46a,#e76f51' --out album.png key:'Album Title'
build/sleeve describe --style riso 7
build/sleeve seed key:'Album Title'
build/sleeve styles
build/sleeve pieces
```

A seed is a decimal number, a `0x` hexadecimal one, or `key:TEXT` for `sleeve_hash` of TEXT's
bytes. `cover` writes one file to `--out`, or several into the directory it names, each named by
its seed; `sheet` writes COUNT covers from `--first` on, in a grid, as one PNG. `cover`, `sheet`
and `describe` take `--style NAME`, one of the [example styles](#example-styles), and
`--palette COLOURS`, 1 to 16 colours as `#rrggbb` separated by commas, drawn from in place of
the style's moods. `styles` lists the styles with their fingerprints, and `pieces` the built-in
pieces with their parameters' ranges and defaults.

## The browser demo

```sh
tools/browser-demo                     # builds build/browser-demo.html and opens it
tools/browser-demo --no-open demo.html
```

A page of ten covers, each labelled with its composition, mood and seed, redrawn from fresh
random seeds at each press of Generate, with the time the ten took. The script compiles
`sleeve.h` for freestanding wasm32 with `tools/browser-demo.c`, the page's entry points, and
embeds the module in `tools/browser-demo.html`, so the page opens from its file with no server.
`WASM_CC` names a Clang that targets wasm32 with `wasm-ld` beside it (`clang` by default); the
script also needs Python 3.

## Building

`sleeve.h` is C99 and builds without warnings under `-Wall -Wextra -Wpedantic -Werror`:

| Target | Compiler | Command |
| --- | --- | --- |
| Desktop | GCC or Clang | `cc -std=c99 -O2 -o sleeve tools/sleeve.c -lm` |
| Android | the NDK's Clang | `aarch64-linux-android29-clang -std=c99 -O2 -o sleeve tools/sleeve.c -lm` |
| WebAssembly | wasi-sdk's Clang | `clang --target=wasm32-wasi -std=c99 -O2 -o sleeve.wasm tools/sleeve.c -lm` |

The library itself calls only `sqrtf`, as a compiler builtin, and needs `-lm` only when built
without optimization. Built by GCC or Clang with `SLEEVE_NO_STDIO` defined, it depends on no
C library at all.

## Checks

`make check` builds `build/check` and runs it; `make faults` shows each check failing on a
deliberately faulty copy of `sleeve.h`, for the reason it predicts. `scripts/offline-gate` at the
repository's root runs `make check`.

| Check | What it holds | Fails on |
| --- | --- | --- |
| descriptions | 21 seeds' text descriptions, and a digest of seeds 0–19,999, equal the goldens in `tests/golden/` | mood weights changed |
| rasters | one cover per composition at 64 px is within one level of its golden PNG | interpolating with the square of the fraction |
| antialiasing | 55 layers alone and 24 whole covers at 48 px against a 64 × 64-sample double-precision reference: layers worst 4 levels and mean 0.5; covers mean 0.6, at most 0.2 % of channels off by more than 8 | point sampling |
| png | 40 images of awkward sizes decode with the vendored stb_image to exactly the pixels encoded; one byte too little capacity is refused | a wrong filter byte |
| svg | 48 covers rendered by `rsvg-convert` at 16× and averaged down are within mean 0.6, 0.2 % of channels beyond 8, worst 48 of the raster (skipped without a renderer) | circles drawn for ellipses |
| variety | over 20,000 seeds, compositions and moods keep their shares within 12 %, runs of neighbouring seeds look random, palettes cover the hue circle, no two covers repeat, and neighbouring seeds look different | uniform composition weights; unmixed seeds |
| api | row ranges equal the whole raster, invalid inputs are refused, sizing calls agree, the hash is fixed | a translucent base |
| default-style | the default style through the styled call, declared from names as a skin compiler would, and with every part empty, each draw `sleeve_generate`'s bytes for 20,000 seeds and share its fingerprint | moods' own composition weights applied only in the default style itself |
| style-shares | a style of 3 moods and 4 compositions draws them in its declared shares over 20,000 seeds, a mood's own composition weights apply under it, and names come through the style | the first weight counted one too many |
| style-hooks | each built-in palette hook changes the palette the composition draws from, in order, and not a given palette; each finisher paints on top | the last palette hook skipped |
| builder | a horizon with a ring over it and a disc on a ground, both on the builder, pass `sleeve_check` and render as drawn, the horizon as it draws alone; the budget and generator answer as documented | a disc without its closing stop |
| containment | hooks adding 30 layers of 12 falling stops, or writing invalid layers, counts, base and palette, leave covers that pass `sleeve_check` with a finisher's layers on top; 3,000 styles of built-in pieces with random parameters in range pass too | no layers kept for finishers; invalid layers kept |
| style-check | 17 invalid styles are refused, each for its reason; the default, example and empty styles are accepted | a parameter above its `at_most` accepted |
| style-hash | equal styles fingerprint equal, `NULL` parameters as the defaults and names aside; each of 13 changes that decide covers changes the fingerprint | a code hook's version left out |
| given-palette | five colours take their roles in all 120 orders; single colours, and black with white, take theirs with tones derived; 0 and 17 colours are refused; covers keep the colours as assigned, of mood `given`, without palette hooks | deep taken as the lightest |
| style-determinism | 2,000 seeds of each example style, every built-in hook and given palettes equal their digests and fingerprints in `tests/golden/styles.txt`, whatever the cover held before | duotone's mix rounded down |

## Why C

sleeve's language was settled by measurement: Haskell if, on every host measured, it generated
and rasterized within 1.25× of C's time, otherwise C. `bench/` holds the measurement: a Haskell
port written for speed (strict loops over unboxed arrays, nothing allocated per pixel) that
produces identical descriptions and rasters, a harness that times both on the same 1,000 seeds at
64, 216 and 512 px, and the runners for each host. `results/20261020T1931Z-language/` holds the
reports. Haskell took 1.20–1.53× C's time natively on the desktop, 2.2× in a browser and 2.6–3.1×
under WAMR on an Android phone, so the library is C; the Haskell port stays in `bench/` only as the measurement's comparison, built and
checked against `sleeve.h` by `bench`'s own `check`, and is not maintained as a second
implementation.

## Licence

Released into the public domain under the Unlicense; see [UNLICENSE](UNLICENSE). The checks vendor
stb_image (`third_party/stb/`), which is public domain or MIT.
