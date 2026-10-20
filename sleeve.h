/* sleeve.h: procedural stand-in album covers, as a single-file C library.

   A cover is a resolution-independent description: an opaque base colour and a short stack of
   paint layers (linear, radial and checker gradients, plain or repeating, with colour stops).
   sleeve_generate turns a 64-bit seed into a description; sleeve_render rasterizes one into an
   RGBA8 buffer of any size; sleeve_svg and sleeve_png write it out as files.

   Declarations are always included. Define SLEEVE_IMPLEMENTATION in exactly one translation unit
   before including this file to compile the implementation there. Define SLEEVE_NO_STDIO as
   well to leave out the file-writing helpers, which are the only parts that use <stdio.h> or
   <stdlib.h>.

   The README beside this file documents the description, the determinism contract and the
   file formats. This is free and unencumbered software released into the public domain; see
   UNLICENSE. */

#ifndef SLEEVE_H
#define SLEEVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The generator's version: covers from the same seed stay the same while it does. */
#define SLEEVE_GENERATOR 1

/* Lengths and positions are integers in units of the cover's side: SLEEVE_UNIT is the side. */
#define SLEEVE_UNIT 10000
#define SLEEVE_MAX_LAYERS 12
#define SLEEVE_MAX_STOPS 8

enum sleeve_kind {
  SLEEVE_LINEAR = 1,  /* bands along a line at an angle */
  SLEEVE_RADIAL = 2,  /* rings around a centre, circular or elliptical */
  SLEEVE_CHECKER = 3  /* a checkerboard of two colours */
};

typedef struct sleeve_colour {
  uint8_t r, g, b, a; /* sRGB, not premultiplied */
} sleeve_colour;

typedef struct sleeve_stop {
  int32_t at;           /* a distance along the gradient, in side units */
  sleeve_colour colour;
} sleeve_stop;

/* One paint layer, drawn over everything before it.
   - Linear: CSS's linear-gradient. The gradient line passes through the cover's centre at
     `angle` degrees clockwise from straight up; it is |sin| + |cos| sides long, and a stop's
     `at` is its distance from the line's start.
   - Radial: rings around (cx, cy). A point's distance is sqrt(dx^2 + (dy * ex / ey)^2), so a
     ring is a circle when ex equals ey and an ellipse ex wide for every ey tall otherwise.
   - Checker: squares `ex` on a side, the one whose top-left corner is (cx, cy) in the first
     stop's colour, its neighbours in the second's. Its stops' positions are unused.
   Colour between two stops is interpolated in premultiplied sRGB; beyond the first and last
   stops it is theirs. A repeating gradient starts its first stop at 0 and repeats every last
   stop's distance. */
typedef struct sleeve_layer {
  uint8_t kind;        /* enum sleeve_kind */
  uint8_t repeating;   /* 0 or 1 */
  uint8_t stop_count;  /* 1 to SLEEVE_MAX_STOPS; 2 for a checker */
  uint8_t reserved;    /* 0 */
  int32_t angle;       /* linear: degrees */
  int32_t cx, cy;      /* radial: centre; checker: a corner */
  int32_t ex, ey;      /* radial: shape, both positive; checker: ex is the square's side */
  sleeve_stop stops[SLEEVE_MAX_STOPS];
} sleeve_layer;

typedef struct sleeve_cover {
  uint32_t generator;            /* SLEEVE_GENERATOR, or 0 for a cover made by hand */
  uint8_t composition;           /* enum sleeve_composition, for a generated cover */
  uint8_t mood;                  /* enum sleeve_mood, for a generated cover */
  uint8_t layer_count;           /* 0 to SLEEVE_MAX_LAYERS */
  uint8_t palette_count;         /* the palette's colours, 5 for a generated cover */
  sleeve_colour palette[5];      /* the colours the cover draws from: deep, mid, pale, accents */
  sleeve_colour base;            /* opaque */
  sleeve_layer layers[SLEEVE_MAX_LAYERS];
} sleeve_cover;

enum sleeve_composition {
  SLEEVE_HORIZON, SLEEVE_GLOW, SLEEVE_DISC, SLEEVE_PAIR, SLEEVE_STRIPES, SLEEVE_BANDS,
  SLEEVE_SPLIT, SLEEVE_BARS, SLEEVE_CONSTELLATION, SLEEVE_GROOVES, SLEEVE_ORBS, SLEEVE_CHECKS,
  SLEEVE_COMPOSITIONS
};

enum sleeve_mood {
  SLEEVE_DUSK, SLEEVE_SEA, SLEEVE_EARTH, SLEEVE_PAPER_WARM, SLEEVE_PAPER_COOL, SLEEVE_NEON,
  SLEEVE_MIST, SLEEVE_GARDEN, SLEEVE_DUOTONE, SLEEVE_EMBER,
  SLEEVE_MOODS
};

/* A stable 64-bit hash of bytes, for keying covers by an identity of the caller's choice. */
uint64_t sleeve_hash(const void *bytes, size_t length);

/* The cover for a seed. */
void sleeve_generate(uint64_t seed, sleeve_cover *cover);

/* 0 when a description is one the renderers accept, otherwise -1. Generated covers are. */
int sleeve_check(const sleeve_cover *cover);

/* Rasterizes a cover into width x height RGBA8 pixels, rows `stride` bytes apart, alpha 255.
   The square is stretched to the rectangle. Allocates nothing and touches no global state.
   Returns 0, or -1 for an invalid description or size (width and height 1 to 16384). */
int sleeve_render(const sleeve_cover *cover, int width, int height, uint8_t *rgba,
                  ptrdiff_t stride);

/* The same for rows [row0, row1) only, into the same full-image buffer: a caller can spread a
   large cover over several frames, or threads. */
int sleeve_render_rows(const sleeve_cover *cover, int width, int height, int row0, int row1,
                       uint8_t *rgba, ptrdiff_t stride);

/* Writes the cover as an SVG document width x height pixels large into out, NUL-terminated
   when it fits, and returns its length without the NUL (as snprintf does): call it with a
   capacity of 0 to size the buffer. Returns 0 for an invalid description. */
size_t sleeve_svg(const sleeve_cover *cover, int width, int height, char *out, size_t capacity);

/* The most bytes sleeve_png can write for an image of this size. */
size_t sleeve_png_bound(int width, int height);

/* Encodes RGBA8 pixels as an 8-bit RGB PNG (alpha is dropped: covers are opaque) into out.
   Returns its length, or 0 when the capacity is too small or the size invalid. */
size_t sleeve_png(const uint8_t *rgba, int width, int height, ptrdiff_t stride, uint8_t *out,
                  size_t capacity);

/* A one-line-per-layer text form of a description, as sleeve_svg returns its length. */
size_t sleeve_describe(const sleeve_cover *cover, char *out, size_t capacity);

const char *sleeve_composition_name(int composition);
const char *sleeve_mood_name(int mood);

#ifndef SLEEVE_NO_STDIO
/* Renders and writes a cover as a PNG or an SVG file. Returns 0, or -1 on any failure. */
int sleeve_write_png(const char *path, const sleeve_cover *cover, int width, int height);
int sleeve_write_svg(const char *path, const sleeve_cover *cover, int width, int height);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SLEEVE_H */

#ifdef SLEEVE_IMPLEMENTATION
#ifndef SLEEVE_IMPLEMENTED
#define SLEEVE_IMPLEMENTED

#ifndef SLEEVE_NO_STDIO
#include <stdio.h>
#include <stdlib.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SLEEVE__SQRTF(x) __builtin_sqrtf(x)
#else
#include <math.h>
#define SLEEVE__SQRTF(x) sqrtf(x)
#endif

/* The square root of a number known not to be negative: the guard lets the compiler use the
   instruction alone, without a call into the maths library to set errno. */
static float sleeve__sqrt(float x) { return x > 0 ? SLEEVE__SQRTF(x) : 0; }

/* The floor of x; every float of magnitude 2^23 or more is already whole. */
static float sleeve__floor(float x) {
  float t;
  if (!(x > -8388608.0f && x < 8388608.0f)) return x;
  t = (float)(int32_t)x;
  return t > x ? t - 1.0f : t;
}

/* ---- Integer arithmetic ---------------------------------------------------------------- */

/* cos(d degrees) in units of 1/65536, for d in [0, 90] (tools/tables.py). */
static const int32_t sleeve__cos_table[91] = {
  65536, 65526, 65496, 65446, 65376, 65287, 65177, 65048, 64898, 64729, 64540, 64332,
  64104, 63856, 63589, 63303, 62997, 62672, 62328, 61966, 61584, 61183, 60764, 60326,
  59870, 59396, 58903, 58393, 57865, 57319, 56756, 56175, 55578, 54963, 54332, 53684,
  53020, 52339, 51643, 50931, 50203, 49461, 48703, 47930, 47143, 46341, 45525, 44695,
  43852, 42995, 42126, 41243, 40348, 39441, 38521, 37590, 36647, 35693, 34729, 33754,
  32768, 31772, 30767, 29753, 28729, 27697, 26656, 25607, 24550, 23486, 22415, 21336,
  20252, 19161, 18064, 16962, 15855, 14742, 13626, 12505, 11380, 10252, 9121, 7987,
  6850, 5712, 4572, 3430, 2287, 1144, 0,
};

/* round(65535 * linear(v / 255)) for each 8-bit sRGB value v (tools/tables.py). */
static const int32_t sleeve__linear_table[256] = {
  0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219,
  241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562,
  599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101,
  1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863,
  1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866,
  2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129,
  4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669,
  5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500,
  7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635,
  9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
  12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874,
  15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
  18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481,
  21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
  25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542,
  29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
  34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138,
  39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
  45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341,
  50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
  57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221,
  63795, 64372, 64952, 65535,
};

/* cos(d degrees) in units of 1/65536, for any whole d. */
static int32_t sleeve__cos(int32_t d) {
  d %= 360;
  if (d < 0) d += 360;
  if (d <= 90) return sleeve__cos_table[d];
  if (d <= 180) return -sleeve__cos_table[180 - d];
  if (d <= 270) return -sleeve__cos_table[d - 180];
  return sleeve__cos_table[360 - d];
}

static int32_t sleeve__sin(int32_t d) { return sleeve__cos(90 - d); }

static int32_t sleeve__abs(int32_t v) { return v < 0 ? -v : v; }

/* n / d rounded to the nearest, halves up, for d > 0. */
static int64_t sleeve__div_round(int64_t n, int64_t d) {
  int64_t q = (2 * n + d) / (2 * d), r = (2 * n + d) % (2 * d);
  return r < 0 ? q - 1 : q;
}

/* The length of a linear gradient's line at this angle, in side units. */
static int32_t sleeve__line_length(int32_t angle) {
  return (int32_t)sleeve__div_round(
      (int64_t)(sleeve__abs(sleeve__sin(angle)) + sleeve__abs(sleeve__cos(angle))) * SLEEVE_UNIT,
      65536);
}

/* MurmurHash3's 64-bit finalizer: every input bit reaches every output bit. */
static uint64_t sleeve__mix(uint64_t h) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdull;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ull;
  h ^= h >> 33;
  return h;
}

uint64_t sleeve_hash(const void *bytes, size_t length) {
  const uint8_t *p = (const uint8_t *)bytes;
  uint64_t h = 0xcbf29ce484222325ull;
  size_t i;
  for (i = 0; i < length; i++) h = (h ^ p[i]) * 0x100000001b3ull;
  return sleeve__mix(h);
}

/* ---- Pseudo-random choices --------------------------------------------------------------- */

typedef struct sleeve__rng {
  uint64_t state;
} sleeve__rng;

static uint32_t sleeve__next(sleeve__rng *r) {
  uint64_t z = (r->state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  z ^= z >> 31;
  return (uint32_t)(z >> 32);
}

/* A number in [0, n). */
static int32_t sleeve__below(sleeve__rng *r, int32_t n) {
  return (int32_t)(((uint64_t)sleeve__next(r) * (uint64_t)(uint32_t)n) >> 32);
}

/* A number in [lo, hi]. */
static int32_t sleeve__range(sleeve__rng *r, int32_t lo, int32_t hi) {
  return lo + sleeve__below(r, hi - lo + 1);
}

static int sleeve__chance(sleeve__rng *r, int32_t percent) {
  return sleeve__below(r, 100) < percent;
}

/* An index into weights[0..n), chosen in proportion to the weights. */
static int sleeve__pick(sleeve__rng *r, const uint8_t *weights, int n) {
  int32_t total = 0, x;
  int i;
  for (i = 0; i < n; i++) total += weights[i];
  x = sleeve__below(r, total);
  for (i = 0; i < n - 1; i++) {
    if (x < weights[i]) return i;
    x -= weights[i];
  }
  return n - 1;
}

/* ---- Colour ------------------------------------------------------------------------------ */

/* The 8-bit sRGB value nearest a linear value in units of 1/65535, the lower on a tie. */
static uint8_t sleeve__encode(int64_t x) {
  int lo = 0, hi = 256;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if ((int64_t)sleeve__linear_table[mid - 1] + sleeve__linear_table[mid] < 2 * x) lo = mid;
    else hi = mid;
  }
  return (uint8_t)lo;
}

static int64_t sleeve__cube(int64_t v) {
  return sleeve__div_round(v * v * v, (int64_t)65536 * 65536);
}

/* OKLCH to sRGB: lightness and chroma in thousandths, hue in degrees. Chroma is lowered in
   steps of 0.004 until the colour is inside the sRGB gamut. */
static sleeve_colour sleeve__oklch(int32_t lightness, int32_t chroma, int32_t hue) {
  sleeve_colour out;
  int64_t l = sleeve__div_round((int64_t)lightness * 65536, 1000);
  for (;;) {
    int64_t a = sleeve__div_round((int64_t)chroma * sleeve__cos(hue), 1000);
    int64_t b = sleeve__div_round((int64_t)chroma * sleeve__sin(hue), 1000);
    int64_t lc = sleeve__cube(l + sleeve__div_round(25974 * a + 14143 * b, 65536));
    int64_t mc = sleeve__cube(l + sleeve__div_round(-6918 * a - 4185 * b, 65536));
    int64_t sc = sleeve__cube(l + sleeve__div_round(-5864 * a - 84639 * b, 65536));
    int64_t red = sleeve__div_round(267173 * lc - 216774 * mc + 15137 * sc, 65536);
    int64_t green = sleeve__div_round(-83128 * lc + 171033 * mc - 22369 * sc, 65536);
    int64_t blue = sleeve__div_round(-275 * lc - 46099 * mc + 111910 * sc, 65536);
    if (chroma <= 0 || (red >= -64 && red <= 65600 && green >= -64 && green <= 65600 &&
                        blue >= -64 && blue <= 65600)) {
      out.r = sleeve__encode(red);
      out.g = sleeve__encode(green);
      out.b = sleeve__encode(blue);
      out.a = 255;
      return out;
    }
    chroma -= 4;
  }
}

/* a and b mixed, `thousandths` of the way to b, in sRGB. */
static sleeve_colour sleeve__mix_colour(sleeve_colour a, sleeve_colour b, int32_t thousandths) {
  sleeve_colour c;
  c.r = (uint8_t)(a.r + sleeve__div_round((int64_t)(b.r - a.r) * thousandths, 1000));
  c.g = (uint8_t)(a.g + sleeve__div_round((int64_t)(b.g - a.g) * thousandths, 1000));
  c.b = (uint8_t)(a.b + sleeve__div_round((int64_t)(b.b - a.b) * thousandths, 1000));
  c.a = 255;
  return c;
}

static sleeve_colour sleeve__alpha(sleeve_colour c, int32_t alpha) {
  c.a = (uint8_t)alpha;
  return c;
}

/* ---- Palettes ---------------------------------------------------------------------------- */

enum { SLEEVE__DEEP, SLEEVE__MID, SLEEVE__PALE, SLEEVE__ACCENT, SLEEVE__ACCENT2 };

/* A palette role: a hue offset from the mood's hue, a lightness and a chroma, each a range. */
typedef struct sleeve__role {
  int16_t hue_lo, hue_hi, light_lo, light_hi, chroma_lo, chroma_hi;
} sleeve__role;

typedef struct sleeve__mood {
  int16_t hue_lo, hue_hi;
  sleeve__role roles[5]; /* deep, mid, pale, accent, second accent */
} sleeve__mood;

static const sleeve__mood sleeve__moods[SLEEVE_MOODS] = {
  /* dusk: indigo into rose, with peach and gold */
  {275, 320, {{0, 15, 220, 300, 60, 110}, {45, 75, 480, 580, 110, 150},
              {95, 125, 860, 920, 45, 75}, {90, 120, 730, 810, 120, 160},
              {60, 80, 620, 690, 130, 160}}},
  /* sea: blues and teals, with sand */
  {195, 235, {{0, 10, 260, 360, 50, 90}, {-15, 5, 520, 630, 70, 110},
              {-20, 0, 890, 950, 20, 45}, {-150, -120, 850, 910, 70, 110},
              {-40, -25, 690, 770, 70, 100}}},
  /* earth: ochre and rust, with olive */
  {40, 80, {{-20, 15, 260, 340, 30, 60}, {-10, 10, 560, 660, 90, 125},
            {5, 25, 880, 930, 30, 60}, {90, 130, 500, 600, 50, 85},
            {-35, -20, 500, 580, 110, 140}}},
  /* warm paper: cream, ink, and a red-orange accent */
  {75, 95, {{150, 190, 250, 320, 25, 60}, {-55, -40, 560, 640, 140, 170},
            {0, 10, 930, 965, 20, 35}, {-60, -45, 600, 680, 150, 180},
            {-20, 0, 780, 840, 80, 110}}},
  /* cool paper: cream, ink, and a blue accent */
  {80, 100, {{170, 180, 260, 320, 40, 80}, {165, 180, 480, 560, 110, 150},
             {0, 10, 930, 965, 15, 30}, {140, 160, 600, 680, 90, 120},
             {-15, 5, 780, 850, 70, 100}}},
  /* neon: a near-black night with bright pink, cyan and yellow */
  {260, 295, {{0, 10, 150, 210, 40, 80}, {0, 20, 300, 380, 80, 120},
              {150, 175, 880, 920, 140, 170}, {65, 95, 640, 710, 200, 250},
              {-80, -55, 790, 850, 120, 150}}},
  /* mist: cool greys with one warm note */
  {200, 250, {{0, 10, 360, 440, 15, 35}, {0, 10, 640, 720, 15, 30},
              {0, 10, 910, 950, 5, 15}, {150, 200, 760, 820, 60, 90},
              {-10, 10, 520, 580, 40, 60}}},
  /* garden: greens with coral and lemon */
  {125, 165, {{0, 10, 340, 420, 50, 80}, {-20, 0, 620, 710, 70, 100},
              {-45, -25, 910, 945, 45, 70}, {165, 200, 720, 790, 100, 130},
              {-65, -50, 860, 900, 110, 140}}},
  /* duotone: one hue against its complement */
  {0, 359, {{0, 0, 300, 400, 80, 120}, {0, 20, 550, 640, 100, 140},
            {150, 210, 860, 920, 30, 60}, {160, 200, 700, 780, 100, 140},
            {20, 40, 750, 820, 60, 90}}},
  /* ember: warm near-black with red and gold */
  {20, 45, {{-10, 10, 170, 220, 15, 30}, {0, 10, 540, 600, 160, 190},
            {40, 60, 900, 940, 40, 60}, {30, 50, 780, 840, 140, 170},
            {-20, -5, 610, 660, 150, 180}}},
};

static const uint8_t sleeve__mood_weights[SLEEVE_MOODS] = {12, 12, 11, 9, 8, 9, 10, 10, 10, 9};

static void sleeve__palette(sleeve__rng *r, int mood, sleeve_colour out[5]) {
  const sleeve__mood *m = &sleeve__moods[mood];
  int32_t hue = sleeve__range(r, m->hue_lo, m->hue_hi);
  int i;
  for (i = 0; i < 5; i++) {
    const sleeve__role *role = &m->roles[i];
    int32_t h = hue + sleeve__range(r, role->hue_lo, role->hue_hi);
    int32_t l = sleeve__range(r, role->light_lo, role->light_hi);
    int32_t c = sleeve__range(r, role->chroma_lo, role->chroma_hi);
    out[i] = sleeve__oklch(l, c, h);
  }
}

/* ---- Building a description -------------------------------------------------------------- */

typedef struct sleeve__gen {
  sleeve_cover *cover;
  sleeve__rng rng;
  sleeve_colour p[5];
} sleeve__gen;

static sleeve_layer *sleeve__layer(sleeve__gen *g, int kind, int repeating) {
  sleeve_layer *l = &g->cover->layers[g->cover->layer_count++];
  int i;
  l->kind = (uint8_t)kind;
  l->repeating = (uint8_t)repeating;
  l->stop_count = 0;
  l->reserved = 0;
  l->angle = 0;
  l->cx = l->cy = 0;
  l->ex = l->ey = SLEEVE_UNIT;
  for (i = 0; i < SLEEVE_MAX_STOPS; i++) {
    l->stops[i].at = 0;
    l->stops[i].colour.r = l->stops[i].colour.g = l->stops[i].colour.b = 0;
    l->stops[i].colour.a = 0;
  }
  return l;
}

static void sleeve__stop(sleeve_layer *l, int32_t at, sleeve_colour c) {
  if (l->stop_count == SLEEVE_MAX_STOPS) return;
  l->stops[l->stop_count].at = at;
  l->stops[l->stop_count].colour = c;
  l->stop_count++;
}

/* A band of colour from `from` to `to`: two stops, so the band's edges are hard. */
static void sleeve__band(sleeve_layer *l, int32_t from, int32_t to, sleeve_colour c) {
  sleeve__stop(l, from, c);
  sleeve__stop(l, to, c);
}

static sleeve_layer *sleeve__linear(sleeve__gen *g, int32_t angle) {
  sleeve_layer *l = sleeve__layer(g, SLEEVE_LINEAR, 0);
  l->angle = angle;
  return l;
}

/* A position along a linear layer's line, as thousandths of its length. */
static int32_t sleeve__along(const sleeve_layer *l, int32_t thousandths) {
  return (int32_t)sleeve__div_round((int64_t)sleeve__line_length(l->angle) * thousandths, 1000);
}

static sleeve_layer *sleeve__radial(sleeve__gen *g, int32_t cx, int32_t cy) {
  sleeve_layer *l = sleeve__layer(g, SLEEVE_RADIAL, 0);
  l->cx = cx;
  l->cy = cy;
  return l;
}

/* A hard-edged disc of radius r. */
static void sleeve__disc(sleeve__gen *g, int32_t cx, int32_t cy, int32_t r, sleeve_colour c) {
  sleeve_layer *l = sleeve__radial(g, cx, cy);
  sleeve__band(l, 0, r, c);
  sleeve__stop(l, r, sleeve__alpha(c, 0));
}

/* A soft glow of radius r, its colour at `alpha` in the centre and fading out. */
static void sleeve__glow(sleeve__gen *g, int32_t cx, int32_t cy, int32_t ex, int32_t ey,
                         int32_t r, int32_t inner, sleeve_colour c, int32_t alpha) {
  sleeve_layer *l = sleeve__radial(g, cx, cy);
  l->ex = ex;
  l->ey = ey;
  sleeve__band(l, 0, inner, sleeve__alpha(c, alpha));
  sleeve__stop(l, r, sleeve__alpha(c, 0));
}

/* A palette role, chosen in proportion to the weights. */
static int sleeve__pick_role(sleeve__gen *g, const uint8_t *weights) {
  return sleeve__pick(&g->rng, weights, 5);
}

/* ---- Compositions ------------------------------------------------------------------------ */

/* A sky over ground at a horizon, a sun or moon in the sky, and sometimes lines in the ground.
   The ground is its own layer over the sun, so a setting sun sinks behind it. */
static void sleeve__horizon(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  int night = sleeve__chance(r, 40);
  int32_t tilt = sleeve__chance(r, 60) ? 0 : sleeve__range(r, -8, 8);
  int32_t horizon = sleeve__range(r, 420, 650), band = 0;
  sleeve_layer *sky = sleeve__linear(g, 180 + tilt), *land;
  int32_t h = sleeve__along(sky, horizon), end = sleeve__along(sky, 1000);
  sleeve_colour top = g->p[night ? SLEEVE__DEEP : SLEEVE__MID];
  sleeve_colour low = g->p[night ? SLEEVE__MID : SLEEVE__PALE];
  sleeve_colour ground = g->p[SLEEVE__DEEP], edge = g->p[SLEEVE__ACCENT2];
  sleeve__stop(sky, 0, top);
  sleeve__stop(sky, h, low);
  if (sleeve__chance(r, 85)) {
    int setting = sleeve__chance(r, 30);
    int32_t sun_r = sleeve__range(r, 700, 1500), sun_x = sleeve__range(r, 2000, 8000);
    int32_t sun_y = setting ? horizon * 10 : sleeve__range(r, 1500, horizon * 10 - sun_r - 600);
    if (sun_y < sun_r + 500) sun_y = sun_r + 500;
    sleeve__disc(g, sun_x, sun_y, sun_r, g->p[night ? SLEEVE__PALE : SLEEVE__ACCENT]);
  }
  land = sleeve__linear(g, 180 + tilt);
  if (sleeve__chance(r, 55)) band = sleeve__range(r, 120, 450);
  sleeve__stop(land, h, sleeve__alpha(band ? edge : ground, 0));
  if (band) sleeve__band(land, h, h + band, edge);
  sleeve__band(land, h + band, end, ground);
  if (sleeve__chance(r, 35)) {
    sleeve_layer *lines = sleeve__linear(g, 180 + tilt);
    int32_t gap = sleeve__range(r, 400, 900), w = sleeve__range(r, 60, 160), at = h + band + gap;
    sleeve_colour c = sleeve__alpha(g->p[SLEEVE__PALE], sleeve__range(r, 70, 140));
    while (lines->stop_count + 4 <= SLEEVE_MAX_STOPS && at + w < end) {
      sleeve__stop(lines, at, sleeve__alpha(c, 0));
      sleeve__band(lines, at, at + w, c);
      sleeve__stop(lines, at + w, sleeve__alpha(c, 0));
      at += w + gap;
      gap = gap * 3 / 4 + 120;
    }
    if (lines->stop_count == 0) g->cover->layer_count--;
  }
}

/* A sky wash with a glow rising from the bottom edge, at dusk or at dawn, and sometimes a sun
   or moon above it. */
static void sleeve__glow_cover(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  sleeve_layer *wash = sleeve__linear(g, 180);
  int dusk = sleeve__chance(r, 60);
  int32_t gx = sleeve__range(r, 3500, 6500), radius = sleeve__range(r, 4500, 6500);
  sleeve__stop(wash, 0, g->p[dusk ? SLEEVE__DEEP : SLEEVE__MID]);
  if (dusk) sleeve__stop(wash, sleeve__range(r, 4500, 6500), g->p[SLEEVE__MID]);
  sleeve__stop(wash, SLEEVE_UNIT, g->p[dusk ? SLEEVE__ACCENT2 : SLEEVE__PALE]);
  {
    int32_t gy = SLEEVE_UNIT + sleeve__range(r, 0, 800), ey = sleeve__range(r, 5500, 8000);
    int32_t alpha = sleeve__range(r, 170, 220);
    sleeve__glow(g, gx, gy, SLEEVE_UNIT, ey, radius, radius / 3, g->p[SLEEVE__PALE], alpha);
  }
  if (sleeve__chance(r, 55)) {
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 1800, 4000);
    int32_t size = sleeve__range(r, 700, 1300);
    sleeve__disc(g, x, y, size, g->p[SLEEVE__ACCENT]);
  }
}

/* Concentric rings on a ground, or a record with grooves and a label, under a faint wash. */
static void sleeve__disc_cover(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  static const uint8_t grounds[5] = {4, 2, 4, 0, 1};
  int ground = sleeve__pick_role(g, grounds);
  int32_t cx = sleeve__chance(r, 60) ? 5000 : sleeve__range(r, 3500, 6500);
  int32_t cy = sleeve__chance(r, 60) ? 5000 : sleeve__range(r, 3500, 6500);
  sleeve_colour bg = g->p[ground];
  g->cover->base = bg;
  if (sleeve__chance(r, 35)) {
    /* the vinyl is the base, so the grooves on it and the ground around it meet in one edge */
    int32_t outer = sleeve__range(r, 3300, 4300), label = sleeve__range(r, 900, 1400);
    int32_t pitch = sleeve__range(r, 90, 200);
    sleeve_colour line = sleeve__alpha(g->p[SLEEVE__PALE], sleeve__range(r, 25, 45));
    sleeve_layer *grooves = sleeve__radial(g, cx, cy), *around;
    g->cover->base = g->p[ground == SLEEVE__DEEP ? SLEEVE__MID : SLEEVE__DEEP];
    grooves->repeating = 1;
    sleeve__band(grooves, 0, pitch / 2, sleeve__alpha(line, 0));
    sleeve__band(grooves, pitch / 2, pitch, line);
    around = sleeve__radial(g, cx, cy);
    sleeve__stop(around, outer, sleeve__alpha(bg, 0));
    sleeve__stop(around, outer, bg);
    sleeve__disc(g, cx, cy, label, g->p[sleeve__chance(r, 50) ? SLEEVE__ACCENT : SLEEVE__ACCENT2]);
    sleeve__disc(g, cx, cy, sleeve__range(r, 120, 220), bg);
  } else {
    sleeve_layer *rings = sleeve__radial(g, cx, cy);
    static const int order[4] = {SLEEVE__ACCENT, SLEEVE__PALE, SLEEVE__MID, SLEEVE__DEEP};
    int count = sleeve__range(r, 2, 3), i, first = sleeve__below(r, 4);
    int32_t at = 0;
    for (i = 0; i < count; i++) {
      int role = order[(first + i) % 4];
      int32_t w = i == 0 ? sleeve__range(r, 1000, 2400) : sleeve__range(r, 150, 900);
      if (role == ground) role = SLEEVE__ACCENT2;
      sleeve__band(rings, at, at + w, g->p[role]);
      at += w;
    }
    sleeve__stop(rings, at, sleeve__alpha(rings->stops[rings->stop_count - 1].colour, 0));
    if (sleeve__chance(r, 40)) {
      sleeve_layer *halo = sleeve__radial(g, cx, cy);
      int32_t gap = sleeve__range(r, 300, 800), w = sleeve__range(r, 120, 300);
      sleeve_colour c = sleeve__alpha(rings->stops[0].colour, sleeve__range(r, 80, 140));
      sleeve__stop(halo, at + gap, sleeve__alpha(c, 0));
      sleeve__band(halo, at + gap, at + gap + w, c);
      sleeve__stop(halo, at + gap + w, sleeve__alpha(c, 0));
    }
  }
  if (sleeve__chance(r, 40)) {
    sleeve_layer *wash = sleeve__linear(g, sleeve__range(r, 135, 200));
    sleeve__stop(wash, 0, sleeve__alpha(g->p[SLEEVE__PALE], 0));
    sleeve__stop(wash, sleeve__along(wash, 1000), sleeve__alpha(g->p[SLEEVE__PALE], 60));
  }
}

/* Two matching ringed discs side by side, as reels. */
static void sleeve__pair(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  int32_t spread = sleeve__range(r, 1800, 2300), y = sleeve__range(r, 4200, 5800);
  int32_t hub = sleeve__range(r, 800, 1300), rim = sleeve__range(r, 250, 450);
  int32_t gap = sleeve__range(r, 200, 400);
  int dark = sleeve__chance(r, 60), side;
  sleeve_colour ground = dark ? g->p[SLEEVE__DEEP] : g->p[SLEEVE__PALE];
  sleeve_colour wheel = dark ? g->p[SLEEVE__PALE] : g->p[SLEEVE__DEEP];
  g->cover->base = ground;
  if (sleeve__chance(r, 50)) {
    sleeve_layer *strip = sleeve__linear(g, 180);
    int32_t half = sleeve__range(r, 1600, 2400);
    sleeve_colour c = g->p[SLEEVE__MID];
    sleeve__stop(strip, y - half, sleeve__alpha(c, 0));
    sleeve__band(strip, y - half, y + half, c);
    sleeve__stop(strip, y + half, sleeve__alpha(c, 0));
  }
  for (side = -1; side <= 1; side += 2) {
    sleeve_layer *reel = sleeve__radial(g, 5000 + side * spread, y);
    sleeve__band(reel, 0, hub, wheel);
    sleeve__band(reel, hub, hub + gap, ground);
    sleeve__band(reel, hub + gap, hub + gap + rim, wheel);
    sleeve__stop(reel, hub + gap + rim, sleeve__alpha(wheel, 0));
  }
  if (sleeve__chance(r, 60)) {
    int32_t spot = sleeve__range(r, 180, 320);
    sleeve__disc(g, 5000 - spread, y, spot, g->p[SLEEVE__ACCENT]);
    sleeve__disc(g, 5000 + spread, y, spot, g->p[SLEEVE__ACCENT]);
  }
}

/* Repeating stripes at an angle, solid or as fine lines over a wash. */
static void sleeve__stripes(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  static const int16_t angles[10] = {0, 20, 45, 65, 90, 105, 115, 135, 160, 120};
  int32_t angle = angles[sleeve__below(r, 10)], wobble = sleeve__range(r, -4, 4);
  angle += wobble;
  if (sleeve__chance(r, 65)) {
    sleeve_layer *s = sleeve__linear(g, angle);
    static const int order[5] = {SLEEVE__DEEP, SLEEVE__MID, SLEEVE__PALE, SLEEVE__ACCENT,
                                 SLEEVE__ACCENT2};
    int count = sleeve__range(r, 2, 4), i, first = sleeve__below(r, 3);
    int32_t at = 0;
    s->repeating = 1;
    for (i = 0; i < count; i++) {
      int role = order[(first + i * 2) % 5];
      int32_t w = i == 0 ? sleeve__range(r, 400, 1200)
                         : (role >= SLEEVE__ACCENT ? sleeve__range(r, 100, 300)
                                                   : sleeve__range(r, 120, 700));
      sleeve__band(s, at, at + w, g->p[role]);
      at += w;
    }
    g->cover->base = s->stops[0].colour;
  } else {
    sleeve_layer *wash = sleeve__linear(g, sleeve__range(r, 150, 210));
    sleeve_layer *lines;
    int dark = sleeve__chance(r, 50);
    int32_t pitch = sleeve__range(r, 350, 800), w = sleeve__range(r, 40, 120);
    sleeve_colour ink = dark ? g->p[SLEEVE__PALE] : g->p[SLEEVE__DEEP];
    sleeve__stop(wash, 0, dark ? g->p[SLEEVE__DEEP] : g->p[SLEEVE__MID]);
    sleeve__stop(wash, sleeve__along(wash, 1000), dark ? g->p[SLEEVE__MID] : g->p[SLEEVE__PALE]);
    lines = sleeve__linear(g, angle);
    lines->repeating = 1;
    ink = sleeve__alpha(ink, sleeve__range(r, 60, 120));
    sleeve__band(lines, 0, pitch - w, sleeve__alpha(ink, 0));
    sleeve__band(lines, pitch - w, pitch, ink);
    if (sleeve__chance(r, 40)) {
      int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
      int32_t size = sleeve__range(r, 1000, 1800);
      sleeve__disc(g, x, y, size, g->p[SLEEVE__ACCENT]);
    }
  }
}

/* Hard bands across the cover in proportion, one of them sometimes a thin accent, and
   sometimes a disc; or a sheen of translucent bands over a diagonal wash. */
static void sleeve__bands(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  static const int16_t angles[6] = {180, 90, 180, 0, 270, 180};
  static const int order[5] = {SLEEVE__PALE, SLEEVE__MID, SLEEVE__ACCENT, SLEEVE__DEEP,
                               SLEEVE__ACCENT2};
  int32_t angle = angles[sleeve__below(r, 6)], weights[5], total = 0, at = 0, length;
  int count = sleeve__range(r, 3, 4), thin = sleeve__chance(r, 40) ? sleeve__below(r, count) : -1;
  int first = sleeve__below(r, 5), i;
  sleeve_layer *b;
  if (sleeve__chance(r, 25)) {
    /* a sheen: a diagonal wash crossed by two translucent pale bands */
    sleeve_layer *wash = sleeve__linear(g, sleeve__range(r, 120, 160)), *sheen;
    int32_t a1 = sleeve__range(r, 3500, 4500), w1 = sleeve__range(r, 200, 400);
    int32_t a2 = a1 + w1 + sleeve__range(r, 800, 1500), w2 = sleeve__range(r, 150, 300);
    sleeve_colour c1 = sleeve__alpha(g->p[SLEEVE__PALE], sleeve__range(r, 110, 150));
    sleeve_colour c2 = sleeve__alpha(g->p[SLEEVE__PALE], sleeve__range(r, 80, 110));
    sleeve__stop(wash, 0, g->p[SLEEVE__PALE]);
    sleeve__stop(wash, sleeve__along(wash, 500), g->p[SLEEVE__MID]);
    sleeve__stop(wash, sleeve__along(wash, 1000), g->p[SLEEVE__DEEP]);
    sheen = sleeve__linear(g, sleeve__chance(r, 50) ? 0 : sleeve__range(r, -10, 10));
    sleeve__stop(sheen, a1, sleeve__alpha(c1, 0));
    sleeve__band(sheen, a1, a1 + w1, c1);
    sleeve__stop(sheen, a1 + w1, sleeve__alpha(c1, 0));
    sleeve__stop(sheen, a2, sleeve__alpha(c2, 0));
    sleeve__band(sheen, a2, a2 + w2, c2);
    sleeve__stop(sheen, a2 + w2, sleeve__alpha(c2, 0));
    return;
  }
  if (sleeve__chance(r, 30)) angle += sleeve__range(r, -12, 12);
  b = sleeve__linear(g, angle);
  length = sleeve__along(b, 1000);
  for (i = 0; i < count; i++) {
    weights[i] = i == thin ? sleeve__range(r, 3, 6) : sleeve__range(r, 14, 36);
    total += weights[i];
  }
  for (i = 0; i < count; i++) {
    int32_t next = i == count - 1 ? length : at + length * weights[i] / total;
    sleeve__band(b, at, next, g->p[order[(first + i) % 5]]);
    at = next;
  }
  g->cover->base = b->stops[0].colour;
  if (sleeve__chance(r, 30)) {
    int32_t dr = sleeve__range(r, 900, 1600);
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
    int role = sleeve__chance(r, 50) ? SLEEVE__PALE : SLEEVE__ACCENT;
    sleeve__disc(g, x, y, dr, g->p[role]);
  }
}

/* A hard diagonal split, sometimes crossed by a band, sometimes with a disc. */
static void sleeve__split(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  int32_t angle = sleeve__range(r, 20, 160), flip = sleeve__chance(r, 50);
  sleeve_layer *s = sleeve__linear(g, flip ? angle + 180 : angle);
  int32_t at = sleeve__along(s, sleeve__range(r, 380, 620));
  int first = sleeve__chance(r, 50) ? SLEEVE__DEEP : SLEEVE__MID;
  int second = first == SLEEVE__DEEP ? (sleeve__chance(r, 50) ? SLEEVE__PALE : SLEEVE__ACCENT)
                                     : SLEEVE__PALE;
  sleeve__band(s, 0, at, g->p[first]);
  sleeve__band(s, at, sleeve__along(s, 1000), g->p[second]);
  if (sleeve__chance(r, 45)) {
    sleeve_layer *cross = sleeve__linear(g, s->angle + sleeve__range(r, 60, 120));
    int32_t c = sleeve__along(cross, sleeve__range(r, 350, 650)), w = sleeve__range(r, 500, 1500);
    sleeve_colour band = g->p[SLEEVE__ACCENT2];
    sleeve__stop(cross, c, sleeve__alpha(band, 0));
    sleeve__band(cross, c, c + w, band);
    sleeve__stop(cross, c + w, sleeve__alpha(band, 0));
  }
  if (sleeve__chance(r, 55)) {
    int32_t dr = sleeve__range(r, 1000, 2200);
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
    sleeve__disc(g, x, y, dr, g->p[second == SLEEVE__ACCENT ? SLEEVE__PALE : SLEEVE__ACCENT]);
  }
}

/* Thin bright bars on a dark ground that lightens at the bottom, two bars to a layer. */
static void sleeve__bars(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  sleeve_layer *ground = sleeve__linear(g, 180);
  int32_t angle = sleeve__chance(r, 70) ? 90 : 180;
  sleeve_layer *bars = 0;
  int count = sleeve__range(r, 2, 3), i;
  int32_t at = sleeve__range(r, 1500, 3500), length = sleeve__line_length(angle);
  sleeve__stop(ground, 0, g->p[SLEEVE__DEEP]);
  sleeve__stop(ground, sleeve__range(r, 6000, 8000), g->p[SLEEVE__DEEP]);
  sleeve__stop(ground, SLEEVE_UNIT, g->p[SLEEVE__MID]);
  if (sleeve__chance(r, 25)) angle += sleeve__range(r, -15, 15);
  for (i = 0; i < count && at < length - 400; i++) {
    int32_t w = sleeve__range(r, 150, 350);
    sleeve_colour c = g->p[i == 1 ? SLEEVE__ACCENT2 : (i == 2 ? SLEEVE__PALE : SLEEVE__ACCENT)];
    if (i % 2 == 0) bars = sleeve__linear(g, angle);
    sleeve__stop(bars, at, sleeve__alpha(c, 0));
    sleeve__band(bars, at, at + w, c);
    sleeve__stop(bars, at + w, sleeve__alpha(c, 0));
    at += w + sleeve__range(r, 600, 2400);
  }
}

/* Small bright dots on a night wash, sometimes with a larger moon. */
static void sleeve__constellation(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  sleeve_layer *sky = sleeve__linear(g, 180);
  int count = sleeve__range(r, 4, 9), i, placed = 0, tries = 0;
  int32_t xs[10], ys[10];
  sleeve__stop(sky, 0, g->p[SLEEVE__DEEP]);
  sleeve__stop(sky, SLEEVE_UNIT, sleeve__mix_colour(g->p[SLEEVE__DEEP], g->p[SLEEVE__MID], 400));
  if (sleeve__chance(r, 35)) {
    int32_t mr = sleeve__range(r, 900, 1400);
    xs[placed] = sleeve__range(r, 2500, 7500);
    ys[placed] = sleeve__range(r, 2000, 5000);
    sleeve__disc(g, xs[placed], ys[placed], mr, g->p[SLEEVE__PALE]);
    placed++;
  }
  while (placed < count + 1 && tries < 40 && g->cover->layer_count < SLEEVE_MAX_LAYERS) {
    int32_t x = sleeve__range(r, 1200, 8800), y = sleeve__range(r, 1200, 8800);
    int ok = 1;
    tries++;
    for (i = 0; i < placed; i++) {
      int64_t dx = x - xs[i], dy = y - ys[i];
      if (dx * dx + dy * dy < (int64_t)1800 * 1800) ok = 0;
    }
    if (!ok) continue;
    {
      int32_t dr = sleeve__range(r, 200, 450);
      int role = sleeve__chance(r, 50) ? SLEEVE__ACCENT : (sleeve__chance(r, 50) ? SLEEVE__ACCENT2
                                                                             : SLEEVE__PALE);
      xs[placed] = x;
      ys[placed] = y;
      placed++;
      sleeve__disc(g, x, y, dr, g->p[role]);
    }
  }
}

/* Repeating rings from a centre off to one side, as ripples or contours. */
static void sleeve__grooves(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  int32_t cx = sleeve__chance(r, 50) ? sleeve__range(r, -2000, 12000)
                                     : (sleeve__chance(r, 50) ? 0 : SLEEVE_UNIT);
  int32_t cy = sleeve__range(r, -2000, 12000);
  sleeve_layer *rings = sleeve__radial(g, cx, cy);
  int lines = sleeve__chance(r, 25);
  rings->repeating = 1;
  if (lines) {
    int dark = sleeve__chance(r, 50);
    int32_t pitch = sleeve__range(r, 350, 700), w = sleeve__range(r, 40, 90);
    sleeve_colour ink = dark ? g->p[SLEEVE__PALE] : g->p[SLEEVE__DEEP];
    g->cover->base = dark ? g->p[SLEEVE__DEEP] : g->p[SLEEVE__PALE];
    ink = sleeve__alpha(ink, sleeve__range(r, 70, 130));
    sleeve__band(rings, 0, pitch - w, sleeve__alpha(ink, 0));
    sleeve__band(rings, pitch - w, pitch, ink);
  } else if (sleeve__chance(r, 50)) {
    /* a ground and one colour */
    int ground = sleeve__chance(r, 50) ? SLEEVE__DEEP : SLEEVE__PALE;
    static const int others[3] = {SLEEVE__MID, SLEEVE__ACCENT, SLEEVE__ACCENT2};
    int32_t w = sleeve__range(r, 350, 900);
    sleeve__band(rings, 0, w, g->p[ground]);
    int32_t w2 = sleeve__range(r, 150, 450), other = others[sleeve__below(r, 3)];
    sleeve__band(rings, w, w + w2, g->p[other]);
    g->cover->base = g->p[ground];
  } else {
    /* the deep, mid and pale tones of one family, in some order */
    static const int orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1},
                                     {2, 1, 0}};
    const int *order = orders[sleeve__below(r, 6)];
    int i;
    int32_t at = 0;
    for (i = 0; i < 3; i++) {
      int32_t w = sleeve__range(r, 250, 800);
      sleeve__band(rings, at, at + w, g->p[order[i]]);
      at += w;
    }
    g->cover->base = rings->stops[0].colour;
  }
  if (sleeve__chance(r, 20)) {
    int32_t dr = sleeve__range(r, 900, 1600);
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
    sleeve__disc(g, x, y, dr, g->p[SLEEVE__ACCENT]);
  }
}

/* Two large discs or ellipses, overlapping or from the edges, over a ground. */
static void sleeve__orbs(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  static const uint8_t grounds[5] = {4, 3, 1, 0, 0};
  int ground = sleeve__pick_role(g, grounds), i;
  static const int roles[4] = {SLEEVE__PALE, SLEEVE__ACCENT, SLEEVE__ACCENT2, SLEEVE__MID};
  int first = sleeve__below(r, 4);
  g->cover->base = g->p[ground];
  if (sleeve__chance(r, 50)) {
    sleeve_layer *wash = sleeve__linear(g, sleeve__range(r, 100, 170));
    sleeve__stop(wash, 0, g->p[ground]);
    sleeve__stop(wash, sleeve__along(wash, 1000), g->p[ground == SLEEVE__DEEP ? SLEEVE__MID
                                                                              : SLEEVE__DEEP]);
  }
  for (i = 0; i < 2; i++) {
    int edge = sleeve__chance(r, 50);
    int32_t x = edge ? (i == 0 ? sleeve__range(r, -1000, 2500) : sleeve__range(r, 7500, 11000))
                     : sleeve__range(r, 2500, 7500);
    int32_t y = edge ? (i == 0 ? sleeve__range(r, 6000, 11000) : sleeve__range(r, -1000, 4000))
                     : sleeve__range(r, 2500, 7500);
    int32_t rad = edge ? sleeve__range(r, 3000, 4800) : sleeve__range(r, 1800, 3200);
    int role = roles[(first + i) % 4];
    sleeve_layer *orb;
    if (role == ground) role = SLEEVE__PALE;
    orb = sleeve__radial(g, x, y);
    orb->ey = sleeve__chance(r, 40) ? sleeve__range(r, 6000, 9000) : SLEEVE_UNIT;
    sleeve__band(orb, 0, rad, g->p[role]);
    sleeve__stop(orb, rad, sleeve__alpha(g->p[role], 0));
  }
}

/* A checkerboard, with a disc or a band over it. */
static void sleeve__checks(sleeve__gen *g) {
  sleeve__rng *r = &g->rng;
  sleeve_layer *c = sleeve__layer(g, SLEEVE_CHECKER, 0);
  int32_t tile = sleeve__range(r, 400, 1000);
  int dark = sleeve__chance(r, 50);
  c->ex = c->ey = tile;
  c->cx = 0;
  c->cy = 0;
  sleeve__stop(c, 0, g->p[SLEEVE__PALE]);
  sleeve__stop(c, 0, g->p[dark ? SLEEVE__DEEP : SLEEVE__MID]);
  g->cover->base = g->p[SLEEVE__PALE];
  if (sleeve__chance(r, 60)) {
    int32_t dr = sleeve__range(r, 1800, 3200);
    sleeve_layer *d = sleeve__radial(g, 5000, 5000);
    sleeve__band(d, 0, dr, g->p[SLEEVE__ACCENT]);
    sleeve__band(d, dr, dr + sleeve__range(r, 300, 700), g->p[SLEEVE__PALE]);
    sleeve__stop(d, d->stops[3].at, sleeve__alpha(g->p[SLEEVE__PALE], 0));
  } else {
    sleeve_layer *b = sleeve__linear(g, 180);
    int32_t at = sleeve__range(r, 5500, 7500);
    sleeve__stop(b, at, sleeve__alpha(g->p[SLEEVE__ACCENT], 0));
    sleeve__band(b, at, SLEEVE_UNIT, g->p[SLEEVE__ACCENT]);
  }
}

/* How often each composition is chosen; the neon mood's accents stay small marks on a dark
   ground, so it has weights of its own. */
static const uint8_t sleeve__composition_weights[2][SLEEVE_COMPOSITIONS] = {
  {14, 6, 12, 4, 12, 10, 12, 6, 7, 6, 8, 3},
  {14, 6, 12, 4, 0, 0, 0, 18, 14, 0, 0, 0},
};

void sleeve_generate(uint64_t seed, sleeve_cover *cover) {
  sleeve__gen g;
  int i;
  g.cover = cover;
  /* SplitMix64 from neighbouring states starts out correlated, so the seed is mixed first */
  g.rng.state = sleeve__mix(seed ^ 0x736c65657665ull);
  cover->generator = SLEEVE_GENERATOR;
  cover->layer_count = 0;
  cover->mood = (uint8_t)sleeve__pick(&g.rng, sleeve__mood_weights, SLEEVE_MOODS);
  cover->composition = (uint8_t)sleeve__pick(
      &g.rng, sleeve__composition_weights[cover->mood == SLEEVE_NEON], SLEEVE_COMPOSITIONS);
  sleeve__palette(&g.rng, cover->mood, g.p);
  cover->palette_count = 5;
  for (i = 0; i < 5; i++) cover->palette[i] = g.p[i];
  cover->base = g.p[SLEEVE__DEEP];
  switch (cover->composition) {
  case SLEEVE_HORIZON: sleeve__horizon(&g); break;
  case SLEEVE_GLOW: sleeve__glow_cover(&g); break;
  case SLEEVE_DISC: sleeve__disc_cover(&g); break;
  case SLEEVE_PAIR: sleeve__pair(&g); break;
  case SLEEVE_STRIPES: sleeve__stripes(&g); break;
  case SLEEVE_BANDS: sleeve__bands(&g); break;
  case SLEEVE_SPLIT: sleeve__split(&g); break;
  case SLEEVE_BARS: sleeve__bars(&g); break;
  case SLEEVE_CONSTELLATION: sleeve__constellation(&g); break;
  case SLEEVE_GROOVES: sleeve__grooves(&g); break;
  case SLEEVE_ORBS: sleeve__orbs(&g); break;
  default: sleeve__checks(&g); break;
  }
  for (i = cover->layer_count; i < SLEEVE_MAX_LAYERS; i++) {
    int j;
    cover->layers[i].kind = 0;
    cover->layers[i].repeating = 0;
    cover->layers[i].stop_count = 0;
    cover->layers[i].reserved = 0;
    cover->layers[i].angle = 0;
    cover->layers[i].cx = cover->layers[i].cy = 0;
    cover->layers[i].ex = cover->layers[i].ey = 0;
    for (j = 0; j < SLEEVE_MAX_STOPS; j++) {
      cover->layers[i].stops[j].at = 0;
      cover->layers[i].stops[j].colour = cover->base;
      cover->layers[i].stops[j].colour.a = 0;
      cover->layers[i].stops[j].colour.r = 0;
      cover->layers[i].stops[j].colour.g = 0;
      cover->layers[i].stops[j].colour.b = 0;
    }
  }
}

int sleeve_check(const sleeve_cover *cover) {
  int i, j;
  const int32_t far = 100 * SLEEVE_UNIT;
  if (cover->layer_count > SLEEVE_MAX_LAYERS || cover->base.a != 255) return -1;
  for (i = 0; i < cover->layer_count; i++) {
    const sleeve_layer *l = &cover->layers[i];
    if (l->stop_count < 1 || l->stop_count > SLEEVE_MAX_STOPS || l->repeating > 1) return -1;
    if (l->cx < -far || l->cx > far || l->cy < -far || l->cy > far) return -1;
    for (j = 0; j < l->stop_count; j++) {
      if (l->stops[j].at < -far || l->stops[j].at > far) return -1;
      if (j > 0 && l->stops[j].at < l->stops[j - 1].at) return -1;
    }
    if (l->repeating && (l->stops[0].at != 0 || l->stops[l->stop_count - 1].at <= 0)) return -1;
    switch (l->kind) {
    case SLEEVE_LINEAR: break;
    case SLEEVE_RADIAL:
      if (l->ex <= 0 || l->ey <= 0 || l->ex > far || l->ey > far) return -1;
      break;
    case SLEEVE_CHECKER:
      if (l->stop_count != 2 || l->repeating || l->ex <= 0 || l->ex > far) return -1;
      break;
    default: return -1;
    }
  }
  return 0;
}

/* ---- Rasterizing ------------------------------------------------------------------------- */

/* A layer ready to draw: positions in sides, colours premultiplied in [0, 255]. */
typedef struct sleeve__ready {
  int kind, repeating, n;
  float at[SLEEVE_MAX_STOPS];
  float c[SLEEVE_MAX_STOPS][4];
  float period, mean[4];
  float dx, dy, start;    /* linear: direction and the start's projection */
  float cx, cy, k;        /* radial: centre and the vertical scale */
  float reach;            /* radial: no colour beyond this distance, or < 0 */
  float tile;             /* checker */
} sleeve__ready;

/* The colour at t, already within a repeating gradient's period. */
static void sleeve__point(const sleeve__ready *g, float t, float out[4]) {
  int i, ch;
  if (t <= g->at[0]) {
    for (ch = 0; ch < 4; ch++) out[ch] = g->c[0][ch];
    return;
  }
  for (i = 0; i + 1 < g->n; i++) {
    if (t < g->at[i + 1]) {
      float f = (t - g->at[i]) / (g->at[i + 1] - g->at[i]);
      for (ch = 0; ch < 4; ch++) out[ch] = g->c[i][ch] + (g->c[i + 1][ch] - g->c[i][ch]) * f;
      return;
    }
  }
  for (ch = 0; ch < 4; ch++) out[ch] = g->c[g->n - 1][ch];
}

typedef struct sleeve__kernel {
  float t, h1, h2, inv_a, inv_ab;
} sleeve__kernel;

/* The trapezoid that a pixel's square projects to along the gradient, at a point within its
   support. A box has no sloping sides, so a point rounded just past its end still counts. */
static float sleeve__weight(const sleeve__kernel *k, float x) {
  float u = x - k->t;
  if (u < 0) u = -u;
  if (u <= k->h2 || k->inv_ab == 0) return k->inv_a;
  u = k->h1 - u;
  return u > 0 ? u * k->inv_ab : 0;
}

/* Adds the integral of a linear colour piece times the kernel over [x0, x1]: the piece's colour
   is c0 at `origin` and changes by `slope` a side. Simpson's rule is exact for the product. */
static void sleeve__piece(const sleeve__kernel *k, float x0, float x1, float origin,
                          const float *c0, const float *slope, float acc[4]) {
  float cuts[4];
  int n = 0, i, ch;
  float lo = k->t - k->h1, hi = k->t + k->h1;
  if (x0 < lo) x0 = lo;
  if (x1 > hi) x1 = hi;
  if (!(x1 > x0)) return;
  cuts[n++] = x0;
  if (k->t - k->h2 > x0 && k->t - k->h2 < x1) cuts[n++] = k->t - k->h2;
  if (k->t + k->h2 > x0 && k->t + k->h2 < x1) cuts[n++] = k->t + k->h2;
  cuts[n++] = x1;
  for (i = 0; i + 1 < n; i++) {
    float l = cuts[i], r = cuts[i + 1], m = 0.5f * (l + r);
    float wl = sleeve__weight(k, l), wm = sleeve__weight(k, m), wr = sleeve__weight(k, r);
    float s = (r - l) * (1.0f / 6.0f);
    for (ch = 0; ch < 4; ch++) {
      float fl = c0[ch] + slope[ch] * (l - origin);
      float fm = c0[ch] + slope[ch] * (m - origin);
      float fr = c0[ch] + slope[ch] * (r - origin);
      acc[ch] += s * (fl * wl + 4.0f * fm * wm + fr * wr);
    }
  }
}

/* Adds the gradient's pieces shifted by `offset`, over the kernel's support. */
static void sleeve__pieces(const sleeve__ready *g, const sleeve__kernel *k, float offset,
                           float acc[4]) {
  static const float flat[4] = {0, 0, 0, 0};
  float slope[4];
  int i, ch;
  for (i = 0; i + 1 < g->n; i++) {
    float len = g->at[i + 1] - g->at[i];
    if (len <= 0) continue;
    for (ch = 0; ch < 4; ch++) slope[ch] = (g->c[i + 1][ch] - g->c[i][ch]) / len;
    sleeve__piece(k, g->at[i] + offset, g->at[i + 1] + offset, g->at[i] + offset, g->c[i], slope,
                  acc);
  }
  if (!g->repeating) {
    float lo = k->t - k->h1, hi = k->t + k->h1;
    if (lo < g->at[0]) sleeve__piece(k, lo, g->at[0], lo, g->c[0], flat, acc);
    if (hi > g->at[g->n - 1])
      sleeve__piece(k, g->at[g->n - 1], hi, hi, g->c[g->n - 1], flat, acc);
  }
}

/* The gradient's colour over a pixel whose footprint along it is a box a wide convolved with a
   box b wide, centred on t: exact for a linear gradient, and for a curved one wherever it is
   locally straight. */
static void sleeve__sample(const sleeve__ready *g, float t, float a, float b, float out[4]) {
  float h, lo, hi, local = t;
  int i, ch;
  if (a < b) {
    float s = a;
    a = b;
    b = s;
  }
  h = 0.5f * (a + b);
  if (g->repeating) local = t - g->period * sleeve__floor(t / g->period);
  /* the support within one piece: the colour is linear there, so its average is its middle */
  lo = local - h;
  hi = local + h;
  if (g->repeating ? (lo >= 0 && hi <= g->period) : 1) {
    float below = g->repeating ? 0 : -1e30f, above = g->repeating ? g->period : 1e30f;
    for (i = 0; i < g->n; i++) {
      if (g->at[i] <= local) below = g->at[i] > below ? g->at[i] : below;
      else {
        above = g->at[i];
        break;
      }
    }
    if (lo >= below && hi <= above) {
      sleeve__point(g, local, out);
      return;
    }
  }
  if (a <= 0) {
    sleeve__point(g, local, out);
    return;
  }
  {
    sleeve__kernel k;
    float acc[4] = {0, 0, 0, 0};
    if (b < a * 1e-4f) b = 0;
    k.t = t;
    k.h1 = 0.5f * (a + b);
    k.h2 = 0.5f * (a - b);
    k.inv_a = 1.0f / a;
    k.inv_ab = b > 0 ? 1.0f / (a * b) : 0;
    if (g->repeating) {
      float first = sleeve__floor((t - k.h1) / g->period);
      float last = sleeve__floor((t + k.h1) / g->period);
      if (last - first > 32) {
        for (ch = 0; ch < 4; ch++) out[ch] = g->mean[ch];
        return;
      }
      for (; first <= last; first += 1) sleeve__pieces(g, &k, first * g->period, acc);
    } else {
      sleeve__pieces(g, &k, 0, acc);
    }
    for (ch = 0; ch < 4; ch++) out[ch] = acc[ch];
  }
}

/* A radial layer over a footprint fw x fh at offset (u, v) from its centre, distance t. */
static void sleeve__radial_sample(const sleeve__ready *g, float u, float v, float t, float fw,
                                  float fh, float out[4]) {
  float a = fw, b = fh;
  if (t > 0) {
    a = (u < 0 ? -u : u) / t * fw;
    b = (v < 0 ? -v : v) / t * fh;
  }
  sleeve__sample(g, t, a, b, out);
}

static void sleeve__prepare(const sleeve_layer *l, sleeve__ready *g) {
  int i, ch;
  const float unit = 1.0f / SLEEVE_UNIT;
  g->kind = l->kind;
  g->repeating = l->repeating;
  g->n = l->stop_count;
  for (i = 0; i < g->n; i++) {
    const sleeve_colour *c = &l->stops[i].colour;
    float alpha = c->a * (1.0f / 255.0f);
    g->at[i] = (float)l->stops[i].at * unit;
    g->c[i][0] = c->r * alpha;
    g->c[i][1] = c->g * alpha;
    g->c[i][2] = c->b * alpha;
    g->c[i][3] = (float)c->a;
  }
  g->period = g->at[g->n - 1];
  for (ch = 0; ch < 4; ch++) g->mean[ch] = 0;
  if (g->repeating) {
    for (i = 0; i + 1 < g->n; i++)
      for (ch = 0; ch < 4; ch++)
        g->mean[ch] += 0.5f * (g->c[i][ch] + g->c[i + 1][ch]) * (g->at[i + 1] - g->at[i]);
    for (ch = 0; ch < 4; ch++) g->mean[ch] /= g->period;
  }
  g->dx = (float)sleeve__sin(l->angle) * (1.0f / 65536.0f);
  g->dy = -(float)sleeve__cos(l->angle) * (1.0f / 65536.0f);
  {
    float len = (g->dx < 0 ? -g->dx : g->dx) + (g->dy < 0 ? -g->dy : g->dy);
    /* the line starts half its length before the centre */
    g->start = 0.5f * g->dx + 0.5f * g->dy - 0.5f * len;
  }
  g->cx = (float)l->cx * unit;
  g->cy = (float)l->cy * unit;
  g->k = l->ey > 0 ? (float)l->ex / (float)l->ey : 1.0f;
  g->tile = (float)l->ex * unit;
  g->reach = -1;
  if (l->kind == SLEEVE_RADIAL && !l->repeating && l->stops[g->n - 1].colour.a == 0) {
    float reach = g->at[0];
    for (i = 0; i < g->n; i++)
      if (l->stops[i].colour.a != 0 && i + 1 < g->n) reach = g->at[i + 1];
    g->reach = reach;
  }
}

/* The share of [x0, x1] that falls on odd squares of a row of squares `tile` wide from 0. */
static float sleeve__odd_share(float x0, float x1, float tile) {
  float q0 = x0 / tile, q1 = x1 / tile, n0 = sleeve__floor(q0), n1 = sleeve__floor(q1);
  float h0 = sleeve__floor(n0 * 0.5f), h1 = sleeve__floor(n1 * 0.5f);
  float f0 = h0 + (n0 - 2 * h0 > 0.5f ? q0 - n0 : 0);
  float f1 = h1 + (n1 - 2 * h1 > 0.5f ? q1 - n1 : 0);
  return (f1 - f0) / (q1 - q0);
}

int sleeve_render_rows(const sleeve_cover *cover, int width, int height, int row0, int row1,
                       uint8_t *rgba, ptrdiff_t stride) {
  sleeve__ready ready[SLEEVE_MAX_LAYERS];
  int count, i, x, y, ch;
  float inv_w, inv_h;
  if (sleeve_check(cover) != 0 || width < 1 || height < 1 || width > 16384 || height > 16384)
    return -1;
  if (row0 < 0) row0 = 0;
  if (row1 > height) row1 = height;
  count = cover->layer_count;
  for (i = 0; i < count; i++) sleeve__prepare(&cover->layers[i], &ready[i]);
  inv_w = 1.0f / (float)width;
  inv_h = 1.0f / (float)height;
  for (y = row0; y < row1; y++) {
    uint8_t *row = rgba + (ptrdiff_t)y * stride;
    float py = ((float)y + 0.5f) * inv_h;
    int lo[SLEEVE_MAX_LAYERS], hi[SLEEVE_MAX_LAYERS];
    /* the columns where each radial layer with a reach can show */
    for (i = 0; i < count; i++) {
      const sleeve__ready *g = &ready[i];
      lo[i] = 0;
      hi[i] = width;
      if (g->kind == SLEEVE_RADIAL && g->reach >= 0) {
        float v = (py - g->cy) * g->k, margin = inv_w + g->k * inv_h;
        float r = g->reach + margin, span;
        if (v < 0) v = -v;
        if (v >= r) {
          hi[i] = 0;
          continue;
        }
        span = sleeve__sqrt(r * r - v * v);
        {
          float first = (g->cx - span) * (float)width - 1.0f;
          float last = (g->cx + span) * (float)width + 1.0f;
          lo[i] = first <= 0 ? 0 : (first >= (float)width ? width : (int)first);
          hi[i] = last <= 0 ? 0 : (last >= (float)width ? width : (int)last + 1);
        }
      }
    }
    for (x = 0; x < width; x++) {
      float px = ((float)x + 0.5f) * inv_w;
      float acc[4], src[4];
      acc[0] = cover->base.r;
      acc[1] = cover->base.g;
      acc[2] = cover->base.b;
      acc[3] = 255.0f;
      for (i = 0; i < count; i++) {
        const sleeve__ready *g = &ready[i];
        float keep;
        if (x < lo[i] || x >= hi[i]) continue;
        if (g->kind == SLEEVE_LINEAR) {
          float t = px * g->dx + py * g->dy - g->start;
          float a = (g->dx < 0 ? -g->dx : g->dx) * inv_w;
          float b = (g->dy < 0 ? -g->dy : g->dy) * inv_h;
          sleeve__sample(g, t, a, b, src);
        } else if (g->kind == SLEEVE_RADIAL) {
          float u = px - g->cx, v = (py - g->cy) * g->k;
          float t = sleeve__sqrt(u * u + v * v);
          float reach = inv_w + g->k * inv_h;
          if (t >= 2 * reach) {
            sleeve__radial_sample(g, u, v, t, inv_w, g->k * inv_h, src);
          } else {
            /* near the centre the rings curve within the pixel, so it is split in 4 x 4 */
            int sx, sy;
            for (ch = 0; ch < 4; ch++) src[ch] = 0;
            for (sy = 0; sy < 4; sy++)
              for (sx = 0; sx < 4; sx++) {
                float part[4];
                float su = u + ((float)sx - 1.5f) * 0.25f * inv_w;
                float sv = v + ((float)sy - 1.5f) * 0.25f * inv_h * g->k;
                sleeve__radial_sample(g, su, sv, sleeve__sqrt(su * su + sv * sv), 0.25f * inv_w,
                                      0.25f * g->k * inv_h, part);
                for (ch = 0; ch < 4; ch++) src[ch] += part[ch] * (1.0f / 16.0f);
              }
          }
        } else {
          float sx = sleeve__odd_share(px - 0.5f * inv_w - g->cx, px + 0.5f * inv_w - g->cx,
                                       g->tile);
          float sy = sleeve__odd_share(py - 0.5f * inv_h - g->cy, py + 0.5f * inv_h - g->cy,
                                       g->tile);
          float odd = sx * (1 - sy) + sy * (1 - sx);
          for (ch = 0; ch < 4; ch++) src[ch] = g->c[0][ch] + (g->c[1][ch] - g->c[0][ch]) * odd;
        }
        keep = 1.0f - src[3] * (1.0f / 255.0f);
        for (ch = 0; ch < 4; ch++) acc[ch] = src[ch] + acc[ch] * keep;
      }
      for (ch = 0; ch < 3; ch++) {
        float v = acc[ch] + 0.5f;
        row[x * 4 + ch] = (uint8_t)(v <= 0 ? 0 : (v >= 255 ? 255 : (int)v));
      }
      row[x * 4 + 3] = 255;
    }
  }
  return 0;
}

int sleeve_render(const sleeve_cover *cover, int width, int height, uint8_t *rgba,
                  ptrdiff_t stride) {
  return sleeve_render_rows(cover, width, height, 0, height, rgba, stride);
}

/* ---- Text output ------------------------------------------------------------------------- */

typedef struct sleeve__text {
  char *out;
  size_t capacity, length;
} sleeve__text;

static void sleeve__put(sleeve__text *t, char c) {
  if (t->length + 1 < t->capacity) t->out[t->length] = c;
  t->length++;
}

static void sleeve__puts(sleeve__text *t, const char *s) {
  while (*s) sleeve__put(t, *s++);
}

static void sleeve__int(sleeve__text *t, int64_t v) {
  char digits[24];
  int n = 0;
  uint64_t u;
  if (v < 0) {
    sleeve__put(t, '-');
    u = (uint64_t)(-(v + 1)) + 1;
  } else {
    u = (uint64_t)v;
  }
  do {
    digits[n++] = (char)('0' + u % 10);
    u /= 10;
  } while (u);
  while (n) sleeve__put(t, digits[--n]);
}

/* num / den, for den > 0, rounded to `places` decimals with trailing zeros dropped. */
static void sleeve__ratio(sleeve__text *t, int64_t num, int64_t den, int places) {
  int64_t scale = 1, scaled, whole, frac;
  int i;
  for (i = 0; i < places; i++) scale *= 10;
  scaled = sleeve__div_round(num * scale, den);
  if (scaled < 0) {
    sleeve__put(t, '-');
    scaled = -scaled;
  }
  whole = scaled / scale;
  frac = scaled % scale;
  sleeve__int(t, whole);
  if (frac) {
    char digits[20];
    int n = places;
    sleeve__put(t, '.');
    for (i = places - 1; i >= 0; i--) {
      digits[i] = (char)('0' + frac % 10);
      frac /= 10;
    }
    while (n > 0 && digits[n - 1] == '0') n--;
    for (i = 0; i < n; i++) sleeve__put(t, digits[i]);
  }
}

static void sleeve__hex(sleeve__text *t, sleeve_colour c, int alpha) {
  static const char hex[] = "0123456789abcdef";
  sleeve__put(t, '#');
  sleeve__put(t, hex[c.r >> 4]);
  sleeve__put(t, hex[c.r & 15]);
  sleeve__put(t, hex[c.g >> 4]);
  sleeve__put(t, hex[c.g & 15]);
  sleeve__put(t, hex[c.b >> 4]);
  sleeve__put(t, hex[c.b & 15]);
  if (alpha) {
    sleeve__put(t, hex[c.a >> 4]);
    sleeve__put(t, hex[c.a & 15]);
  }
}

static size_t sleeve__finish(sleeve__text *t) {
  if (t->capacity > 0) t->out[t->length < t->capacity ? t->length : t->capacity - 1] = 0;
  return t->length;
}

static const char *const sleeve__composition_names[SLEEVE_COMPOSITIONS] = {
  "horizon", "glow", "disc", "pair", "stripes", "bands",
  "split", "bars", "constellation", "grooves", "orbs", "checks",
};

static const char *const sleeve__mood_names[SLEEVE_MOODS] = {
  "dusk", "sea", "earth", "paper-warm", "paper-cool", "neon", "mist", "garden", "duotone",
  "ember",
};

const char *sleeve_composition_name(int composition) {
  return composition >= 0 && composition < SLEEVE_COMPOSITIONS
             ? sleeve__composition_names[composition] : "unknown";
}

const char *sleeve_mood_name(int mood) {
  return mood >= 0 && mood < SLEEVE_MOODS ? sleeve__mood_names[mood] : "unknown";
}

size_t sleeve_describe(const sleeve_cover *cover, char *out, size_t capacity) {
  sleeve__text t;
  int i, j;
  t.out = out;
  t.capacity = capacity;
  t.length = 0;
  sleeve__puts(&t, "sleeve ");
  sleeve__int(&t, cover->generator);
  if (cover->generator) {
    sleeve__puts(&t, " ");
    sleeve__puts(&t, sleeve_composition_name(cover->composition));
    sleeve__puts(&t, " ");
    sleeve__puts(&t, sleeve_mood_name(cover->mood));
  }
  sleeve__puts(&t, "\npalette");
  for (i = 0; i < cover->palette_count && i < 5; i++) {
    sleeve__put(&t, ' ');
    sleeve__hex(&t, cover->palette[i], 0);
  }
  sleeve__puts(&t, "\nbase ");
  sleeve__hex(&t, cover->base, 0);
  sleeve__put(&t, '\n');
  for (i = 0; i < cover->layer_count && i < SLEEVE_MAX_LAYERS; i++) {
    const sleeve_layer *l = &cover->layers[i];
    if (l->repeating) sleeve__puts(&t, "repeating ");
    if (l->kind == SLEEVE_LINEAR) {
      sleeve__puts(&t, "linear ");
      sleeve__int(&t, l->angle);
    } else {
      sleeve__puts(&t, l->kind == SLEEVE_RADIAL ? "radial " : "checker ");
      sleeve__int(&t, l->cx);
      sleeve__put(&t, ' ');
      sleeve__int(&t, l->cy);
      sleeve__put(&t, ' ');
      sleeve__int(&t, l->ex);
      if (l->kind == SLEEVE_RADIAL) {
        sleeve__put(&t, ' ');
        sleeve__int(&t, l->ey);
      }
    }
    sleeve__puts(&t, " :");
    for (j = 0; j < l->stop_count && j < SLEEVE_MAX_STOPS; j++) {
      sleeve__put(&t, ' ');
      sleeve__hex(&t, l->stops[j].colour, 1);
      sleeve__put(&t, '@');
      sleeve__int(&t, l->stops[j].at);
    }
    sleeve__put(&t, '\n');
  }
  return sleeve__finish(&t);
}

/* ---- SVG --------------------------------------------------------------------------------- */

static void sleeve__svg_stops(sleeve__text *t, const sleeve_layer *l, int32_t length) {
  int j;
  for (j = 0; j < l->stop_count; j++) {
    sleeve_colour c = l->stops[j].colour;
    sleeve__puts(t, "<stop offset=\"");
    sleeve__ratio(t, l->stops[j].at, length, 6);
    sleeve__puts(t, "\" stop-color=\"");
    sleeve__hex(t, c, 0);
    sleeve__put(t, '"');
    if (c.a != 255) {
      sleeve__puts(t, " stop-opacity=\"");
      sleeve__ratio(t, c.a, 255, 5);
      sleeve__put(t, '"');
    }
    sleeve__puts(t, "/>");
  }
}

static void sleeve__svg_attr(sleeve__text *t, const char *name, int64_t num, int64_t den) {
  sleeve__put(t, ' ');
  sleeve__puts(t, name);
  sleeve__puts(t, "=\"");
  sleeve__ratio(t, num, den, 3);
  sleeve__put(t, '"');
}

static void sleeve__svg_fill(sleeve__text *t, sleeve_colour c) {
  sleeve__puts(t, " fill=\"");
  sleeve__hex(t, c, 0);
  sleeve__put(t, '"');
  if (c.a != 255) {
    sleeve__puts(t, " fill-opacity=\"");
    sleeve__ratio(t, c.a, 255, 5);
    sleeve__put(t, '"');
  }
}

static int64_t sleeve__floor_div(int64_t n, int64_t d) {
  int64_t q = n / d;
  return q * d > n ? q - 1 : q;
}

/* How many of a checker's squares meet the cover. */
static int64_t sleeve__checker_cells(const sleeve_layer *l) {
  int64_t across = sleeve__floor_div(SLEEVE_UNIT - l->cx, l->ex) - sleeve__floor_div(-l->cx, l->ex) + 1;
  int64_t down = sleeve__floor_div(SLEEVE_UNIT - l->cy, l->ex) - sleeve__floor_div(-l->cy, l->ex) + 1;
  return across * down;
}

/* A checker's squares of one parity that meet the cover, as one path. */
static void sleeve__svg_squares(sleeve__text *t, const sleeve_layer *l, int parity) {
  int64_t s = l->ex, i, j;
  int64_t i0 = sleeve__floor_div(-l->cx, s), i1 = sleeve__floor_div(SLEEVE_UNIT - l->cx, s);
  int64_t j0 = sleeve__floor_div(-l->cy, s), j1 = sleeve__floor_div(SLEEVE_UNIT - l->cy, s);
  sleeve__puts(t, "<path d=\"");
  for (j = j0; j <= j1; j++)
    for (i = i0; i <= i1; i++) {
      if (((i + j) & 1) != parity) continue;
      sleeve__put(t, 'M');
      sleeve__int(t, l->cx + i * s);
      sleeve__put(t, ' ');
      sleeve__int(t, l->cy + j * s);
      sleeve__put(t, 'h');
      sleeve__int(t, s);
      sleeve__put(t, 'v');
      sleeve__int(t, s);
      sleeve__put(t, 'h');
      sleeve__int(t, -s);
      sleeve__put(t, 'z');
    }
  sleeve__put(t, '"');
  sleeve__svg_fill(t, l->stops[parity].colour);
  sleeve__puts(t, "/>\n");
}

size_t sleeve_svg(const sleeve_cover *cover, int width, int height, char *out, size_t capacity) {
  sleeve__text t;
  int i;
  t.out = out;
  t.capacity = capacity;
  t.length = 0;
  if (sleeve_check(cover) != 0 || width < 1 || height < 1) {
    if (capacity > 0) out[0] = 0;
    return 0;
  }
  sleeve__puts(&t, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"");
  sleeve__int(&t, width);
  sleeve__puts(&t, "\" height=\"");
  sleeve__int(&t, height);
  sleeve__puts(&t, "\" viewBox=\"0 0 10000 10000\" preserveAspectRatio=\"none\">\n<defs>\n");
  for (i = 0; i < cover->layer_count; i++) {
    const sleeve_layer *l = &cover->layers[i];
    int32_t end = l->stops[l->stop_count - 1].at;
    if (end <= 0) end = 1;
    if (l->kind == SLEEVE_LINEAR) {
      /* the line's start, and its end at the last stop, in units of 1/(2 * 65536^2) */
      int64_t sn = sleeve__sin(l->angle), cs = sleeve__cos(l->angle);
      int64_t len = (sn < 0 ? -sn : sn) + (cs < 0 ? -cs : cs);
      int64_t den = (int64_t)2 * 65536 * 65536;
      int64_t x1 = ((int64_t)65536 * 65536 - sn * len) * SLEEVE_UNIT;
      int64_t y1 = ((int64_t)65536 * 65536 + cs * len) * SLEEVE_UNIT;
      int64_t x2 = x1 + sn * end * 2 * 65536, y2 = y1 - cs * end * 2 * 65536;
      sleeve__puts(&t, "<linearGradient id=\"l");
      sleeve__int(&t, i);
      sleeve__puts(&t, "\" gradientUnits=\"userSpaceOnUse\"");
      sleeve__svg_attr(&t, "x1", x1, den);
      sleeve__svg_attr(&t, "y1", y1, den);
      sleeve__svg_attr(&t, "x2", x2, den);
      sleeve__svg_attr(&t, "y2", y2, den);
      if (l->repeating) sleeve__puts(&t, " spreadMethod=\"repeat\"");
      sleeve__puts(&t, ">");
      sleeve__svg_stops(&t, l, end);
      sleeve__puts(&t, "</linearGradient>\n");
    } else if (l->kind == SLEEVE_RADIAL) {
      sleeve__puts(&t, "<radialGradient id=\"l");
      sleeve__int(&t, i);
      sleeve__puts(&t, "\" gradientUnits=\"userSpaceOnUse\"");
      sleeve__svg_attr(&t, "cx", l->cx, 1);
      sleeve__svg_attr(&t, "cy", l->cy, 1);
      sleeve__svg_attr(&t, "fx", l->cx, 1);
      sleeve__svg_attr(&t, "fy", l->cy, 1);
      sleeve__svg_attr(&t, "r", end, 1);
      if (l->ex != l->ey) {
        /* distance is measured with y scaled by ex / ey, so the circle is stretched by ey / ex */
        sleeve__puts(&t, " gradientTransform=\"matrix(1 0 0 ");
        sleeve__ratio(&t, l->ey, l->ex, 6);
        sleeve__puts(&t, " 0 ");
        sleeve__ratio(&t, (int64_t)l->cy * (l->ex - l->ey), l->ex, 3);
        sleeve__puts(&t, ")\"");
      }
      if (l->repeating) sleeve__puts(&t, " spreadMethod=\"repeat\"");
      sleeve__puts(&t, ">");
      sleeve__svg_stops(&t, l, end);
      sleeve__puts(&t, "</radialGradient>\n");
    } else if (sleeve__checker_cells(l) > 4096) {
      /* squares too small to list are a pattern, which renderers may resample */
      int32_t side = l->ex;
      int q;
      sleeve__puts(&t, "<pattern id=\"l");
      sleeve__int(&t, i);
      sleeve__puts(&t, "\" patternUnits=\"userSpaceOnUse\"");
      sleeve__svg_attr(&t, "x", l->cx, 1);
      sleeve__svg_attr(&t, "y", l->cy, 1);
      sleeve__svg_attr(&t, "width", 2 * (int64_t)side, 1);
      sleeve__svg_attr(&t, "height", 2 * (int64_t)side, 1);
      sleeve__puts(&t, ">");
      for (q = 0; q < 4; q++) {
        int col = q & 1, row = q >> 1;
        sleeve__puts(&t, "<rect");
        sleeve__svg_attr(&t, "x", (int64_t)col * side, 1);
        sleeve__svg_attr(&t, "y", (int64_t)row * side, 1);
        sleeve__svg_attr(&t, "width", side, 1);
        sleeve__svg_attr(&t, "height", side, 1);
        sleeve__svg_fill(&t, l->stops[(col + row) & 1].colour);
        sleeve__puts(&t, "/>");
      }
      sleeve__puts(&t, "</pattern>\n");
    }
  }
  sleeve__puts(&t, "</defs>\n<rect width=\"10000\" height=\"10000\"");
  sleeve__svg_fill(&t, cover->base);
  sleeve__puts(&t, "/>\n");
  for (i = 0; i < cover->layer_count; i++) {
    const sleeve_layer *l = &cover->layers[i];
    if (l->kind == SLEEVE_CHECKER && sleeve__checker_cells(l) <= 4096) {
      /* the squares themselves: an opaque first colour is drawn whole beneath the second's
         squares, so the two share no edge for a renderer to blend twice */
      if (l->stops[0].colour.a == 255) {
        sleeve__puts(&t, "<rect width=\"10000\" height=\"10000\"");
        sleeve__svg_fill(&t, l->stops[0].colour);
        sleeve__puts(&t, "/>\n");
      } else {
        sleeve__svg_squares(&t, l, 0);
      }
      sleeve__svg_squares(&t, l, 1);
      continue;
    }
    sleeve__puts(&t, "<rect width=\"10000\" height=\"10000\" fill=\"url(#l");
    sleeve__int(&t, i);
    sleeve__puts(&t, ")\"/>\n");
  }
  sleeve__puts(&t, "</svg>\n");
  return sleeve__finish(&t);
}

/* ---- PNG --------------------------------------------------------------------------------- */

typedef struct sleeve__bits {
  uint8_t *out;
  size_t capacity, length;
  uint32_t buffer;
  int count, overflow;
} sleeve__bits;

static void sleeve__byte(sleeve__bits *b, uint8_t v) {
  if (b->length < b->capacity) b->out[b->length] = v;
  else b->overflow = 1;
  b->length++;
}

static void sleeve__u32(sleeve__bits *b, uint32_t v) {
  sleeve__byte(b, (uint8_t)(v >> 24));
  sleeve__byte(b, (uint8_t)(v >> 16));
  sleeve__byte(b, (uint8_t)(v >> 8));
  sleeve__byte(b, (uint8_t)v);
}

/* Writes the low `n` bits of v, least significant first, as deflate packs them. */
static void sleeve__bits_put(sleeve__bits *b, uint32_t v, int n) {
  b->buffer |= v << b->count;
  b->count += n;
  while (b->count >= 8) {
    sleeve__byte(b, (uint8_t)b->buffer);
    b->buffer >>= 8;
    b->count -= 8;
  }
}

/* Writes a Huffman code of n bits, most significant first. */
static void sleeve__code(sleeve__bits *b, uint32_t code, int n) {
  uint32_t reversed = 0;
  int i;
  for (i = 0; i < n; i++) reversed |= ((code >> i) & 1u) << (n - 1 - i);
  sleeve__bits_put(b, reversed, n);
}

static void sleeve__literal(sleeve__bits *b, int v) {
  if (v < 144) sleeve__code(b, 0x30u + (uint32_t)v, 8);
  else if (v < 256) sleeve__code(b, 0x190u + (uint32_t)(v - 144), 9);
  else if (v < 280) sleeve__code(b, (uint32_t)(v - 256), 7);
  else sleeve__code(b, 0xc0u + (uint32_t)(v - 280), 8);
}

/* A match of `length` bytes (3 to 258) at distance 1. */
static void sleeve__match(sleeve__bits *b, int length) {
  static const uint16_t base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  int i = 28;
  while (base[i] > length) i--;
  sleeve__literal(b, 257 + i);
  if (extra[i]) sleeve__bits_put(b, (uint32_t)(length - base[i]), extra[i]);
  sleeve__code(b, 0, 5);
}

static uint32_t sleeve__crc(uint32_t crc, const uint8_t *p, size_t n) {
  static const uint32_t table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
  };
  size_t i;
  crc = ~crc;
  for (i = 0; i < n; i++) {
    crc = (crc >> 4) ^ table[(crc ^ p[i]) & 15];
    crc = (crc >> 4) ^ table[(crc ^ (uint32_t)(p[i] >> 4)) & 15];
  }
  return ~crc;
}

/* The filtered byte at column i (counting RGB bytes) of row y, under PNG filter f. */
static uint8_t sleeve__filtered(const uint8_t *rgba, ptrdiff_t stride, int y, int i, int f) {
  const uint8_t *row = rgba + (ptrdiff_t)y * stride;
  int px = i / 3, ch = i % 3;
  int cur = row[px * 4 + ch];
  int left = px > 0 ? row[(px - 1) * 4 + ch] : 0;
  int up = y > 0 ? row[px * 4 + ch - stride] : 0;
  int corner = px > 0 && y > 0 ? row[(px - 1) * 4 + ch - stride] : 0;
  int pred = 0;
  if (f == 1) pred = left;
  else if (f == 2) pred = up;
  else if (f == 3) pred = (left + up) / 2;
  else if (f == 4) {
    int p = left + up - corner;
    int pa = p > left ? p - left : left - p, pb = p > up ? p - up : up - p;
    int pc = p > corner ? p - corner : corner - p;
    pred = pa <= pb && pa <= pc ? left : (pb <= pc ? up : corner);
  }
  return (uint8_t)(cur - pred);
}

size_t sleeve_png_bound(int width, int height) {
  size_t raw;
  if (width < 1 || height < 1 || width > 16384 || height > 16384) return 0;
  raw = (size_t)height * (1 + (size_t)width * 3);
  /* fixed-Huffman literals take at most 9 bits */
  return raw + raw / 8 + 128;
}

size_t sleeve_png(const uint8_t *rgba, int width, int height, ptrdiff_t stride, uint8_t *out,
                  size_t capacity) {
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  sleeve__bits b;
  size_t chunk, i;
  uint32_t adler_a = 1, adler_b = 0;
  int y, x, n = width * 3, run = 0, previous = -1;
  if (width < 1 || height < 1 || width > 16384 || height > 16384) return 0;
  b.out = out;
  b.capacity = capacity;
  b.length = 0;
  b.buffer = 0;
  b.count = 0;
  b.overflow = 0;
  for (i = 0; i < 8; i++) sleeve__byte(&b, signature[i]);
  sleeve__u32(&b, 13);
  chunk = b.length;
  sleeve__u32(&b, 0x49484452); /* IHDR */
  sleeve__u32(&b, (uint32_t)width);
  sleeve__u32(&b, (uint32_t)height);
  sleeve__byte(&b, 8);
  sleeve__byte(&b, 2);
  sleeve__byte(&b, 0);
  sleeve__byte(&b, 0);
  sleeve__byte(&b, 0);
  if (!b.overflow) sleeve__u32(&b, sleeve__crc(0, out + chunk, b.length - chunk));
  else sleeve__u32(&b, 0);
  /* IDAT: its length is filled in once known */
  sleeve__u32(&b, 0);
  chunk = b.length;
  sleeve__u32(&b, 0x49444154);
  sleeve__byte(&b, 0x78);
  sleeve__byte(&b, 0x01);
  sleeve__bits_put(&b, 1, 1); /* the final block */
  sleeve__bits_put(&b, 1, 2); /* fixed Huffman codes */
  for (y = 0; y < height; y++) {
    /* the filter whose bytes sum smallest as signed values */
    int best = 0, f;
    long best_sum = -1;
    for (f = 0; f < 5; f++) {
      long sum = 0;
      for (x = 0; x < n; x++) {
        int v = sleeve__filtered(rgba, stride, y, x, f);
        sum += v < 128 ? v : 256 - v;
      }
      if (best_sum < 0 || sum < best_sum) {
        best_sum = sum;
        best = f;
      }
    }
    for (x = -1; x < n; x++) {
      int v = x < 0 ? best : sleeve__filtered(rgba, stride, y, x, best);
      adler_a = (adler_a + (uint32_t)v) % 65521;
      adler_b = (adler_b + adler_a) % 65521;
      /* a byte equal to the one before extends a run, emitted as matches at distance 1 */
      if (v == previous && run < 258) {
        run++;
        continue;
      }
      if (run >= 3) sleeve__match(&b, run);
      else
        while (run-- > 0) sleeve__literal(&b, previous);
      run = 0;
      sleeve__literal(&b, v);
      previous = v;
    }
  }
  if (run >= 3) sleeve__match(&b, run);
  else
    while (run-- > 0) sleeve__literal(&b, previous);
  sleeve__literal(&b, 256);
  if (b.count > 0) sleeve__bits_put(&b, 0, 8 - b.count);
  sleeve__u32(&b, (adler_b << 16) | adler_a);
  if (!b.overflow) {
    uint32_t length = (uint32_t)(b.length - chunk - 4);
    out[chunk - 4] = (uint8_t)(length >> 24);
    out[chunk - 3] = (uint8_t)(length >> 16);
    out[chunk - 2] = (uint8_t)(length >> 8);
    out[chunk - 1] = (uint8_t)length;
    sleeve__u32(&b, sleeve__crc(0, out + chunk, b.length - chunk));
  } else {
    sleeve__u32(&b, 0);
  }
  sleeve__u32(&b, 0);
  chunk = b.length;
  sleeve__u32(&b, 0x49454e44); /* IEND */
  if (!b.overflow) sleeve__u32(&b, sleeve__crc(0, out + chunk, 4));
  else sleeve__u32(&b, 0);
  return b.overflow ? 0 : b.length;
}

/* ---- Files ------------------------------------------------------------------------------- */

#ifndef SLEEVE_NO_STDIO
static int sleeve__write_file(const char *path, const void *bytes, size_t length) {
  FILE *f = fopen(path, "wb");
  int ok;
  if (!f) return -1;
  ok = fwrite(bytes, 1, length, f) == length;
  if (fclose(f) != 0) ok = 0;
  return ok ? 0 : -1;
}

int sleeve_write_png(const char *path, const sleeve_cover *cover, int width, int height) {
  size_t bound = sleeve_png_bound(width, height), length;
  uint8_t *pixels, *png;
  int result = -1;
  if (bound == 0) return -1;
  pixels = (uint8_t *)malloc((size_t)width * (size_t)height * 4);
  png = (uint8_t *)malloc(bound);
  if (pixels && png && sleeve_render(cover, width, height, pixels, (ptrdiff_t)width * 4) == 0) {
    length = sleeve_png(pixels, width, height, (ptrdiff_t)width * 4, png, bound);
    if (length > 0) result = sleeve__write_file(path, png, length);
  }
  free(pixels);
  free(png);
  return result;
}

int sleeve_write_svg(const char *path, const sleeve_cover *cover, int width, int height) {
  size_t length = sleeve_svg(cover, width, height, NULL, 0);
  char *svg;
  int result = -1;
  if (length == 0) return -1;
  svg = (char *)malloc(length + 1);
  if (svg && sleeve_svg(cover, width, height, svg, length + 1) == length)
    result = sleeve__write_file(path, svg, length);
  free(svg);
  return result;
}
#endif

#endif /* SLEEVE_IMPLEMENTED */
#endif /* SLEEVE_IMPLEMENTATION */
