/* sleeve.h: procedural stand-in album covers, as a single-file C library.

   A cover is a resolution-independent description: an opaque base colour and a short stack of
   paint layers (linear, radial and checker gradients, plain or repeating, with colour stops).
   sleeve_generate turns a 64-bit seed into a description; sleeve_render rasterizes one into an
   RGBA8 buffer of any size; sleeve_svg and sleeve_png write it out as files. A style, plain
   data with optional code hooks, shapes what generation draws, and a palette given for one
   call can take the place of its moods.

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

/* The generator's version: the default style's covers, and those of any style made of built-in
   pieces, stay the same for the same seed while it does. */
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
  int32_t angle;       /* linear: degrees, any, as its equal in [0, 360) */
  int32_t cx, cy;      /* radial: centre; checker: a corner */
  int32_t ex, ey;      /* radial: shape, both positive; checker: ex is the square's side */
  sleeve_stop stops[SLEEVE_MAX_STOPS];
} sleeve_layer;

typedef struct sleeve_cover {
  uint32_t generator;            /* SLEEVE_GENERATOR, or 0 for a cover made by hand */
  uint8_t composition;           /* an index into its style's compositions */
  uint8_t mood;                  /* an index into its style's moods, or SLEEVE_NO_MOOD */
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

/* The mood of a cover drawn from a palette given for the call. */
#define SLEEVE_NO_MOOD 255

/* A palette's five roles, the order of a cover's palette. */
enum sleeve_role {
  SLEEVE_DEEP, SLEEVE_MID, SLEEVE_PALE, SLEEVE_ACCENT, SLEEVE_ACCENT2,
  SLEEVE_ROLES
};

/* A stable 64-bit hash of bytes, for keying covers by an identity of the caller's choice. */
uint64_t sleeve_hash(const void *bytes, size_t length);

/* The cover for a seed, in the default style. */
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

/* A one-line-per-layer text form of a description, as sleeve_svg returns its length. Names
   come from the default style; sleeve_describe_styled takes them from the cover's style. */
size_t sleeve_describe(const sleeve_cover *cover, char *out, size_t capacity);

/* The default style's names for a cover's composition and mood. */
const char *sleeve_composition_name(int composition);
const char *sleeve_mood_name(int mood);

#ifndef SLEEVE_NO_STDIO
/* Renders and writes a cover as a PNG or an SVG file. Returns 0, or -1 on any failure. */
int sleeve_write_png(const char *path, const sleeve_cover *cover, int width, int height);
int sleeve_write_svg(const char *path, const sleeve_cover *cover, int width, int height);
#endif

/* ---- Styles ------------------------------------------------------------------------------
   A style shapes generation: the moods palettes are drawn from, the compositions and how often
   each is chosen, palette hooks that transform the five colours, and finishers that paint over
   the composition. Every part is plain data the caller owns; the README describes them. */

#define SLEEVE_STYLE_MAX_MOODS 32
#define SLEEVE_STYLE_MAX_COMPOSITIONS 32
#define SLEEVE_STYLE_MAX_HOOKS 8      /* palette hooks, and finishers, each */
#define SLEEVE_STYLE_MAX_RESERVED 6   /* layers finishers may reserve in all */
#define SLEEVE_MAX_GIVEN 16           /* colours in a palette given for one call */

/* The generator a cover's choices are drawn from: SplitMix64. */
typedef struct sleeve_rng {
  uint64_t state;
} sleeve_rng;

/* A cover being generated, as every hook receives it. Hooks read `palette` and the cover, add
   layers through the builder calls below, and draw choices only from the builder, which is
   what keeps a style's covers the same on every platform. */
typedef struct sleeve_builder {
  sleeve_cover *cover;                 /* the description being built */
  sleeve_rng rng;
  sleeve_colour palette[SLEEVE_ROLES]; /* palette hooks may change it; others read it */
  int limit;                           /* the layers the running hook may fill up to */
  sleeve_layer spare;                  /* where a layer beyond the limit goes, unseen */
} sleeve_builder;

/* A composition, palette hook or finisher: params is its parameter struct, or NULL for the
   built-in's defaults. */
typedef void (*sleeve_hook)(sleeve_builder *b, const void *params);

/* A palette role's recipe: a hue offset from the mood's hue in degrees (-360 to 360), and an
   OKLCH lightness (0 to 1000) and chroma (0 to 400), both in thousandths. A value is drawn
   from each range, low to high inclusive; a range of one value pins it. */
typedef struct sleeve_role_range {
  int16_t hue_lo, hue_hi, light_lo, light_hi, chroma_lo, chroma_hi;
} sleeve_role_range;

typedef struct sleeve_mood_def {
  const char *name;
  int16_t hue_lo, hue_hi;              /* hue_lo 0 to 359, hue_hi hue_lo to hue_lo + 359 */
  sleeve_role_range roles[SLEEVE_ROLES];
  sleeve_colour exact[SLEEVE_ROLES];   /* a role's colour as it is, when its alpha is 255 */
} sleeve_mood_def;

/* A mood in a style: how often it is chosen, and optionally its own composition weights, one
   for each of the style's compositions, which then replace their weights under this mood. */
typedef struct sleeve_style_mood {
  const sleeve_mood_def *mood;
  uint16_t weight;
  const uint16_t *composition_weights; /* or NULL */
} sleeve_style_mood;

/* A composition, palette hook or finisher in a style. A built-in piece is named by its `run`;
   any other function is a code hook, identified to sleeve_style_hash by its name and version,
   which its owner changes whenever the hook or its parameters would draw differently. */
typedef struct sleeve_style_part {
  const char *name;                    /* the name covers report; a code hook's identity */
  sleeve_hook run;
  const void *params;                  /* the parameter struct, or NULL for the defaults */
  uint16_t weight;                     /* compositions: how often it is chosen */
  uint8_t layers;                      /* finishers: the layers kept free for it */
  uint32_t version;                    /* code hooks: their owner's version */
} sleeve_style_part;

/* A style. A part with a count of 0 is the default style's; palette hooks and finishers are
   then none. Moods' composition weights index the style's compositions, so the default moods'
   own weights apply only together with the default compositions. */
typedef struct sleeve_style {
  const char *name;
  const sleeve_style_mood *moods;
  int mood_count;
  const sleeve_style_part *compositions;
  int composition_count;
  const sleeve_style_part *palette_hooks;
  int palette_hook_count;
  const sleeve_style_part *finishers;
  int finisher_count;
} sleeve_style;

/* The style sleeve_generate draws in: the ten moods and twelve compositions. */
extern const sleeve_style sleeve_default_style;

/* The built-in moods, in enum sleeve_mood's order. */
extern const sleeve_mood_def sleeve_moods[SLEEVE_MOODS];

/* The cover for a seed in a style that passes sleeve_style_check (NULL for the default). */
void sleeve_generate_styled(uint64_t seed, const sleeve_style *style, sleeve_cover *cover);

/* The same with the palette given, 1 to SLEEVE_MAX_GIVEN colours, in place of a mood's: the
   colours are assigned to the five roles, roles they leave empty are derived from them, and
   the style's palette hooks do not run. The cover's mood is SLEEVE_NO_MOOD. Returns 0, or -1
   with the cover untouched when the count is out of range. */
int sleeve_generate_with_palette(uint64_t seed, const sleeve_style *style,
                                 const sleeve_colour *colours, int count, sleeve_cover *cover);

/* The five roles a given palette is assigned to, as sleeve_generate_with_palette assigns
   them. Returns 0, or -1 when the count is out of range. */
int sleeve_assign_palette(const sleeve_colour *colours, int count,
                          sleeve_colour roles[SLEEVE_ROLES]);

/* 0 when a style is one generation accepts, otherwise -1 and the first problem found, as text
   in `reason` (NUL-terminated, truncated to the capacity; reason may be NULL). */
int sleeve_style_check(const sleeve_style *style, char *reason, size_t capacity);

/* A fingerprint of everything in a style that decides its covers: the moods' recipes and
   weights, the compositions' weights, the built-in pieces and their parameters, the code
   hooks' names and versions, the layers reserved, and SLEEVE_GENERATOR. Equal style data
   hashes equal on every platform; names of the style and its moods do not count. */
uint64_t sleeve_style_hash(const sleeve_style *style);

/* A style's names for a cover's composition and mood; "given" for SLEEVE_NO_MOOD. */
const char *sleeve_style_composition_name(const sleeve_style *style, int composition);
const char *sleeve_style_mood_name(const sleeve_style *style, int mood);

size_t sleeve_describe_styled(const sleeve_cover *cover, const sleeve_style *style, char *out,
                              size_t capacity);

/* ---- Built-in pieces ---------------------------------------------------------------------
   Every built-in composition, palette hook and finisher has a stable name and a parameter
   struct made only of int32_t fields, each listed with its range and default, so a style can
   be assembled from names and numbers alone. */

enum sleeve_piece_kind { SLEEVE_COMPOSE, SLEEVE_PALETTE, SLEEVE_FINISH };

typedef struct sleeve_param {
  const char *name;
  int32_t lo, hi, value;  /* the allowed range, inclusive, and the default */
  int8_t at_most;         /* another parameter's index this one must not exceed, or -1 */
} sleeve_param;

typedef struct sleeve_piece {
  const char *name;
  sleeve_hook run;
  const void *defaults;   /* the parameter struct of defaults */
  const sleeve_param *params;
  int param_count;        /* the struct's int32_t fields, in order */
  int layers;             /* finishers: the most layers it adds */
} sleeve_piece;

/* The built-in pieces of a kind, and their number in *count. */
const sleeve_piece *sleeve_pieces(int kind, int *count);
/* A built-in piece by kind and name, or NULL. */
const sleeve_piece *sleeve_find_piece(int kind, const char *name);
/* A parameter's index by name, or -1. */
int sleeve_find_param(const sleeve_piece *piece, const char *name);
/* A built-in mood by name, or NULL. */
const sleeve_mood_def *sleeve_find_mood(const char *name);

/* Compositions. Chances are percentages; lengths are in side units; a _min and _max pair
   draws from the range between them. Ranges and defaults are in sleeve_pieces. */
typedef struct sleeve_horizon_params {
  int32_t night;                    /* a deep sky over mid, the sun pale */
  int32_t level;                    /* a level horizon; the rest tilt */
  int32_t tilt;                     /* the steepest tilt, degrees */
  int32_t horizon_min, horizon_max; /* the horizon's height, thousandths down the sky */
  int32_t sun;                      /* a sun or moon */
  int32_t setting;                  /* of those, sinking behind the ground */
  int32_t sun_min, sun_max;         /* its radius */
  int32_t band;                     /* an accent band along the horizon */
  int32_t lines;                    /* lines in the ground */
} sleeve_horizon_params;

typedef struct sleeve_glow_params {
  int32_t dusk;                     /* a deep sky; the rest are dawn */
  int32_t radius_min, radius_max;   /* the glow's reach */
  int32_t alpha_min, alpha_max;     /* its opacity at the core, 0 to 255 */
  int32_t sun;                      /* a sun or moon above it */
} sleeve_glow_params;

typedef struct sleeve_disc_params {
  int32_t record;                   /* a record; the rest are rings */
  int32_t centred;                  /* centred, on each axis */
  int32_t label_min, label_max;     /* a record's label radius */
  int32_t rings_min, rings_max;     /* rings */
  int32_t halo;                     /* a thin ring around the rings */
  int32_t wash;                     /* a faint pale wash over it all */
} sleeve_disc_params;

typedef struct sleeve_pair_params {
  int32_t dark;                     /* a deep ground; the rest pale */
  int32_t spread_min, spread_max;   /* each reel's distance from the middle */
  int32_t hub_min, hub_max;         /* a reel's hub radius */
  int32_t strip;                    /* a strip behind the reels */
  int32_t spots;                    /* accent spots on the hubs */
} sleeve_pair_params;

typedef struct sleeve_stripes_params {
  int32_t solid;                    /* solid stripes; the rest fine lines over a wash */
  int32_t colours_min, colours_max; /* colours in a solid stripe's repeat */
  int32_t wobble;                   /* the most degrees off the chosen angle */
  int32_t disc;                     /* a disc over fine lines */
} sleeve_stripes_params;

typedef struct sleeve_bands_params {
  int32_t sheen;                    /* a sheen over a diagonal wash; the rest bands */
  int32_t bands_min, bands_max;     /* bands */
  int32_t thin;                     /* one band thin */
  int32_t tilt;                     /* bands off the square */
  int32_t disc;                     /* a disc over the bands */
} sleeve_bands_params;

typedef struct sleeve_split_params {
  int32_t angle_min, angle_max;     /* the split's angle, degrees */
  int32_t cross;                    /* a band across it */
  int32_t disc;                     /* a disc */
  int32_t disc_min, disc_max;       /* its radius */
} sleeve_split_params;

typedef struct sleeve_bars_params {
  int32_t vertical;                 /* upright bars; the rest across */
  int32_t bars_min, bars_max;       /* bars */
  int32_t tilt;                     /* bars off the square */
} sleeve_bars_params;

typedef struct sleeve_constellation_params {
  int32_t stars_min, stars_max;     /* stars, one more without a moon */
  int32_t moon;                     /* a moon */
  int32_t star_min, star_max;       /* a star's radius */
} sleeve_constellation_params;

typedef struct sleeve_grooves_params {
  int32_t lines;                    /* fine lines; the rest bands */
  int32_t two_tone;                 /* of bands, a ground and one colour; the rest three tones */
  int32_t disc;                     /* a disc */
} sleeve_grooves_params;

typedef struct sleeve_orbs_params {
  int32_t wash;                     /* a wash under the orbs */
  int32_t edge;                     /* an orb rising from an edge, each */
  int32_t oval;                     /* an orb drawn as an ellipse, each */
} sleeve_orbs_params;

typedef struct sleeve_checks_params {
  int32_t tile_min, tile_max;       /* a square's side */
  int32_t dark;                     /* deep squares; the rest mid */
  int32_t disc;                     /* a disc over the squares; the rest a band */
} sleeve_checks_params;

/* Palette hooks. Colours are 0xRRGGBB; amounts are thousandths. */
typedef struct sleeve_tint_params {
  int32_t colour;                   /* the colour mixed in, in sRGB */
  int32_t amount;
} sleeve_tint_params;

typedef struct sleeve_chroma_params {
  int32_t scale;                    /* each colour's OKLCH chroma, in thousandths of itself */
} sleeve_chroma_params;

typedef struct sleeve_monochrome_params {
  int32_t hue;                      /* every colour's hue, or -1 for the palette's own key hue */
} sleeve_monochrome_params;

typedef struct sleeve_lightness_params {
  int32_t lo, hi;                   /* OKLCH lightness 0 to 1000 maps onto lo to hi */
} sleeve_lightness_params;

typedef struct sleeve_duotone_params {
  int32_t dark, light;              /* the inks the darkest and lightest colours become */
} sleeve_duotone_params;

/* Finishers. Roles are enum sleeve_role. */
typedef struct sleeve_vignette_params {
  int32_t role, alpha;              /* the colour the edges darken toward, and its opacity */
  int32_t inner, outer;             /* where it starts and is full, from the centre */
} sleeve_vignette_params;

typedef struct sleeve_wash_params {
  int32_t role, alpha;              /* a flat colour over the whole cover */
} sleeve_wash_params;

typedef struct sleeve_frame_params {
  int32_t role, alpha;
  int32_t width;                    /* the border's width */
} sleeve_frame_params;

typedef struct sleeve_lines_params {
  int32_t role, alpha;
  int32_t angle;                    /* degrees */
  int32_t pitch, width;             /* line spacing and width */
} sleeve_lines_params;

/* The built-in pieces' functions, for a style or a hook to call directly. */
void sleeve_compose_horizon(sleeve_builder *b, const void *params);
void sleeve_compose_glow(sleeve_builder *b, const void *params);
void sleeve_compose_disc(sleeve_builder *b, const void *params);
void sleeve_compose_pair(sleeve_builder *b, const void *params);
void sleeve_compose_stripes(sleeve_builder *b, const void *params);
void sleeve_compose_bands(sleeve_builder *b, const void *params);
void sleeve_compose_split(sleeve_builder *b, const void *params);
void sleeve_compose_bars(sleeve_builder *b, const void *params);
void sleeve_compose_constellation(sleeve_builder *b, const void *params);
void sleeve_compose_grooves(sleeve_builder *b, const void *params);
void sleeve_compose_orbs(sleeve_builder *b, const void *params);
void sleeve_compose_checks(sleeve_builder *b, const void *params);
void sleeve_palette_tint(sleeve_builder *b, const void *params);
void sleeve_palette_chroma(sleeve_builder *b, const void *params);
void sleeve_palette_monochrome(sleeve_builder *b, const void *params);
void sleeve_palette_lightness(sleeve_builder *b, const void *params);
void sleeve_palette_duotone(sleeve_builder *b, const void *params);
void sleeve_finish_vignette(sleeve_builder *b, const void *params);
void sleeve_finish_wash(sleeve_builder *b, const void *params);
void sleeve_finish_frame(sleeve_builder *b, const void *params);
void sleeve_finish_lines(sleeve_builder *b, const void *params);

/* ---- The builder -------------------------------------------------------------------------
   What compositions and finishers paint with. Whatever a hook asks, the builder keeps the
   description within SLEEVE_MAX_LAYERS and SLEEVE_MAX_STOPS, and generation drops any layer
   sleeve_check would refuse, so every generated cover passes it. */

/* The generator: a number in [0, n), in [lo, hi], true `percent` times in 100, and an index
   chosen in proportion to n weights. Each call takes the next draw, even when it has only one
   answer (n of 1 or less, hi of lo or less, weights summing to 0). */
int32_t sleeve_below(sleeve_builder *b, int32_t n);
int32_t sleeve_range(sleeve_builder *b, int32_t lo, int32_t hi);
int sleeve_chance(sleeve_builder *b, int32_t percent);
int sleeve_pick(sleeve_builder *b, const uint16_t *weights, int n);

/* The layers the running hook can still add. */
int sleeve_layers_left(const sleeve_builder *b);

/* A new layer on top, cleared: no stops, centred shape. Beyond the limit it is a spare layer
   that is never part of the cover, so a hook need not check. */
sleeve_layer *sleeve_add_layer(sleeve_builder *b, int kind, int repeating);
sleeve_layer *sleeve_add_linear(sleeve_builder *b, int32_t angle);
sleeve_layer *sleeve_add_radial(sleeve_builder *b, int32_t cx, int32_t cy);
/* A hard-edged disc of radius r. */
void sleeve_add_disc(sleeve_builder *b, int32_t cx, int32_t cy, int32_t r, sleeve_colour c);
/* A soft glow of radius r with an (ex, ey) shape: `alpha` out to `inner`, then fading out. */
void sleeve_add_glow(sleeve_builder *b, int32_t cx, int32_t cy, int32_t ex, int32_t ey,
                     int32_t r, int32_t inner, sleeve_colour c, int32_t alpha);

/* Adds a stop; one beyond SLEEVE_MAX_STOPS is dropped, and one before the last moves up to it. */
void sleeve_add_stop(sleeve_layer *l, int32_t at, sleeve_colour c);
/* A band of colour from `from` to `to`: two stops, so the band's edges are hard. */
void sleeve_add_band(sleeve_layer *l, int32_t from, int32_t to, sleeve_colour c);
/* A position along a linear layer's line, as thousandths of its length. */
int32_t sleeve_along(const sleeve_layer *l, int32_t thousandths);

sleeve_colour sleeve_alpha(sleeve_colour c, int32_t alpha);
/* a and b mixed, `thousandths` of the way to b, in sRGB; opaque. */
sleeve_colour sleeve_mix(sleeve_colour a, sleeve_colour b, int32_t thousandths);
/* OKLCH to opaque sRGB, lightness and chroma in thousandths, hue in degrees; chroma is lowered
   in steps of 4 until the colour is in gamut. */
sleeve_colour sleeve_oklch(int32_t lightness, int32_t chroma, int32_t hue);
/* sRGB to OKLCH, in the same units, hue 0 to 359. */
void sleeve_to_oklch(sleeve_colour c, int32_t *lightness, int32_t *chroma, int32_t *hue);

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

static int32_t sleeve__sin(int32_t d) { return sleeve__cos(90 - d % 360); }

static int32_t sleeve__abs(int32_t v) { return v < 0 ? -v : v; }

/* n / d rounded to the nearest, halves up, for d > 0: q + r / d, with r / d in (-1, 1). */
static int64_t sleeve__div_round(int64_t n, int64_t d) {
  int64_t q = n / d, r = n % d;
  if (r >= 0) return r >= d - r ? q + 1 : q;
  return -r > d + r ? q - 1 : q;
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

static uint32_t sleeve__next(sleeve_rng *r) {
  uint64_t z = (r->state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  z ^= z >> 31;
  return (uint32_t)(z >> 32);
}

/* A number in [0, n), for n > 0. */
static int32_t sleeve__below(sleeve_rng *r, int32_t n) {
  return (int32_t)(((uint64_t)sleeve__next(r) * (uint64_t)(uint32_t)n) >> 32);
}

/* A number in [lo, hi], or lo when hi is below it. */
static int32_t sleeve__range(sleeve_rng *r, int32_t lo, int32_t hi) {
  uint64_t span = hi > lo ? (uint64_t)((int64_t)hi - lo) + 1 : 1;
  return (int32_t)(lo + (int64_t)(((uint64_t)sleeve__next(r) * span) >> 32));
}

static int sleeve__chance(sleeve_rng *r, int32_t percent) {
  return sleeve__below(r, 100) < percent;
}

/* An index into weights[0..n), chosen in proportion to the weights. */
static int sleeve__pick(sleeve_rng *r, const uint16_t *weights, int n) {
  int32_t total = 0, x;
  int i;
  for (i = 0; i < n; i++) total += weights[i];
  x = sleeve__below(r, total);
  for (i = 0; i < n - 1; i++) {
    if (x < weights[i]) return i;
    x -= weights[i];
  }
  return n > 0 ? n - 1 : 0;
}

int32_t sleeve_below(sleeve_builder *b, int32_t n) {
  int32_t v = sleeve__below(&b->rng, n > 0 ? n : 1);
  return n > 0 ? v : 0;
}

int32_t sleeve_range(sleeve_builder *b, int32_t lo, int32_t hi) {
  return sleeve__range(&b->rng, lo, hi);
}

int sleeve_chance(sleeve_builder *b, int32_t percent) {
  return sleeve__chance(&b->rng, percent);
}

int sleeve_pick(sleeve_builder *b, const uint16_t *weights, int n) {
  return sleeve__pick(&b->rng, weights, n);
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

sleeve_colour sleeve_oklch(int32_t lightness, int32_t chroma, int32_t hue) {
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

/* The cube root of v, both in units of 1/65536, rounded down. */
static int64_t sleeve__cbrt(int64_t v) {
  int64_t lo = 0, hi = 1 << 18, target = v * 65536 * 65536;
  if (v <= 0) return 0;
  while (hi - lo > 1) {
    int64_t mid = (lo + hi) / 2;
    if (mid * mid * mid <= target) lo = mid;
    else hi = mid;
  }
  return lo;
}

/* The square root of v, rounded down. */
static int64_t sleeve__isqrt(int64_t v) {
  int64_t lo = 0, hi = 3037000500ll;
  while (hi - lo > 1) {
    int64_t mid = (lo + hi) / 2;
    if (mid * mid <= v) lo = mid;
    else hi = mid;
  }
  return lo;
}

/* The direction of (a, b) in whole degrees, 0 to 359, the nearest by the cosine table. */
static int32_t sleeve__direction(int64_t a, int64_t b) {
  int64_t x = a < 0 ? -a : a, y = b < 0 ? -b : b;
  int32_t lo = 0, hi = 90, d;
  /* the largest d in [0, 90] with tan d <= y / x, then whichever of d and d + 1 is nearer */
  while (lo < hi) {
    int32_t mid = (lo + hi + 1) / 2;
    if (sleeve__cos_table[90 - mid] * x <= sleeve__cos_table[mid] * y) lo = mid;
    else hi = mid - 1;
  }
  d = lo;
  if (d < 90) {
    int64_t under = sleeve__cos_table[d] * y - sleeve__cos_table[90 - d] * x;
    int64_t over = sleeve__cos_table[90 - d - 1] * x - sleeve__cos_table[d + 1] * y;
    if (over < under) d++;
  }
  if (a < 0) d = 180 - d;
  if (b < 0) d = 360 - d;
  return d % 360;
}

/* The matrices are tools/tables.py's. */
void sleeve_to_oklch(sleeve_colour c, int32_t *lightness, int32_t *chroma, int32_t *hue) {
  int64_t r = sleeve__linear_table[c.r], g = sleeve__linear_table[c.g];
  int64_t b = sleeve__linear_table[c.b];
  int64_t l = sleeve__cbrt(sleeve__div_round(27015 * r + 35149 * g + 3372 * b, 65536));
  int64_t m = sleeve__cbrt(sleeve__div_round(13887 * r + 44610 * g + 7038 * b, 65536));
  int64_t s = sleeve__cbrt(sleeve__div_round(5787 * r + 18463 * g + 41286 * b, 65536));
  int64_t ll = sleeve__div_round(13792 * l + 52011 * m - 267 * s, 65536);
  int64_t aa = sleeve__div_round(129630 * l - 159160 * m + 29530 * s, 65536);
  int64_t bb = sleeve__div_round(1698 * l + 51300 * m - 52997 * s, 65536);
  *lightness = (int32_t)sleeve__div_round(ll * 1000, 65536);
  *chroma = (int32_t)sleeve__div_round(sleeve__isqrt(aa * aa + bb * bb) * 1000, 65536);
  *hue = sleeve__direction(aa, bb);
}

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

/* An opaque colour from 0xRRGGBB. */
static sleeve_colour sleeve__rgb(int32_t v) {
  sleeve_colour c;
  c.r = (uint8_t)(v >> 16);
  c.g = (uint8_t)(v >> 8);
  c.b = (uint8_t)v;
  c.a = 255;
  return c;
}

/* ---- Palettes ---------------------------------------------------------------------------- */

const sleeve_mood_def sleeve_moods[SLEEVE_MOODS] = {
  /* dusk: indigo into rose, with peach and gold */
  {"dusk", 275, 320, {{0, 15, 220, 300, 60, 110}, {45, 75, 480, 580, 110, 150},
                      {95, 125, 860, 920, 45, 75}, {90, 120, 730, 810, 120, 160},
                      {60, 80, 620, 690, 130, 160}}, {{0}}},
  /* sea: blues and teals, with sand */
  {"sea", 195, 235, {{0, 10, 260, 360, 50, 90}, {-15, 5, 520, 630, 70, 110},
                     {-20, 0, 890, 950, 20, 45}, {-150, -120, 850, 910, 70, 110},
                     {-40, -25, 690, 770, 70, 100}}, {{0}}},
  /* earth: ochre and rust, with olive */
  {"earth", 40, 80, {{-20, 15, 260, 340, 30, 60}, {-10, 10, 560, 660, 90, 125},
                     {5, 25, 880, 930, 30, 60}, {90, 130, 500, 600, 50, 85},
                     {-35, -20, 500, 580, 110, 140}}, {{0}}},
  /* warm paper: cream, ink, and a red-orange accent */
  {"paper-warm", 75, 95, {{150, 190, 250, 320, 25, 60}, {-55, -40, 560, 640, 140, 170},
                          {0, 10, 930, 965, 20, 35}, {-60, -45, 600, 680, 150, 180},
                          {-20, 0, 780, 840, 80, 110}}, {{0}}},
  /* cool paper: cream, ink, and a blue accent */
  {"paper-cool", 80, 100, {{170, 180, 260, 320, 40, 80}, {165, 180, 480, 560, 110, 150},
                           {0, 10, 930, 965, 15, 30}, {140, 160, 600, 680, 90, 120},
                           {-15, 5, 780, 850, 70, 100}}, {{0}}},
  /* neon: a near-black night with bright pink, cyan and yellow */
  {"neon", 260, 295, {{0, 10, 150, 210, 40, 80}, {0, 20, 300, 380, 80, 120},
                      {150, 175, 880, 920, 140, 170}, {65, 95, 640, 710, 200, 250},
                      {-80, -55, 790, 850, 120, 150}}, {{0}}},
  /* mist: cool greys with one warm note */
  {"mist", 200, 250, {{0, 10, 360, 440, 15, 35}, {0, 10, 640, 720, 15, 30},
                      {0, 10, 910, 950, 5, 15}, {150, 200, 760, 820, 60, 90},
                      {-10, 10, 520, 580, 40, 60}}, {{0}}},
  /* garden: greens with coral and lemon */
  {"garden", 125, 165, {{0, 10, 340, 420, 50, 80}, {-20, 0, 620, 710, 70, 100},
                        {-45, -25, 910, 945, 45, 70}, {165, 200, 720, 790, 100, 130},
                        {-65, -50, 860, 900, 110, 140}}, {{0}}},
  /* duotone: one hue against its complement */
  {"duotone", 0, 359, {{0, 0, 300, 400, 80, 120}, {0, 20, 550, 640, 100, 140},
                       {150, 210, 860, 920, 30, 60}, {160, 200, 700, 780, 100, 140},
                       {20, 40, 750, 820, 60, 90}}, {{0}}},
  /* ember: warm near-black with red and gold */
  {"ember", 20, 45, {{-10, 10, 170, 220, 15, 30}, {0, 10, 540, 600, 160, 190},
                     {40, 60, 900, 940, 40, 60}, {30, 50, 780, 840, 140, 170},
                     {-20, -5, 610, 660, 150, 180}}, {{0}}},
};

static void sleeve__palette(sleeve_rng *r, const sleeve_mood_def *m, sleeve_colour out[5]) {
  int32_t hue = sleeve__range(r, m->hue_lo, m->hue_hi);
  int i;
  for (i = 0; i < 5; i++) {
    const sleeve_role_range *role = &m->roles[i];
    int32_t h, l, c;
    if (m->exact[i].a == 255) {
      out[i] = m->exact[i];
      continue;
    }
    h = hue + sleeve__range(r, role->hue_lo, role->hue_hi);
    l = sleeve__range(r, role->light_lo, role->light_hi);
    c = sleeve__range(r, role->chroma_lo, role->chroma_hi);
    out[i] = sleeve_oklch(l, c, h);
  }
}

/* A given palette's roles. The darkest colour is deep if its lightness is at most 450, the
   lightest of the rest pale if at least 750; of the rest, the most chromatic is the accent if
   its chroma is at least 50, the one nearest the middle lightness mid, and the most chromatic
   left the second accent if its chroma is at least 50. Colours beyond those go unused. An
   empty role is derived in OKLCH from the key colour, the most chromatic given:
   - deep: lightness 240, the key's hue, chroma the key's up to 80;
   - pale: lightness 930, the key's hue, chroma a third of the key's up to 40;
   - mid: lightness halfway between deep and pale, the key's hue, three quarters of its chroma;
   - accent: lightness 720, the key's hue and chroma;
   - second accent: the accent's hue, four fifths of its chroma, its lightness moved 160 toward
     the middle of deep and pale. */
static void sleeve__assign(const sleeve_colour *given, int n, sleeve_colour out[5]) {
  int32_t l[SLEEVE_MAX_GIVEN], c[SLEEVE_MAX_GIVEN], h[SLEEVE_MAX_GIVEN], lr[5], cr[5], hr[5];
  int role[5], used[SLEEVE_MAX_GIVEN], i, k, key = 0, best;
  for (i = 0; i < n; i++) {
    sleeve_to_oklch(given[i], &l[i], &c[i], &h[i]);
    used[i] = 0;
    if (c[i] > c[key]) key = i;
  }
  for (i = 0; i < 5; i++) role[i] = -1;
  /* deep and pale */
  for (best = -1, i = 0; i < n; i++)
    if (best < 0 || l[i] < l[best]) best = i;
  if (l[best] <= 450) role[SLEEVE_DEEP] = best, used[best] = 1;
  for (best = -1, i = 0; i < n; i++)
    if (!used[i] && (best < 0 || l[i] > l[best])) best = i;
  if (best >= 0 && l[best] >= 750) role[SLEEVE_PALE] = best, used[best] = 1;
  /* the accent, mid and the second accent */
  for (best = -1, i = 0; i < n; i++)
    if (!used[i] && (best < 0 || c[i] > c[best])) best = i;
  if (best >= 0 && c[best] >= 50) role[SLEEVE_ACCENT] = best, used[best] = 1;
  {
    int32_t middle = ((role[SLEEVE_DEEP] >= 0 ? l[role[SLEEVE_DEEP]] : 240) +
                      (role[SLEEVE_PALE] >= 0 ? l[role[SLEEVE_PALE]] : 930)) / 2;
    for (best = -1, i = 0; i < n; i++)
      if (!used[i] && (best < 0 || sleeve__abs(l[i] - middle) < sleeve__abs(l[best] - middle)))
        best = i;
    if (best >= 0) role[SLEEVE_MID] = best, used[best] = 1;
  }
  for (best = -1, i = 0; i < n; i++)
    if (!used[i] && (best < 0 || c[i] > c[best])) best = i;
  if (best >= 0 && c[best] >= 50) role[SLEEVE_ACCENT2] = best;
  /* deep, pale, mid, the accent and the second accent, each from those before it */
  for (k = 0; k < 5; k++) {
    static const int order[5] = {SLEEVE_DEEP, SLEEVE_PALE, SLEEVE_MID, SLEEVE_ACCENT,
                                 SLEEVE_ACCENT2};
    i = order[k];
    if (role[i] >= 0) {
      out[i] = given[role[i]];
      out[i].a = 255;
      lr[i] = l[role[i]];
      cr[i] = c[role[i]];
      hr[i] = h[role[i]];
      continue;
    }
    hr[i] = h[key];
    switch (i) {
    case SLEEVE_DEEP: lr[i] = 240, cr[i] = c[key] < 80 ? c[key] : 80; break;
    case SLEEVE_PALE: lr[i] = 930, cr[i] = c[key] / 3 < 40 ? c[key] / 3 : 40; break;
    case SLEEVE_MID: lr[i] = (lr[SLEEVE_DEEP] + lr[SLEEVE_PALE]) / 2, cr[i] = c[key] * 3 / 4; break;
    case SLEEVE_ACCENT: lr[i] = 720, cr[i] = c[key]; break;
    default: {
      int32_t middle = (lr[SLEEVE_DEEP] + lr[SLEEVE_PALE]) / 2;
      hr[i] = hr[SLEEVE_ACCENT];
      cr[i] = cr[SLEEVE_ACCENT] * 4 / 5;
      lr[i] = lr[SLEEVE_ACCENT] > middle ? lr[SLEEVE_ACCENT] - 160 : lr[SLEEVE_ACCENT] + 160;
    }
    }
    out[i] = sleeve_oklch(lr[i], cr[i], hr[i]);
  }
}

int sleeve_assign_palette(const sleeve_colour *colours, int count,
                          sleeve_colour roles[SLEEVE_ROLES]) {
  if (!colours || count < 1 || count > SLEEVE_MAX_GIVEN) return -1;
  sleeve__assign(colours, count, roles);
  return 0;
}

/* ---- The builder ------------------------------------------------------------------------- */

int sleeve_layers_left(const sleeve_builder *b) {
  int limit = b->limit < SLEEVE_MAX_LAYERS ? b->limit : SLEEVE_MAX_LAYERS;
  return b->cover->layer_count < limit ? limit - b->cover->layer_count : 0;
}

static sleeve_layer *sleeve__add_layer(sleeve_builder *b, int kind, int repeating) {
  sleeve_cover *c = b->cover;
  sleeve_layer *l = c->layer_count < b->limit && c->layer_count < SLEEVE_MAX_LAYERS
                        ? &c->layers[c->layer_count++] : &b->spare;
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

static void sleeve__add_stop(sleeve_layer *l, int32_t at, sleeve_colour c) {
  int n = l->stop_count;
  if (n >= SLEEVE_MAX_STOPS) return;
  if (n > 0 && at < l->stops[n - 1].at) at = l->stops[n - 1].at;
  l->stops[n].at = at;
  l->stops[n].colour = c;
  l->stop_count = (uint8_t)(n + 1);
}

static void sleeve__add_band(sleeve_layer *l, int32_t from, int32_t to, sleeve_colour c) {
  sleeve__add_stop(l, from, c);
  sleeve__add_stop(l, to, c);
}

static sleeve_layer *sleeve__add_linear(sleeve_builder *b, int32_t angle) {
  sleeve_layer *l = sleeve__add_layer(b, SLEEVE_LINEAR, 0);
  l->angle = angle;
  return l;
}

static int32_t sleeve__along(const sleeve_layer *l, int32_t thousandths) {
  return (int32_t)sleeve__div_round((int64_t)sleeve__line_length(l->angle) * thousandths, 1000);
}

static sleeve_layer *sleeve__add_radial(sleeve_builder *b, int32_t cx, int32_t cy) {
  sleeve_layer *l = sleeve__add_layer(b, SLEEVE_RADIAL, 0);
  l->cx = cx;
  l->cy = cy;
  return l;
}

static void sleeve__add_disc(sleeve_builder *b, int32_t cx, int32_t cy, int32_t r,
                             sleeve_colour c) {
  sleeve_layer *l = sleeve__add_radial(b, cx, cy);
  sleeve__add_band(l, 0, r, c);
  sleeve__add_stop(l, r, sleeve__alpha(c, 0));
}

static void sleeve__add_glow(sleeve_builder *b, int32_t cx, int32_t cy, int32_t ex, int32_t ey,
                             int32_t r, int32_t inner, sleeve_colour c, int32_t alpha) {
  sleeve_layer *l = sleeve__add_radial(b, cx, cy);
  l->ex = ex;
  l->ey = ey;
  sleeve__add_band(l, 0, inner, sleeve__alpha(c, alpha));
  sleeve__add_stop(l, r, sleeve__alpha(c, 0));
}

sleeve_layer *sleeve_add_layer(sleeve_builder *b, int kind, int repeating) {
  return sleeve__add_layer(b, kind, repeating);
}

sleeve_layer *sleeve_add_linear(sleeve_builder *b, int32_t angle) {
  return sleeve__add_linear(b, angle);
}

sleeve_layer *sleeve_add_radial(sleeve_builder *b, int32_t cx, int32_t cy) {
  return sleeve__add_radial(b, cx, cy);
}

void sleeve_add_disc(sleeve_builder *b, int32_t cx, int32_t cy, int32_t r, sleeve_colour c) {
  sleeve__add_disc(b, cx, cy, r, c);
}

void sleeve_add_glow(sleeve_builder *b, int32_t cx, int32_t cy, int32_t ex, int32_t ey,
                     int32_t r, int32_t inner, sleeve_colour c, int32_t alpha) {
  sleeve__add_glow(b, cx, cy, ex, ey, r, inner, c, alpha);
}

void sleeve_add_stop(sleeve_layer *l, int32_t at, sleeve_colour c) { sleeve__add_stop(l, at, c); }

void sleeve_add_band(sleeve_layer *l, int32_t from, int32_t to, sleeve_colour c) {
  sleeve__add_band(l, from, to, c);
}

int32_t sleeve_along(const sleeve_layer *l, int32_t thousandths) {
  return sleeve__along(l, thousandths);
}

sleeve_colour sleeve_alpha(sleeve_colour c, int32_t alpha) { return sleeve__alpha(c, alpha); }

sleeve_colour sleeve_mix(sleeve_colour a, sleeve_colour b, int32_t thousandths) {
  return sleeve__mix_colour(a, b, thousandths);
}

/* A palette role, chosen in proportion to the weights. */
static int sleeve__pick_role(sleeve_builder *g, const uint16_t *weights) {
  return sleeve__pick(&g->rng, weights, 5);
}

/* ---- Compositions ------------------------------------------------------------------------ */

static const sleeve_horizon_params sleeve__horizon_defaults = {40, 60, 8, 420, 650, 85, 30, 700,
                                                                1500, 55, 35};
static const sleeve_glow_params sleeve__glow_defaults = {60, 4500, 6500, 170, 220, 55};
static const sleeve_disc_params sleeve__disc_defaults = {35, 60, 900, 1400, 2, 3, 40, 40};
static const sleeve_pair_params sleeve__pair_defaults = {60, 1800, 2300, 800, 1300, 50, 60};
static const sleeve_stripes_params sleeve__stripes_defaults = {65, 2, 4, 4, 40};
static const sleeve_bands_params sleeve__bands_defaults = {25, 3, 4, 40, 30, 30};
static const sleeve_split_params sleeve__split_defaults = {20, 160, 45, 55, 1000, 2200};
static const sleeve_bars_params sleeve__bars_defaults = {70, 2, 3, 25};
static const sleeve_constellation_params sleeve__constellation_defaults = {4, 9, 35, 200, 450};
static const sleeve_grooves_params sleeve__grooves_defaults = {25, 50, 20};
static const sleeve_orbs_params sleeve__orbs_defaults = {50, 50, 40};
static const sleeve_checks_params sleeve__checks_defaults = {400, 1000, 50, 60};

static int32_t sleeve__clamp(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/* A sky over ground at a horizon, a sun or moon in the sky, and sometimes lines in the ground.
   The ground is its own layer over the sun, so a setting sun sinks behind it. */
void sleeve_compose_horizon(sleeve_builder *g, const void *params) {
  const sleeve_horizon_params *p = params ? (const sleeve_horizon_params *)params
                                          : &sleeve__horizon_defaults;
  sleeve_rng *r = &g->rng;
  int night = sleeve__chance(r, p->night);
  int32_t tilt = sleeve__chance(r, p->level) ? 0 : sleeve__range(r, -p->tilt, p->tilt);
  int32_t horizon = sleeve__range(r, p->horizon_min, p->horizon_max), band = 0;
  sleeve_layer *sky = sleeve__add_linear(g, 180 + tilt), *land;
  int32_t h = sleeve__along(sky, horizon), end = sleeve__along(sky, 1000);
  sleeve_colour top = g->palette[night ? SLEEVE_DEEP : SLEEVE_MID];
  sleeve_colour low = g->palette[night ? SLEEVE_MID : SLEEVE_PALE];
  sleeve_colour ground = g->palette[SLEEVE_DEEP], edge = g->palette[SLEEVE_ACCENT2];
  sleeve__add_stop(sky, 0, top);
  sleeve__add_stop(sky, h, low);
  if (sleeve__chance(r, p->sun)) {
    int setting = sleeve__chance(r, p->setting);
    int32_t sun_r = sleeve__range(r, p->sun_min, p->sun_max), sun_x = sleeve__range(r, 2000, 8000);
    int32_t sun_y = setting ? horizon * 10 : sleeve__range(r, 1500, horizon * 10 - sun_r - 600);
    if (sun_y < sun_r + 500) sun_y = sun_r + 500;
    sleeve__add_disc(g, sun_x, sun_y, sun_r, g->palette[night ? SLEEVE_PALE : SLEEVE_ACCENT]);
  }
  land = sleeve__add_linear(g, 180 + tilt);
  if (sleeve__chance(r, p->band)) band = sleeve__range(r, 120, 450);
  sleeve__add_stop(land, h, sleeve__alpha(band ? edge : ground, 0));
  if (band) sleeve__add_band(land, h, h + band, edge);
  sleeve__add_band(land, h + band, end, ground);
  if (sleeve__chance(r, p->lines)) {
    int32_t gap = sleeve__range(r, 400, 900), w = sleeve__range(r, 60, 160), at = h + band + gap;
    sleeve_colour c = sleeve__alpha(g->palette[SLEEVE_PALE], sleeve__range(r, 70, 140));
    if (at + w < end) {
      sleeve_layer *lines = sleeve__add_linear(g, 180 + tilt);
      while (lines->stop_count + 4 <= SLEEVE_MAX_STOPS && at + w < end) {
        sleeve__add_stop(lines, at, sleeve__alpha(c, 0));
        sleeve__add_band(lines, at, at + w, c);
        sleeve__add_stop(lines, at + w, sleeve__alpha(c, 0));
        at += w + gap;
        gap = gap * 3 / 4 + 120;
      }
    }
  }
}

/* A sky wash with a glow rising from the bottom edge, at dusk or at dawn, and sometimes a sun
   or moon above it. */
void sleeve_compose_glow(sleeve_builder *g, const void *params) {
  const sleeve_glow_params *p = params ? (const sleeve_glow_params *)params
                                       : &sleeve__glow_defaults;
  sleeve_rng *r = &g->rng;
  sleeve_layer *wash = sleeve__add_linear(g, 180);
  int dusk = sleeve__chance(r, p->dusk);
  int32_t gx = sleeve__range(r, 3500, 6500);
  int32_t radius = sleeve__range(r, p->radius_min, p->radius_max);
  sleeve__add_stop(wash, 0, g->palette[dusk ? SLEEVE_DEEP : SLEEVE_MID]);
  if (dusk) sleeve__add_stop(wash, sleeve__range(r, 4500, 6500), g->palette[SLEEVE_MID]);
  sleeve__add_stop(wash, SLEEVE_UNIT, g->palette[dusk ? SLEEVE_ACCENT2 : SLEEVE_PALE]);
  {
    int32_t gy = SLEEVE_UNIT + sleeve__range(r, 0, 800), ey = sleeve__range(r, 5500, 8000);
    int32_t alpha = sleeve__range(r, p->alpha_min, p->alpha_max);
    sleeve__add_glow(g, gx, gy, SLEEVE_UNIT, ey, radius, radius / 3, g->palette[SLEEVE_PALE],
                     alpha);
  }
  if (sleeve__chance(r, p->sun)) {
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 1800, 4000);
    int32_t size = sleeve__range(r, 700, 1300);
    sleeve__add_disc(g, x, y, size, g->palette[SLEEVE_ACCENT]);
  }
}

/* Concentric rings on a ground, or a record with grooves and a label, under a faint wash. */
void sleeve_compose_disc(sleeve_builder *g, const void *params) {
  const sleeve_disc_params *p = params ? (const sleeve_disc_params *)params
                                       : &sleeve__disc_defaults;
  sleeve_rng *r = &g->rng;
  static const uint16_t grounds[5] = {4, 2, 4, 0, 1};
  int ground = sleeve__pick_role(g, grounds);
  int32_t cx = sleeve__chance(r, p->centred) ? 5000 : sleeve__range(r, 3500, 6500);
  int32_t cy = sleeve__chance(r, p->centred) ? 5000 : sleeve__range(r, 3500, 6500);
  sleeve_colour bg = g->palette[ground];
  g->cover->base = bg;
  if (sleeve__chance(r, p->record)) {
    /* the vinyl is the base, so the grooves on it and the ground around it meet in one edge */
    int32_t outer = sleeve__range(r, 3300, 4300);
    int32_t label = sleeve__range(r, p->label_min, p->label_max);
    int32_t pitch = sleeve__range(r, 90, 200);
    sleeve_colour line = sleeve__alpha(g->palette[SLEEVE_PALE], sleeve__range(r, 25, 45));
    sleeve_layer *grooves = sleeve__add_radial(g, cx, cy), *around;
    g->cover->base = g->palette[ground == SLEEVE_DEEP ? SLEEVE_MID : SLEEVE_DEEP];
    grooves->repeating = 1;
    sleeve__add_band(grooves, 0, pitch / 2, sleeve__alpha(line, 0));
    sleeve__add_band(grooves, pitch / 2, pitch, line);
    around = sleeve__add_radial(g, cx, cy);
    sleeve__add_stop(around, outer, sleeve__alpha(bg, 0));
    sleeve__add_stop(around, outer, bg);
    sleeve__add_disc(g, cx, cy, label,
                     g->palette[sleeve__chance(r, 50) ? SLEEVE_ACCENT : SLEEVE_ACCENT2]);
    sleeve__add_disc(g, cx, cy, sleeve__range(r, 120, 220), bg);
  } else {
    sleeve_layer *rings = sleeve__add_radial(g, cx, cy);
    static const int order[4] = {SLEEVE_ACCENT, SLEEVE_PALE, SLEEVE_MID, SLEEVE_DEEP};
    int count = sleeve__clamp(sleeve__range(r, p->rings_min, p->rings_max), 1, 3), i;
    int first = sleeve__below(r, 4);
    int32_t at = 0;
    for (i = 0; i < count; i++) {
      int role = order[(first + i) % 4];
      int32_t w = i == 0 ? sleeve__range(r, 1000, 2400) : sleeve__range(r, 150, 900);
      if (role == ground) role = SLEEVE_ACCENT2;
      sleeve__add_band(rings, at, at + w, g->palette[role]);
      at += w;
    }
    sleeve__add_stop(rings, at, sleeve__alpha(rings->stops[rings->stop_count - 1].colour, 0));
    if (sleeve__chance(r, p->halo)) {
      sleeve_layer *halo = sleeve__add_radial(g, cx, cy);
      int32_t gap = sleeve__range(r, 300, 800), w = sleeve__range(r, 120, 300);
      sleeve_colour c = sleeve__alpha(rings->stops[0].colour, sleeve__range(r, 80, 140));
      sleeve__add_stop(halo, at + gap, sleeve__alpha(c, 0));
      sleeve__add_band(halo, at + gap, at + gap + w, c);
      sleeve__add_stop(halo, at + gap + w, sleeve__alpha(c, 0));
    }
  }
  if (sleeve__chance(r, p->wash)) {
    sleeve_layer *wash = sleeve__add_linear(g, sleeve__range(r, 135, 200));
    sleeve__add_stop(wash, 0, sleeve__alpha(g->palette[SLEEVE_PALE], 0));
    sleeve__add_stop(wash, sleeve__along(wash, 1000), sleeve__alpha(g->palette[SLEEVE_PALE], 60));
  }
}

/* Two matching ringed discs side by side, as reels. */
void sleeve_compose_pair(sleeve_builder *g, const void *params) {
  const sleeve_pair_params *p = params ? (const sleeve_pair_params *)params
                                       : &sleeve__pair_defaults;
  sleeve_rng *r = &g->rng;
  int32_t spread = sleeve__range(r, p->spread_min, p->spread_max), y = sleeve__range(r, 4200, 5800);
  int32_t hub = sleeve__range(r, p->hub_min, p->hub_max), rim = sleeve__range(r, 250, 450);
  int32_t gap = sleeve__range(r, 200, 400);
  int dark = sleeve__chance(r, p->dark), side;
  sleeve_colour ground = dark ? g->palette[SLEEVE_DEEP] : g->palette[SLEEVE_PALE];
  sleeve_colour wheel = dark ? g->palette[SLEEVE_PALE] : g->palette[SLEEVE_DEEP];
  g->cover->base = ground;
  if (sleeve__chance(r, p->strip)) {
    sleeve_layer *strip = sleeve__add_linear(g, 180);
    int32_t half = sleeve__range(r, 1600, 2400);
    sleeve_colour c = g->palette[SLEEVE_MID];
    sleeve__add_stop(strip, y - half, sleeve__alpha(c, 0));
    sleeve__add_band(strip, y - half, y + half, c);
    sleeve__add_stop(strip, y + half, sleeve__alpha(c, 0));
  }
  for (side = -1; side <= 1; side += 2) {
    sleeve_layer *reel = sleeve__add_radial(g, 5000 + side * spread, y);
    sleeve__add_band(reel, 0, hub, wheel);
    sleeve__add_band(reel, hub, hub + gap, ground);
    sleeve__add_band(reel, hub + gap, hub + gap + rim, wheel);
    sleeve__add_stop(reel, hub + gap + rim, sleeve__alpha(wheel, 0));
  }
  if (sleeve__chance(r, p->spots)) {
    int32_t spot = sleeve__range(r, 180, 320);
    sleeve__add_disc(g, 5000 - spread, y, spot, g->palette[SLEEVE_ACCENT]);
    sleeve__add_disc(g, 5000 + spread, y, spot, g->palette[SLEEVE_ACCENT]);
  }
}

/* Repeating stripes at an angle, solid or as fine lines over a wash. */
void sleeve_compose_stripes(sleeve_builder *g, const void *params) {
  const sleeve_stripes_params *p = params ? (const sleeve_stripes_params *)params
                                          : &sleeve__stripes_defaults;
  sleeve_rng *r = &g->rng;
  static const int16_t angles[10] = {0, 20, 45, 65, 90, 105, 115, 135, 160, 120};
  int32_t angle = angles[sleeve__below(r, 10)], wobble = sleeve__range(r, -p->wobble, p->wobble);
  angle += wobble;
  if (sleeve__chance(r, p->solid)) {
    sleeve_layer *s = sleeve__add_linear(g, angle);
    static const int order[5] = {SLEEVE_DEEP, SLEEVE_MID, SLEEVE_PALE, SLEEVE_ACCENT,
                                 SLEEVE_ACCENT2};
    int count = sleeve__clamp(sleeve__range(r, p->colours_min, p->colours_max), 1, 4), i;
    int first = sleeve__below(r, 3);
    int32_t at = 0;
    s->repeating = 1;
    for (i = 0; i < count; i++) {
      int role = order[(first + i * 2) % 5];
      int32_t w = i == 0 ? sleeve__range(r, 400, 1200)
                         : (role >= SLEEVE_ACCENT ? sleeve__range(r, 100, 300)
                                                  : sleeve__range(r, 120, 700));
      sleeve__add_band(s, at, at + w, g->palette[role]);
      at += w;
    }
    g->cover->base = s->stops[0].colour;
  } else {
    sleeve_layer *wash = sleeve__add_linear(g, sleeve__range(r, 150, 210));
    sleeve_layer *lines;
    int dark = sleeve__chance(r, 50);
    int32_t pitch = sleeve__range(r, 350, 800), w = sleeve__range(r, 40, 120);
    sleeve_colour ink = dark ? g->palette[SLEEVE_PALE] : g->palette[SLEEVE_DEEP];
    sleeve__add_stop(wash, 0, dark ? g->palette[SLEEVE_DEEP] : g->palette[SLEEVE_MID]);
    sleeve__add_stop(wash, sleeve__along(wash, 1000),
                     dark ? g->palette[SLEEVE_MID] : g->palette[SLEEVE_PALE]);
    lines = sleeve__add_linear(g, angle);
    lines->repeating = 1;
    ink = sleeve__alpha(ink, sleeve__range(r, 60, 120));
    sleeve__add_band(lines, 0, pitch - w, sleeve__alpha(ink, 0));
    sleeve__add_band(lines, pitch - w, pitch, ink);
    if (sleeve__chance(r, p->disc)) {
      int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
      int32_t size = sleeve__range(r, 1000, 1800);
      sleeve__add_disc(g, x, y, size, g->palette[SLEEVE_ACCENT]);
    }
  }
}

/* Hard bands across the cover in proportion, one of them sometimes a thin accent, and
   sometimes a disc; or a sheen of translucent bands over a diagonal wash. */
void sleeve_compose_bands(sleeve_builder *g, const void *params) {
  const sleeve_bands_params *p = params ? (const sleeve_bands_params *)params
                                        : &sleeve__bands_defaults;
  sleeve_rng *r = &g->rng;
  static const int16_t angles[6] = {180, 90, 180, 0, 270, 180};
  static const int order[5] = {SLEEVE_PALE, SLEEVE_MID, SLEEVE_ACCENT, SLEEVE_DEEP,
                               SLEEVE_ACCENT2};
  int32_t angle = angles[sleeve__below(r, 6)], weights[4], total = 0, at = 0, length;
  int count = sleeve__clamp(sleeve__range(r, p->bands_min, p->bands_max), 2, 4);
  int thin = sleeve__chance(r, p->thin) ? sleeve__below(r, count) : -1;
  int first = sleeve__below(r, 5), i;
  sleeve_layer *b;
  if (sleeve__chance(r, p->sheen)) {
    /* a sheen: a diagonal wash crossed by two translucent pale bands */
    sleeve_layer *wash = sleeve__add_linear(g, sleeve__range(r, 120, 160)), *sheen;
    int32_t a1 = sleeve__range(r, 3500, 4500), w1 = sleeve__range(r, 200, 400);
    int32_t a2 = a1 + w1 + sleeve__range(r, 800, 1500), w2 = sleeve__range(r, 150, 300);
    sleeve_colour c1 = sleeve__alpha(g->palette[SLEEVE_PALE], sleeve__range(r, 110, 150));
    sleeve_colour c2 = sleeve__alpha(g->palette[SLEEVE_PALE], sleeve__range(r, 80, 110));
    sleeve__add_stop(wash, 0, g->palette[SLEEVE_PALE]);
    sleeve__add_stop(wash, sleeve__along(wash, 500), g->palette[SLEEVE_MID]);
    sleeve__add_stop(wash, sleeve__along(wash, 1000), g->palette[SLEEVE_DEEP]);
    sheen = sleeve__add_linear(g, sleeve__chance(r, 50) ? 0 : sleeve__range(r, -10, 10));
    sleeve__add_stop(sheen, a1, sleeve__alpha(c1, 0));
    sleeve__add_band(sheen, a1, a1 + w1, c1);
    sleeve__add_stop(sheen, a1 + w1, sleeve__alpha(c1, 0));
    sleeve__add_stop(sheen, a2, sleeve__alpha(c2, 0));
    sleeve__add_band(sheen, a2, a2 + w2, c2);
    sleeve__add_stop(sheen, a2 + w2, sleeve__alpha(c2, 0));
    return;
  }
  if (sleeve__chance(r, p->tilt)) angle += sleeve__range(r, -12, 12);
  b = sleeve__add_linear(g, angle);
  length = sleeve__along(b, 1000);
  for (i = 0; i < count; i++) {
    weights[i] = i == thin ? sleeve__range(r, 3, 6) : sleeve__range(r, 14, 36);
    total += weights[i];
  }
  for (i = 0; i < count; i++) {
    int32_t next = i == count - 1 ? length : at + length * weights[i] / total;
    sleeve__add_band(b, at, next, g->palette[order[(first + i) % 5]]);
    at = next;
  }
  g->cover->base = b->stops[0].colour;
  if (sleeve__chance(r, p->disc)) {
    int32_t dr = sleeve__range(r, 900, 1600);
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
    int role = sleeve__chance(r, 50) ? SLEEVE_PALE : SLEEVE_ACCENT;
    sleeve__add_disc(g, x, y, dr, g->palette[role]);
  }
}

/* A hard diagonal split, sometimes crossed by a band, sometimes with a disc. */
void sleeve_compose_split(sleeve_builder *g, const void *params) {
  const sleeve_split_params *p = params ? (const sleeve_split_params *)params
                                        : &sleeve__split_defaults;
  sleeve_rng *r = &g->rng;
  int32_t angle = sleeve__range(r, p->angle_min, p->angle_max), flip = sleeve__chance(r, 50);
  sleeve_layer *s = sleeve__add_linear(g, flip ? angle + 180 : angle);
  int32_t at = sleeve__along(s, sleeve__range(r, 380, 620));
  int first = sleeve__chance(r, 50) ? SLEEVE_DEEP : SLEEVE_MID;
  int second = first == SLEEVE_DEEP ? (sleeve__chance(r, 50) ? SLEEVE_PALE : SLEEVE_ACCENT)
                                    : SLEEVE_PALE;
  sleeve__add_band(s, 0, at, g->palette[first]);
  sleeve__add_band(s, at, sleeve__along(s, 1000), g->palette[second]);
  if (sleeve__chance(r, p->cross)) {
    sleeve_layer *cross = sleeve__add_linear(g, s->angle + sleeve__range(r, 60, 120));
    int32_t c = sleeve__along(cross, sleeve__range(r, 350, 650)), w = sleeve__range(r, 500, 1500);
    sleeve_colour band = g->palette[SLEEVE_ACCENT2];
    sleeve__add_stop(cross, c, sleeve__alpha(band, 0));
    sleeve__add_band(cross, c, c + w, band);
    sleeve__add_stop(cross, c + w, sleeve__alpha(band, 0));
  }
  if (sleeve__chance(r, p->disc)) {
    int32_t dr = sleeve__range(r, p->disc_min, p->disc_max);
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
    sleeve__add_disc(g, x, y, dr,
                     g->palette[second == SLEEVE_ACCENT ? SLEEVE_PALE : SLEEVE_ACCENT]);
  }
}

/* Thin bright bars on a dark ground that lightens at the bottom, two bars to a layer. */
void sleeve_compose_bars(sleeve_builder *g, const void *params) {
  const sleeve_bars_params *p = params ? (const sleeve_bars_params *)params
                                       : &sleeve__bars_defaults;
  static const int roles[3] = {SLEEVE_ACCENT, SLEEVE_ACCENT2, SLEEVE_PALE};
  sleeve_rng *r = &g->rng;
  sleeve_layer *ground = sleeve__add_linear(g, 180);
  int32_t angle = sleeve__chance(r, p->vertical) ? 90 : 180;
  sleeve_layer *bars = 0;
  int count = sleeve__range(r, p->bars_min, p->bars_max), i;
  int32_t at = sleeve__range(r, 1500, 3500), length = sleeve__line_length(angle);
  sleeve__add_stop(ground, 0, g->palette[SLEEVE_DEEP]);
  sleeve__add_stop(ground, sleeve__range(r, 6000, 8000), g->palette[SLEEVE_DEEP]);
  sleeve__add_stop(ground, SLEEVE_UNIT, g->palette[SLEEVE_MID]);
  if (sleeve__chance(r, p->tilt)) angle += sleeve__range(r, -15, 15);
  for (i = 0; i < count && at < length - 400; i++) {
    int32_t w = sleeve__range(r, 150, 350);
    sleeve_colour c = g->palette[roles[i % 3]];
    if (i % 2 == 0) bars = sleeve__add_linear(g, angle);
    sleeve__add_stop(bars, at, sleeve__alpha(c, 0));
    sleeve__add_band(bars, at, at + w, c);
    sleeve__add_stop(bars, at + w, sleeve__alpha(c, 0));
    at += w + sleeve__range(r, 600, 2400);
  }
}

/* Small bright dots on a night wash, sometimes with a larger moon. */
void sleeve_compose_constellation(sleeve_builder *g, const void *params) {
  const sleeve_constellation_params *p = params ? (const sleeve_constellation_params *)params
                                                : &sleeve__constellation_defaults;
  sleeve_rng *r = &g->rng;
  sleeve_layer *sky = sleeve__add_linear(g, 180);
  int count = sleeve__clamp(sleeve__range(r, p->stars_min, p->stars_max), 1, 9), i;
  int placed = 0, tries = 0;
  int32_t xs[10], ys[10];
  sleeve__add_stop(sky, 0, g->palette[SLEEVE_DEEP]);
  sleeve__add_stop(sky, SLEEVE_UNIT,
                   sleeve__mix_colour(g->palette[SLEEVE_DEEP], g->palette[SLEEVE_MID], 400));
  if (sleeve__chance(r, p->moon)) {
    int32_t mr = sleeve__range(r, 900, 1400);
    xs[placed] = sleeve__range(r, 2500, 7500);
    ys[placed] = sleeve__range(r, 2000, 5000);
    sleeve__add_disc(g, xs[placed], ys[placed], mr, g->palette[SLEEVE_PALE]);
    placed++;
  }
  while (placed < count + 1 && tries < 40 && g->cover->layer_count < g->limit) {
    int32_t x = sleeve__range(r, 1200, 8800), y = sleeve__range(r, 1200, 8800);
    int ok = 1;
    tries++;
    for (i = 0; i < placed; i++) {
      int64_t dx = x - xs[i], dy = y - ys[i];
      if (dx * dx + dy * dy < (int64_t)1800 * 1800) ok = 0;
    }
    if (!ok) continue;
    {
      int32_t dr = sleeve__range(r, p->star_min, p->star_max);
      int role = sleeve__chance(r, 50) ? SLEEVE_ACCENT : (sleeve__chance(r, 50) ? SLEEVE_ACCENT2
                                                                            : SLEEVE_PALE);
      xs[placed] = x;
      ys[placed] = y;
      placed++;
      sleeve__add_disc(g, x, y, dr, g->palette[role]);
    }
  }
}

/* Repeating rings from a centre off to one side, as ripples or contours. */
void sleeve_compose_grooves(sleeve_builder *g, const void *params) {
  const sleeve_grooves_params *p = params ? (const sleeve_grooves_params *)params
                                          : &sleeve__grooves_defaults;
  sleeve_rng *r = &g->rng;
  int32_t cx = sleeve__chance(r, 50) ? sleeve__range(r, -2000, 12000)
                                     : (sleeve__chance(r, 50) ? 0 : SLEEVE_UNIT);
  int32_t cy = sleeve__range(r, -2000, 12000);
  sleeve_layer *rings = sleeve__add_radial(g, cx, cy);
  int lines = sleeve__chance(r, p->lines);
  rings->repeating = 1;
  if (lines) {
    int dark = sleeve__chance(r, 50);
    int32_t pitch = sleeve__range(r, 350, 700), w = sleeve__range(r, 40, 90);
    sleeve_colour ink = dark ? g->palette[SLEEVE_PALE] : g->palette[SLEEVE_DEEP];
    g->cover->base = dark ? g->palette[SLEEVE_DEEP] : g->palette[SLEEVE_PALE];
    ink = sleeve__alpha(ink, sleeve__range(r, 70, 130));
    sleeve__add_band(rings, 0, pitch - w, sleeve__alpha(ink, 0));
    sleeve__add_band(rings, pitch - w, pitch, ink);
  } else if (sleeve__chance(r, p->two_tone)) {
    /* a ground and one colour */
    static const int others[3] = {SLEEVE_MID, SLEEVE_ACCENT, SLEEVE_ACCENT2};
    int ground = sleeve__chance(r, 50) ? SLEEVE_DEEP : SLEEVE_PALE;
    int32_t w = sleeve__range(r, 350, 900), w2 = sleeve__range(r, 150, 450);
    int other = others[sleeve__below(r, 3)];
    sleeve__add_band(rings, 0, w, g->palette[ground]);
    sleeve__add_band(rings, w, w + w2, g->palette[other]);
    g->cover->base = g->palette[ground];
  } else {
    /* the deep, mid and pale tones of one family, in some order */
    static const int orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1},
                                     {2, 1, 0}};
    const int *order = orders[sleeve__below(r, 6)];
    int i;
    int32_t at = 0;
    for (i = 0; i < 3; i++) {
      int32_t w = sleeve__range(r, 250, 800);
      sleeve__add_band(rings, at, at + w, g->palette[order[i]]);
      at += w;
    }
    g->cover->base = rings->stops[0].colour;
  }
  if (sleeve__chance(r, p->disc)) {
    int32_t dr = sleeve__range(r, 900, 1600);
    int32_t x = sleeve__range(r, 2500, 7500), y = sleeve__range(r, 2500, 7500);
    sleeve__add_disc(g, x, y, dr, g->palette[SLEEVE_ACCENT]);
  }
}

/* Two large discs or ellipses, overlapping or from the edges, over a ground. */
void sleeve_compose_orbs(sleeve_builder *g, const void *params) {
  const sleeve_orbs_params *p = params ? (const sleeve_orbs_params *)params
                                       : &sleeve__orbs_defaults;
  sleeve_rng *r = &g->rng;
  static const uint16_t grounds[5] = {4, 3, 1, 0, 0};
  int ground = sleeve__pick_role(g, grounds), i;
  static const int roles[4] = {SLEEVE_PALE, SLEEVE_ACCENT, SLEEVE_ACCENT2, SLEEVE_MID};
  int first = sleeve__below(r, 4);
  g->cover->base = g->palette[ground];
  if (sleeve__chance(r, p->wash)) {
    sleeve_layer *wash = sleeve__add_linear(g, sleeve__range(r, 100, 170));
    sleeve__add_stop(wash, 0, g->palette[ground]);
    sleeve__add_stop(wash, sleeve__along(wash, 1000),
                     g->palette[ground == SLEEVE_DEEP ? SLEEVE_MID : SLEEVE_DEEP]);
  }
  for (i = 0; i < 2; i++) {
    int edge = sleeve__chance(r, p->edge);
    int32_t x = edge ? (i == 0 ? sleeve__range(r, -1000, 2500) : sleeve__range(r, 7500, 11000))
                     : sleeve__range(r, 2500, 7500);
    int32_t y = edge ? (i == 0 ? sleeve__range(r, 6000, 11000) : sleeve__range(r, -1000, 4000))
                     : sleeve__range(r, 2500, 7500);
    int32_t rad = edge ? sleeve__range(r, 3000, 4800) : sleeve__range(r, 1800, 3200);
    int role = roles[(first + i) % 4];
    sleeve_layer *orb;
    if (role == ground) role = SLEEVE_PALE;
    orb = sleeve__add_radial(g, x, y);
    orb->ey = sleeve__chance(r, p->oval) ? sleeve__range(r, 6000, 9000) : SLEEVE_UNIT;
    sleeve__add_band(orb, 0, rad, g->palette[role]);
    sleeve__add_stop(orb, rad, sleeve__alpha(g->palette[role], 0));
  }
}

/* A checkerboard, with a disc or a band over it. */
void sleeve_compose_checks(sleeve_builder *g, const void *params) {
  const sleeve_checks_params *p = params ? (const sleeve_checks_params *)params
                                         : &sleeve__checks_defaults;
  sleeve_rng *r = &g->rng;
  sleeve_layer *c = sleeve__add_layer(g, SLEEVE_CHECKER, 0);
  int32_t tile = sleeve__range(r, p->tile_min, p->tile_max);
  int dark = sleeve__chance(r, p->dark);
  c->ex = c->ey = tile;
  c->cx = 0;
  c->cy = 0;
  sleeve__add_stop(c, 0, g->palette[SLEEVE_PALE]);
  sleeve__add_stop(c, 0, g->palette[dark ? SLEEVE_DEEP : SLEEVE_MID]);
  g->cover->base = g->palette[SLEEVE_PALE];
  if (sleeve__chance(r, p->disc)) {
    int32_t dr = sleeve__range(r, 1800, 3200);
    sleeve_layer *d = sleeve__add_radial(g, 5000, 5000);
    sleeve__add_band(d, 0, dr, g->palette[SLEEVE_ACCENT]);
    sleeve__add_band(d, dr, dr + sleeve__range(r, 300, 700), g->palette[SLEEVE_PALE]);
    sleeve__add_stop(d, d->stops[3].at, sleeve__alpha(g->palette[SLEEVE_PALE], 0));
  } else {
    sleeve_layer *b = sleeve__add_linear(g, 180);
    int32_t at = sleeve__range(r, 5500, 7500);
    sleeve__add_stop(b, at, sleeve__alpha(g->palette[SLEEVE_ACCENT], 0));
    sleeve__add_band(b, at, SLEEVE_UNIT, g->palette[SLEEVE_ACCENT]);
  }
}

/* ---- Palette hooks ----------------------------------------------------------------------- */

static const sleeve_tint_params sleeve__tint_defaults = {0xf2e3c6, 250};
static const sleeve_chroma_params sleeve__chroma_defaults = {500};
static const sleeve_monochrome_params sleeve__monochrome_defaults = {-1};
static const sleeve_lightness_params sleeve__lightness_defaults = {150, 900};
static const sleeve_duotone_params sleeve__duotone_defaults = {0x1d2b4f, 0xf3e8d2};

/* Every colour mixed toward one colour. */
void sleeve_palette_tint(sleeve_builder *b, const void *params) {
  const sleeve_tint_params *p = params ? (const sleeve_tint_params *)params
                                       : &sleeve__tint_defaults;
  int i;
  for (i = 0; i < SLEEVE_ROLES; i++)
    b->palette[i] = sleeve__mix_colour(b->palette[i], sleeve__rgb(p->colour), p->amount);
}

/* Every colour's chroma scaled. */
void sleeve_palette_chroma(sleeve_builder *b, const void *params) {
  const sleeve_chroma_params *p = params ? (const sleeve_chroma_params *)params
                                         : &sleeve__chroma_defaults;
  int i;
  for (i = 0; i < SLEEVE_ROLES; i++) {
    int32_t l, c, h;
    sleeve_to_oklch(b->palette[i], &l, &c, &h);
    b->palette[i] = sleeve_oklch(l, (int32_t)sleeve__div_round((int64_t)c * p->scale, 1000), h);
  }
}

/* Every colour turned to one hue, keeping its lightness and chroma: the given hue, or the hue
   of the palette's most chromatic colour. */
void sleeve_palette_monochrome(sleeve_builder *b, const void *params) {
  const sleeve_monochrome_params *p = params ? (const sleeve_monochrome_params *)params
                                             : &sleeve__monochrome_defaults;
  int32_t l[SLEEVE_ROLES], c[SLEEVE_ROLES], h[SLEEVE_ROLES], hue = p->hue;
  int i, key = 0;
  for (i = 0; i < SLEEVE_ROLES; i++) {
    sleeve_to_oklch(b->palette[i], &l[i], &c[i], &h[i]);
    if (c[i] > c[key]) key = i;
  }
  if (hue < 0) hue = h[key];
  for (i = 0; i < SLEEVE_ROLES; i++) b->palette[i] = sleeve_oklch(l[i], c[i], hue);
}

/* Every colour's lightness mapped from 0 to 1000 onto lo to hi. */
void sleeve_palette_lightness(sleeve_builder *b, const void *params) {
  const sleeve_lightness_params *p = params ? (const sleeve_lightness_params *)params
                                            : &sleeve__lightness_defaults;
  int i;
  for (i = 0; i < SLEEVE_ROLES; i++) {
    int32_t l, c, h;
    sleeve_to_oklch(b->palette[i], &l, &c, &h);
    l = p->lo + (int32_t)sleeve__div_round((int64_t)l * (p->hi - p->lo), 1000);
    b->palette[i] = sleeve_oklch(l, c, h);
  }
}

/* Two inks: the darkest colour becomes the dark ink, the lightest the light one, and the rest
   fall between them by their lightness. */
void sleeve_palette_duotone(sleeve_builder *b, const void *params) {
  const sleeve_duotone_params *p = params ? (const sleeve_duotone_params *)params
                                          : &sleeve__duotone_defaults;
  int32_t l[SLEEVE_ROLES], c, h, lo = 1000, hi = 0;
  int i;
  for (i = 0; i < SLEEVE_ROLES; i++) {
    sleeve_to_oklch(b->palette[i], &l[i], &c, &h);
    if (l[i] < lo) lo = l[i];
    if (l[i] > hi) hi = l[i];
  }
  for (i = 0; i < SLEEVE_ROLES; i++) {
    int32_t t = hi > lo ? (int32_t)sleeve__div_round((int64_t)(l[i] - lo) * 1000, hi - lo) : 500;
    b->palette[i] = sleeve__mix_colour(sleeve__rgb(p->dark), sleeve__rgb(p->light), t);
  }
}

/* ---- Finishers --------------------------------------------------------------------------- */

static const sleeve_vignette_params sleeve__vignette_defaults = {SLEEVE_DEEP, 110, 4500, 7500};
static const sleeve_wash_params sleeve__wash_defaults = {SLEEVE_PALE, 40};
static const sleeve_frame_params sleeve__frame_defaults = {SLEEVE_PALE, 255, 600};
static const sleeve_lines_params sleeve__lines_defaults = {SLEEVE_DEEP, 36, 45, 160, 60};

static sleeve_colour sleeve__role_colour(const sleeve_builder *b, int32_t role, int32_t alpha) {
  return sleeve__alpha(b->palette[sleeve__clamp(role, 0, SLEEVE_ROLES - 1)], alpha);
}

/* The edges darkened toward a colour, from `inner` out from the centre to full at `outer`. */
void sleeve_finish_vignette(sleeve_builder *b, const void *params) {
  const sleeve_vignette_params *p = params ? (const sleeve_vignette_params *)params
                                           : &sleeve__vignette_defaults;
  sleeve_layer *l = sleeve__add_radial(b, 5000, 5000);
  sleeve_colour c = sleeve__role_colour(b, p->role, p->alpha);
  sleeve__add_stop(l, p->inner, sleeve__alpha(c, 0));
  sleeve__add_stop(l, p->outer, c);
}

/* A translucent colour over the whole cover. */
void sleeve_finish_wash(sleeve_builder *b, const void *params) {
  const sleeve_wash_params *p = params ? (const sleeve_wash_params *)params
                                       : &sleeve__wash_defaults;
  sleeve__add_stop(sleeve__add_linear(b, 0), 0, sleeve__role_colour(b, p->role, p->alpha));
}

/* A border `width` wide around the cover, as two layers. */
void sleeve_finish_frame(sleeve_builder *b, const void *params) {
  const sleeve_frame_params *p = params ? (const sleeve_frame_params *)params
                                        : &sleeve__frame_defaults;
  sleeve_colour c = sleeve__role_colour(b, p->role, p->alpha), clear = sleeve__alpha(c, 0);
  int side;
  for (side = 0; side < 2; side++) {
    sleeve_layer *l = sleeve__add_linear(b, side ? 180 : 90);
    sleeve__add_band(l, 0, p->width, c);
    sleeve__add_band(l, p->width, SLEEVE_UNIT - p->width, clear);
    sleeve__add_band(l, SLEEVE_UNIT - p->width, SLEEVE_UNIT, c);
  }
}

/* Fine repeating lines over the whole cover. */
void sleeve_finish_lines(sleeve_builder *b, const void *params) {
  const sleeve_lines_params *p = params ? (const sleeve_lines_params *)params
                                        : &sleeve__lines_defaults;
  sleeve_layer *l = sleeve__add_layer(b, SLEEVE_LINEAR, 1);
  sleeve_colour c = sleeve__role_colour(b, p->role, p->alpha);
  l->angle = p->angle;
  sleeve__add_band(l, 0, p->pitch - p->width, sleeve__alpha(c, 0));
  sleeve__add_band(l, p->pitch - p->width, p->pitch, c);
}

/* ---- Built-in pieces --------------------------------------------------------------------- */

#define SLEEVE__CHANCE(name, value) {name, 0, 100, value, -1}
#define SLEEVE__COUNT(array) ((int)(sizeof array / sizeof array[0]))

static const sleeve_param sleeve__horizon_params[] = {
  SLEEVE__CHANCE("night", 40), SLEEVE__CHANCE("level", 60), {"tilt", 0, 30, 8, -1},
  {"horizon_min", 200, 800, 420, 4}, {"horizon_max", 200, 800, 650, -1},
  SLEEVE__CHANCE("sun", 85), SLEEVE__CHANCE("setting", 30),
  {"sun_min", 300, 2500, 700, 8}, {"sun_max", 300, 2500, 1500, -1},
  SLEEVE__CHANCE("band", 55), SLEEVE__CHANCE("lines", 35),
};
static const sleeve_param sleeve__glow_params[] = {
  SLEEVE__CHANCE("dusk", 60), {"radius_min", 2000, 9000, 4500, 2},
  {"radius_max", 2000, 9000, 6500, -1}, {"alpha_min", 0, 255, 170, 4},
  {"alpha_max", 0, 255, 220, -1}, SLEEVE__CHANCE("sun", 55),
};
static const sleeve_param sleeve__disc_params[] = {
  SLEEVE__CHANCE("record", 35), SLEEVE__CHANCE("centred", 60),
  {"label_min", 300, 2500, 900, 3}, {"label_max", 300, 2500, 1400, -1},
  {"rings_min", 1, 3, 2, 5}, {"rings_max", 1, 3, 3, -1},
  SLEEVE__CHANCE("halo", 40), SLEEVE__CHANCE("wash", 40),
};
static const sleeve_param sleeve__pair_params[] = {
  SLEEVE__CHANCE("dark", 60), {"spread_min", 1000, 3000, 1800, 2},
  {"spread_max", 1000, 3000, 2300, -1}, {"hub_min", 300, 1800, 800, 4},
  {"hub_max", 300, 1800, 1300, -1}, SLEEVE__CHANCE("strip", 50), SLEEVE__CHANCE("spots", 60),
};
static const sleeve_param sleeve__stripes_params[] = {
  SLEEVE__CHANCE("solid", 65), {"colours_min", 2, 4, 2, 2}, {"colours_max", 2, 4, 4, -1},
  {"wobble", 0, 20, 4, -1}, SLEEVE__CHANCE("disc", 40),
};
static const sleeve_param sleeve__bands_params[] = {
  SLEEVE__CHANCE("sheen", 25), {"bands_min", 2, 4, 3, 2}, {"bands_max", 2, 4, 4, -1},
  SLEEVE__CHANCE("thin", 40), SLEEVE__CHANCE("tilt", 30), SLEEVE__CHANCE("disc", 30),
};
static const sleeve_param sleeve__split_params[] = {
  {"angle_min", 0, 180, 20, 1}, {"angle_max", 0, 180, 160, -1}, SLEEVE__CHANCE("cross", 45),
  SLEEVE__CHANCE("disc", 55), {"disc_min", 300, 3500, 1000, 5}, {"disc_max", 300, 3500, 2200, -1},
};
static const sleeve_param sleeve__bars_params[] = {
  SLEEVE__CHANCE("vertical", 70), {"bars_min", 1, 6, 2, 2}, {"bars_max", 1, 6, 3, -1},
  SLEEVE__CHANCE("tilt", 25),
};
static const sleeve_param sleeve__constellation_params[] = {
  {"stars_min", 1, 9, 4, 1}, {"stars_max", 1, 9, 9, -1}, SLEEVE__CHANCE("moon", 35),
  {"star_min", 100, 800, 200, 4}, {"star_max", 100, 800, 450, -1},
};
static const sleeve_param sleeve__grooves_params[] = {
  SLEEVE__CHANCE("lines", 25), SLEEVE__CHANCE("two_tone", 50), SLEEVE__CHANCE("disc", 20),
};
static const sleeve_param sleeve__orbs_params[] = {
  SLEEVE__CHANCE("wash", 50), SLEEVE__CHANCE("edge", 50), SLEEVE__CHANCE("oval", 40),
};
static const sleeve_param sleeve__checks_params[] = {
  {"tile_min", 150, 2500, 400, 1}, {"tile_max", 150, 2500, 1000, -1}, SLEEVE__CHANCE("dark", 50),
  SLEEVE__CHANCE("disc", 60),
};
static const sleeve_param sleeve__tint_params[] = {
  {"colour", 0, 0xffffff, 0xf2e3c6, -1}, {"amount", 0, 1000, 250, -1},
};
static const sleeve_param sleeve__chroma_params[] = {{"scale", 0, 2000, 500, -1}};
static const sleeve_param sleeve__monochrome_params[] = {{"hue", -1, 359, -1, -1}};
static const sleeve_param sleeve__lightness_params[] = {
  {"lo", 0, 1000, 150, 1}, {"hi", 0, 1000, 900, -1},
};
static const sleeve_param sleeve__duotone_params[] = {
  {"dark", 0, 0xffffff, 0x1d2b4f, -1}, {"light", 0, 0xffffff, 0xf3e8d2, -1},
};
static const sleeve_param sleeve__vignette_params[] = {
  {"role", 0, 4, SLEEVE_DEEP, -1}, {"alpha", 0, 255, 110, -1}, {"inner", 0, 14000, 4500, 3},
  {"outer", 0, 14000, 7500, -1},
};
static const sleeve_param sleeve__wash_params[] = {
  {"role", 0, 4, SLEEVE_PALE, -1}, {"alpha", 0, 255, 40, -1},
};
static const sleeve_param sleeve__frame_params[] = {
  {"role", 0, 4, SLEEVE_PALE, -1}, {"alpha", 0, 255, 255, -1}, {"width", 50, 2500, 600, -1},
};
static const sleeve_param sleeve__lines_params[] = {
  {"role", 0, 4, SLEEVE_DEEP, -1}, {"alpha", 0, 255, 36, -1}, {"angle", 0, 359, 45, -1},
  {"pitch", 40, 2000, 160, -1}, {"width", 10, 2000, 60, 3},
};

#define SLEEVE__PIECE(name, kind, layers)                                                    \
  {#name, sleeve_##kind##_##name, &sleeve__##name##_defaults, sleeve__##name##_params,         \
   SLEEVE__COUNT(sleeve__##name##_params), layers}

static const sleeve_piece sleeve__compositions[SLEEVE_COMPOSITIONS] = {
  SLEEVE__PIECE(horizon, compose, 0), SLEEVE__PIECE(glow, compose, 0),
  SLEEVE__PIECE(disc, compose, 0), SLEEVE__PIECE(pair, compose, 0),
  SLEEVE__PIECE(stripes, compose, 0), SLEEVE__PIECE(bands, compose, 0),
  SLEEVE__PIECE(split, compose, 0), SLEEVE__PIECE(bars, compose, 0),
  SLEEVE__PIECE(constellation, compose, 0), SLEEVE__PIECE(grooves, compose, 0),
  SLEEVE__PIECE(orbs, compose, 0), SLEEVE__PIECE(checks, compose, 0),
};
static const sleeve_piece sleeve__palette_hooks[] = {
  SLEEVE__PIECE(tint, palette, 0), SLEEVE__PIECE(chroma, palette, 0),
  SLEEVE__PIECE(monochrome, palette, 0), SLEEVE__PIECE(lightness, palette, 0),
  SLEEVE__PIECE(duotone, palette, 0),
};
static const sleeve_piece sleeve__finishers[] = {
  SLEEVE__PIECE(vignette, finish, 1), SLEEVE__PIECE(wash, finish, 1),
  SLEEVE__PIECE(frame, finish, 2), SLEEVE__PIECE(lines, finish, 1),
};

static int sleeve__same_name(const char *a, const char *b) {
  if (!a || !b) return 0;
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}

const sleeve_piece *sleeve_pieces(int kind, int *count) {
  const sleeve_piece *list = 0;
  int n = 0;
  switch (kind) {
  case SLEEVE_COMPOSE:
    list = sleeve__compositions;
    n = SLEEVE__COUNT(sleeve__compositions);
    break;
  case SLEEVE_PALETTE:
    list = sleeve__palette_hooks;
    n = SLEEVE__COUNT(sleeve__palette_hooks);
    break;
  case SLEEVE_FINISH:
    list = sleeve__finishers;
    n = SLEEVE__COUNT(sleeve__finishers);
    break;
  }
  if (count) *count = n;
  return list;
}

const sleeve_piece *sleeve_find_piece(int kind, const char *name) {
  int n, i;
  const sleeve_piece *list = sleeve_pieces(kind, &n);
  for (i = 0; i < n; i++)
    if (sleeve__same_name(list[i].name, name)) return &list[i];
  return 0;
}

int sleeve_find_param(const sleeve_piece *piece, const char *name) {
  int i;
  for (i = 0; piece && i < piece->param_count; i++)
    if (sleeve__same_name(piece->params[i].name, name)) return i;
  return -1;
}

const sleeve_mood_def *sleeve_find_mood(const char *name) {
  int i;
  for (i = 0; i < SLEEVE_MOODS; i++)
    if (sleeve__same_name(sleeve_moods[i].name, name)) return &sleeve_moods[i];
  return 0;
}

/* The built-in piece of a kind a function is, or NULL for a code hook. */
static const sleeve_piece *sleeve__builtin(int kind, sleeve_hook run) {
  int n, i;
  const sleeve_piece *list = sleeve_pieces(kind, &n);
  for (i = 0; i < n; i++)
    if (list[i].run == run) return &list[i];
  return 0;
}

/* ---- The default style ------------------------------------------------------------------- */

/* The neon mood's accents stay small marks on a dark ground, so it has weights of its own. */
static const uint16_t sleeve__neon_weights[SLEEVE_COMPOSITIONS] = {14, 6, 12, 4, 0, 0,
                                                                  0, 18, 14, 0, 0, 0};

static const sleeve_style_mood sleeve__default_moods[SLEEVE_MOODS] = {
  {&sleeve_moods[SLEEVE_DUSK], 12, 0}, {&sleeve_moods[SLEEVE_SEA], 12, 0},
  {&sleeve_moods[SLEEVE_EARTH], 11, 0}, {&sleeve_moods[SLEEVE_PAPER_WARM], 9, 0},
  {&sleeve_moods[SLEEVE_PAPER_COOL], 8, 0}, {&sleeve_moods[SLEEVE_NEON], 9, sleeve__neon_weights},
  {&sleeve_moods[SLEEVE_MIST], 10, 0}, {&sleeve_moods[SLEEVE_GARDEN], 10, 0},
  {&sleeve_moods[SLEEVE_DUOTONE], 10, 0}, {&sleeve_moods[SLEEVE_EMBER], 9, 0},
};

static const sleeve_style_part sleeve__default_compositions[SLEEVE_COMPOSITIONS] = {
  {"horizon", sleeve_compose_horizon, 0, 14, 0, 0},
  {"glow", sleeve_compose_glow, 0, 6, 0, 0},
  {"disc", sleeve_compose_disc, 0, 12, 0, 0},
  {"pair", sleeve_compose_pair, 0, 4, 0, 0},
  {"stripes", sleeve_compose_stripes, 0, 12, 0, 0},
  {"bands", sleeve_compose_bands, 0, 10, 0, 0},
  {"split", sleeve_compose_split, 0, 12, 0, 0},
  {"bars", sleeve_compose_bars, 0, 6, 0, 0},
  {"constellation", sleeve_compose_constellation, 0, 7, 0, 0},
  {"grooves", sleeve_compose_grooves, 0, 6, 0, 0},
  {"orbs", sleeve_compose_orbs, 0, 8, 0, 0},
  {"checks", sleeve_compose_checks, 0, 3, 0, 0},
};

const sleeve_style sleeve_default_style = {
  "default", sleeve__default_moods, SLEEVE_MOODS, sleeve__default_compositions,
  SLEEVE_COMPOSITIONS, 0, 0, 0, 0,
};

/* A style's parts with the default's in place of empty ones, and counts kept in bounds. */
typedef struct sleeve__parts {
  const sleeve_style_mood *moods;
  const sleeve_style_part *compositions, *palette_hooks, *finishers;
  int mood_count, composition_count, palette_hook_count, finisher_count;
  int rows; /* whether moods' composition weights apply */
} sleeve__parts;

static void sleeve__resolve(const sleeve_style *style, sleeve__parts *p) {
  const sleeve_style *d = &sleeve_default_style;
  if (!style) style = d;
  p->rows = style->mood_count > 0 || style->composition_count <= 0;
  if (style->mood_count > 0) p->moods = style->moods, p->mood_count = style->mood_count;
  else p->moods = d->moods, p->mood_count = d->mood_count;
  if (style->composition_count > 0)
    p->compositions = style->compositions, p->composition_count = style->composition_count;
  else p->compositions = d->compositions, p->composition_count = d->composition_count;
  p->palette_hooks = style->palette_hooks;
  p->palette_hook_count = style->palette_hook_count > 0 ? style->palette_hook_count : 0;
  p->finishers = style->finishers;
  p->finisher_count = style->finisher_count > 0 ? style->finisher_count : 0;
  if (p->mood_count > SLEEVE_STYLE_MAX_MOODS) p->mood_count = SLEEVE_STYLE_MAX_MOODS;
  if (p->composition_count > SLEEVE_STYLE_MAX_COMPOSITIONS)
    p->composition_count = SLEEVE_STYLE_MAX_COMPOSITIONS;
  if (p->palette_hook_count > SLEEVE_STYLE_MAX_HOOKS)
    p->palette_hook_count = SLEEVE_STYLE_MAX_HOOKS;
  if (p->finisher_count > SLEEVE_STYLE_MAX_HOOKS) p->finisher_count = SLEEVE_STYLE_MAX_HOOKS;
}

/* ---- Generating -------------------------------------------------------------------------- */

/* Whether the renderers accept a layer. */
static int sleeve__layer_ok(const sleeve_layer *l) {
  const int32_t far = 100 * SLEEVE_UNIT;
  int j;
  if (l->stop_count < 1 || l->stop_count > SLEEVE_MAX_STOPS || l->repeating > 1) return 0;
  if (l->cx < -far || l->cx > far || l->cy < -far || l->cy > far) return 0;
  for (j = 0; j < l->stop_count; j++) {
    if (l->stops[j].at < -far || l->stops[j].at > far) return 0;
    if (j > 0 && l->stops[j].at < l->stops[j - 1].at) return 0;
  }
  if (l->repeating && (l->stops[0].at != 0 || l->stops[l->stop_count - 1].at <= 0)) return 0;
  switch (l->kind) {
  case SLEEVE_LINEAR: return 1;
  case SLEEVE_RADIAL: return l->ex > 0 && l->ey > 0 && l->ex <= far && l->ey <= far;
  case SLEEVE_CHECKER: return l->stop_count == 2 && !l->repeating && l->ex > 0 && l->ex <= far;
  default: return 0;
  }
}

/* The pick of sleeve__pick, from the weights of a style's moods or parts. */
static int sleeve__pick_mood(sleeve_rng *r, const sleeve_style_mood *moods, int n) {
  int32_t total = 0, x;
  int i;
  for (i = 0; i < n; i++) total += moods[i].weight;
  x = sleeve__below(r, total);
  for (i = 0; i < n - 1; i++) {
    if (x < moods[i].weight) return i;
    x -= moods[i].weight;
  }
  return n > 0 ? n - 1 : 0;
}

static int sleeve__pick_part(sleeve_rng *r, const sleeve_style_part *parts, int n) {
  int32_t total = 0, x;
  int i;
  for (i = 0; i < n; i++) total += parts[i].weight;
  x = sleeve__below(r, total);
  for (i = 0; i < n - 1; i++) {
    if (x < parts[i].weight) return i;
    x -= parts[i].weight;
  }
  return n > 0 ? n - 1 : 0;
}

/* Whether every hook that runs is built in, so the layers need no second look. */
static int sleeve__trusted(const sleeve__parts *s, const sleeve_style_part *composition) {
  int i;
  if (!sleeve__builtin(SLEEVE_COMPOSE, composition->run)) return 0;
  for (i = 0; i < s->palette_hook_count; i++)
    if (!sleeve__builtin(SLEEVE_PALETTE, s->palette_hooks[i].run)) return 0;
  for (i = 0; i < s->finisher_count; i++)
    if (!sleeve__builtin(SLEEVE_FINISH, s->finishers[i].run)) return 0;
  return 1;
}

static const sleeve_layer sleeve__no_layer = {0, 0, 0, 0, 0, 0, 0, 0, 0, {{0, {0, 0, 0, 0}}}};

static void sleeve__generate(uint64_t seed, const sleeve_style *style, const sleeve_colour *given,
                             int given_count, sleeve_cover *cover) {
  sleeve__parts s;
  sleeve_builder b;
  sleeve_colour chosen[5];
  const uint16_t *row = 0;
  const sleeve_style_part *part;
  int i, n, mood = SLEEVE_NO_MOOD, composition, reserved = 0;
  sleeve__resolve(style, &s);
  b.cover = cover;
  b.limit = 0;
  /* SplitMix64 from neighbouring states starts out correlated, so the seed is mixed first */
  b.rng.state = sleeve__mix(seed ^ 0x736c65657665ull);
  cover->generator = SLEEVE_GENERATOR;
  cover->layer_count = 0;
  if (!given) {
    mood = sleeve__pick_mood(&b.rng, s.moods, s.mood_count);
    if (s.rows) row = s.moods[mood].composition_weights;
  }
  composition = row ? sleeve__pick(&b.rng, row, s.composition_count)
              : sleeve__pick_part(&b.rng, s.compositions, s.composition_count);
  cover->mood = (uint8_t)mood;
  cover->composition = (uint8_t)composition;
  if (given) {
    sleeve__assign(given, given_count, b.palette);
  } else {
    sleeve__palette(&b.rng, s.moods[mood].mood ? s.moods[mood].mood : &sleeve_moods[0], b.palette);
    for (i = 0; i < s.palette_hook_count; i++)
      if (s.palette_hooks[i].run) s.palette_hooks[i].run(&b, s.palette_hooks[i].params);
    for (i = 0; s.palette_hook_count > 0 && i < 5; i++)
      b.palette[i] = sleeve__alpha(b.palette[i], 255);
  }
  cover->palette_count = 5;
  for (i = 0; i < 5; i++) cover->palette[i] = chosen[i] = b.palette[i];
  cover->base = b.palette[SLEEVE_DEEP];
  for (i = 0; i < s.finisher_count; i++) reserved += s.finishers[i].layers;
  if (reserved > SLEEVE_STYLE_MAX_RESERVED) reserved = SLEEVE_STYLE_MAX_RESERVED;
  b.limit = SLEEVE_MAX_LAYERS - reserved;
  part = &s.compositions[composition];
  if (part->run) part->run(&b, part->params);
  b.limit = SLEEVE_MAX_LAYERS;
  for (i = 0; i < s.finisher_count; i++)
    if (s.finishers[i].run) s.finishers[i].run(&b, s.finishers[i].params);
  /* whatever the hooks did, the cover is one the renderers accept */
  cover->generator = SLEEVE_GENERATOR;
  cover->mood = (uint8_t)mood;
  cover->composition = (uint8_t)composition;
  cover->palette_count = 5;
  cover->base.a = 255;
  if (cover->layer_count > SLEEVE_MAX_LAYERS) cover->layer_count = SLEEVE_MAX_LAYERS;
  if (style != &sleeve_default_style && !sleeve__trusted(&s, part)) {
    for (i = 0; i < 5; i++) cover->palette[i] = chosen[i];
    n = cover->layer_count;
    cover->layer_count = 0;
    for (i = 0; i < n; i++)
      if (sleeve__layer_ok(&cover->layers[i]))
        cover->layers[cover->layer_count++] = cover->layers[i];
  }
  /* unused layers are zero, so equal covers are equal bytes */
  for (i = cover->layer_count; i < SLEEVE_MAX_LAYERS; i++) cover->layers[i] = sleeve__no_layer;
}

void sleeve_generate(uint64_t seed, sleeve_cover *cover) {
  sleeve__generate(seed, &sleeve_default_style, 0, 0, cover);
}

void sleeve_generate_styled(uint64_t seed, const sleeve_style *style, sleeve_cover *cover) {
  sleeve__generate(seed, style, 0, 0, cover);
}

int sleeve_generate_with_palette(uint64_t seed, const sleeve_style *style,
                                 const sleeve_colour *colours, int count, sleeve_cover *cover) {
  if (!colours || count < 1 || count > SLEEVE_MAX_GIVEN) return -1;
  sleeve__generate(seed, style, colours, count, cover);
  return 0;
}

int sleeve_check(const sleeve_cover *cover) {
  int i;
  if (cover->layer_count > SLEEVE_MAX_LAYERS || cover->base.a != 255) return -1;
  for (i = 0; i < cover->layer_count; i++)
    if (!sleeve__layer_ok(&cover->layers[i])) return -1;
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
    /* a repeating gradient is integrated around `local`, so the periods are small whole numbers */
    k.t = g->repeating ? local : t;
    k.h1 = 0.5f * (a + b);
    k.h2 = 0.5f * (a - b);
    k.inv_a = 1.0f / a;
    k.inv_ab = b > 0 ? 1.0f / (a * b) : 0;
    if (g->repeating) {
      float first = sleeve__floor((local - k.h1) / g->period);
      float last = sleeve__floor((local + k.h1) / g->period);
      int period, periods;
      if (!(last - first <= 32 && first >= -1048576.0f && last <= 1048576.0f)) {
        for (ch = 0; ch < 4; ch++) out[ch] = g->mean[ch];
        return;
      }
      periods = (int)(last - first);
      for (period = (int)first; periods >= 0; period++, periods--)
        sleeve__pieces(g, &k, (float)period * g->period, acc);
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

/* num / den, for den > 0, rounded to `places` decimals with trailing zeros dropped. Only the
   remainder is scaled, so the arithmetic fits wherever the decimals printed do. */
static void sleeve__ratio(sleeve__text *t, int64_t num, int64_t den, int places) {
  int64_t scale = 1, scaled, whole = num / den, frac;
  int i;
  for (i = 0; i < places; i++) scale *= 10;
  scaled = whole * scale + sleeve__div_round(num % den * scale, den);
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

const char *sleeve_style_composition_name(const sleeve_style *style, int composition) {
  sleeve__parts s;
  sleeve__resolve(style, &s);
  if (composition < 0 || composition >= s.composition_count) return "unknown";
  return s.compositions[composition].name ? s.compositions[composition].name : "unknown";
}

const char *sleeve_style_mood_name(const sleeve_style *style, int mood) {
  sleeve__parts s;
  sleeve__resolve(style, &s);
  if (mood == SLEEVE_NO_MOOD) return "given";
  if (mood < 0 || mood >= s.mood_count || !s.moods[mood].mood || !s.moods[mood].mood->name)
    return "unknown";
  return s.moods[mood].mood->name;
}

const char *sleeve_composition_name(int composition) {
  return sleeve_style_composition_name(&sleeve_default_style, composition);
}

const char *sleeve_mood_name(int mood) {
  return sleeve_style_mood_name(&sleeve_default_style, mood);
}

size_t sleeve_describe(const sleeve_cover *cover, char *out, size_t capacity) {
  return sleeve_describe_styled(cover, &sleeve_default_style, out, capacity);
}

size_t sleeve_describe_styled(const sleeve_cover *cover, const sleeve_style *style, char *out,
                              size_t capacity) {
  sleeve__text t;
  int i, j;
  t.out = out;
  t.capacity = capacity;
  t.length = 0;
  sleeve__puts(&t, "sleeve ");
  sleeve__int(&t, cover->generator);
  if (cover->generator) {
    sleeve__puts(&t, " ");
    sleeve__puts(&t, sleeve_style_composition_name(style, cover->composition));
    sleeve__puts(&t, " ");
    sleeve__puts(&t, sleeve_style_mood_name(style, cover->mood));
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

/* ---- Styles: checks and fingerprints ----------------------------------------------------- */

static const char *const sleeve__role_names[SLEEVE_ROLES] = {"deep", "mid", "pale", "accent",
                                                             "accent2"};
static const char *const sleeve__kind_names[3] = {"composition", "palette hook", "finisher"};

/* A reason is written as the checks go and rewound to `mark` by each check that passes. */

/* "what[index] (name): " */
static void sleeve__where(sleeve__text *t, const char *what, int index, const char *name) {
  sleeve__puts(t, what);
  if (index >= 0) {
    sleeve__put(t, '[');
    sleeve__int(t, index);
    sleeve__put(t, ']');
  }
  if (name) {
    sleeve__puts(t, " (");
    sleeve__puts(t, name);
    sleeve__put(t, ')');
  }
  sleeve__puts(t, ": ");
}

/* -1 with "NAME VALUE is outside LO to HI" when the value is out of range, otherwise 0. */
static int sleeve__outside(sleeve__text *t, const char *name, int32_t v, int32_t lo, int32_t hi) {
  if (v >= lo && v <= hi) return 0;
  sleeve__puts(t, name);
  sleeve__put(t, ' ');
  sleeve__int(t, v);
  sleeve__puts(t, " is outside ");
  sleeve__int(t, lo);
  sleeve__puts(t, " to ");
  sleeve__int(t, hi);
  return -1;
}

static int sleeve__check_count(sleeve__text *t, const char *name, int count, const void *list,
                               int most) {
  if (sleeve__outside(t, name, count, 0, most)) return -1;
  if (count > 0 && !list) {
    sleeve__puts(t, name);
    sleeve__puts(t, " is set but its list is NULL");
    return -1;
  }
  return 0;
}

static int sleeve__check_params(sleeve__text *t, const sleeve_piece *piece, const void *params) {
  const int32_t *v = (const int32_t *)(params ? params : piece->defaults);
  int i;
  for (i = 0; i < piece->param_count; i++) {
    const sleeve_param *p = &piece->params[i];
    if (sleeve__outside(t, p->name, v[i], p->lo, p->hi)) return -1;
    if (p->at_most >= 0 && v[i] > v[p->at_most]) {
      sleeve__puts(t, p->name);
      sleeve__put(t, ' ');
      sleeve__int(t, v[i]);
      sleeve__puts(t, " is above ");
      sleeve__puts(t, piece->params[p->at_most].name);
      sleeve__put(t, ' ');
      sleeve__int(t, v[p->at_most]);
      return -1;
    }
  }
  return 0;
}

static int sleeve__check_part(sleeve__text *t, const char *what, int index,
                              const sleeve_style_part *part, int kind) {
  size_t mark = t->length;
  const sleeve_piece *piece;
  int other;
  sleeve__where(t, what, index, part->name);
  if (!part->name || !part->name[0]) return sleeve__puts(t, "has no name"), -1;
  if (!part->run) return sleeve__puts(t, "has no function"), -1;
  piece = sleeve__builtin(kind, part->run);
  if (!piece) {
    for (other = SLEEVE_COMPOSE; other <= SLEEVE_FINISH; other++)
      if (other != kind && (piece = sleeve__builtin(other, part->run)) != 0) {
        sleeve__puts(t, "runs the built-in ");
        sleeve__puts(t, sleeve__kind_names[other]);
        sleeve__puts(t, " \"");
        sleeve__puts(t, piece->name);
        sleeve__puts(t, "\", which is not a ");
        sleeve__puts(t, sleeve__kind_names[kind]);
        return -1;
      }
  } else {
    if (sleeve__check_params(t, piece, part->params)) return -1;
    if (kind == SLEEVE_FINISH && part->layers < piece->layers) {
      sleeve__puts(t, "reserves ");
      sleeve__int(t, part->layers);
      sleeve__puts(t, " layers but \"");
      sleeve__puts(t, piece->name);
      sleeve__puts(t, "\" adds up to ");
      sleeve__int(t, piece->layers);
      return -1;
    }
  }
  t->length = mark;
  return 0;
}

static int sleeve__check_mood(sleeve__text *t, int index, const sleeve_style_mood *m) {
  size_t mark = t->length;
  const sleeve_mood_def *d = m->mood;
  int i;
  sleeve__where(t, "moods", index, d ? d->name : 0);
  if (!d) return sleeve__puts(t, "has no mood definition"), -1;
  if (sleeve__outside(t, "hue_lo", d->hue_lo, 0, 359) ||
      sleeve__outside(t, "hue_hi", d->hue_hi, d->hue_lo, d->hue_lo + 359))
    return -1;
  for (i = 0; i < SLEEVE_ROLES; i++) {
    const sleeve_role_range *r = &d->roles[i];
    size_t role_mark = t->length;
    if (d->exact[i].a == 255) continue;
    sleeve__puts(t, sleeve__role_names[i]);
    sleeve__put(t, ' ');
    if (d->exact[i].a != 0) return sleeve__puts(t, "exact alpha is neither 0 nor 255"), -1;
    if (sleeve__outside(t, "hue_lo", r->hue_lo, -360, 360) ||
        sleeve__outside(t, "hue_hi", r->hue_hi, r->hue_lo, 360) ||
        sleeve__outside(t, "light_lo", r->light_lo, 0, 1000) ||
        sleeve__outside(t, "light_hi", r->light_hi, r->light_lo, 1000) ||
        sleeve__outside(t, "chroma_lo", r->chroma_lo, 0, 400) ||
        sleeve__outside(t, "chroma_hi", r->chroma_hi, r->chroma_lo, 400))
      return -1;
    t->length = role_mark;
  }
  t->length = mark;
  return 0;
}

static int sleeve__check_style(sleeve__text *t, const sleeve_style *style) {
  sleeve__parts s;
  int i, k, reserved = 0;
  int32_t total = 0;
  if (!style) return sleeve__puts(t, "no style"), -1;
  if (sleeve__check_count(t, "mood_count", style->mood_count, style->moods,
                          SLEEVE_STYLE_MAX_MOODS) ||
      sleeve__check_count(t, "composition_count", style->composition_count, style->compositions,
                          SLEEVE_STYLE_MAX_COMPOSITIONS) ||
      sleeve__check_count(t, "palette_hook_count", style->palette_hook_count,
                          style->palette_hooks, SLEEVE_STYLE_MAX_HOOKS) ||
      sleeve__check_count(t, "finisher_count", style->finisher_count, style->finishers,
                          SLEEVE_STYLE_MAX_HOOKS))
    return -1;
  sleeve__resolve(style, &s);
  for (i = 0; i < s.mood_count; i++) {
    const uint16_t *row = s.moods[i].composition_weights;
    if (sleeve__check_mood(t, i, &s.moods[i])) return -1;
    total += s.moods[i].weight;
    if (s.rows && row) {
      int32_t sum = 0;
      for (k = 0; k < s.composition_count; k++) sum += row[k];
      if (sum == 0) {
        sleeve__where(t, "moods", i, s.moods[i].mood->name);
        return sleeve__puts(t, "its composition weights are all 0"), -1;
      }
    }
  }
  if (total == 0) return sleeve__puts(t, "the moods' weights are all 0"), -1;
  for (total = 0, i = 0; i < s.composition_count; i++) {
    if (sleeve__check_part(t, "compositions", i, &s.compositions[i], SLEEVE_COMPOSE)) return -1;
    total += s.compositions[i].weight;
  }
  if (total == 0) return sleeve__puts(t, "the compositions' weights are all 0"), -1;
  for (i = 0; i < s.palette_hook_count; i++)
    if (sleeve__check_part(t, "palette_hooks", i, &s.palette_hooks[i], SLEEVE_PALETTE)) return -1;
  for (i = 0; i < s.finisher_count; i++) {
    if (sleeve__check_part(t, "finishers", i, &s.finishers[i], SLEEVE_FINISH)) return -1;
    reserved += s.finishers[i].layers;
  }
  if (reserved > SLEEVE_STYLE_MAX_RESERVED) {
    sleeve__puts(t, "the finishers reserve ");
    sleeve__int(t, reserved);
    sleeve__puts(t, " layers, more than ");
    sleeve__int(t, SLEEVE_STYLE_MAX_RESERVED);
    return -1;
  }
  return 0;
}

int sleeve_style_check(const sleeve_style *style, char *reason, size_t capacity) {
  sleeve__text t;
  int result;
  t.out = reason;
  t.capacity = reason ? capacity : 0;
  t.length = 0;
  result = sleeve__check_style(&t, style);
  if (result == 0) t.length = 0;
  sleeve__finish(&t);
  return result;
}

static uint64_t sleeve__hash_int(uint64_t h, int64_t v) {
  int k;
  for (k = 0; k < 8; k++) h = (h ^ (((uint64_t)v >> (8 * k)) & 255)) * 0x100000001b3ull;
  return h;
}

static uint64_t sleeve__hash_text(uint64_t h, const char *s) {
  int64_t n = 0;
  if (!s) return sleeve__hash_int(h, -1);
  for (; s[n]; n++) h = (h ^ (uint8_t)s[n]) * 0x100000001b3ull;
  return sleeve__hash_int(h, n);
}

static uint64_t sleeve__hash_part(uint64_t h, const sleeve_style_part *part, int kind) {
  const sleeve_piece *piece = part->run ? sleeve__builtin(kind, part->run) : 0;
  int i;
  if (piece) {
    const int32_t *v = (const int32_t *)(part->params ? part->params : piece->defaults);
    h = sleeve__hash_int(h, 1);
    h = sleeve__hash_text(h, piece->name);
    for (i = 0; i < piece->param_count; i++) h = sleeve__hash_int(h, v[i]);
  } else {
    h = sleeve__hash_int(h, 2);
    h = sleeve__hash_text(h, part->name);
    h = sleeve__hash_int(h, part->version);
  }
  if (kind == SLEEVE_COMPOSE) h = sleeve__hash_int(h, part->weight);
  if (kind == SLEEVE_FINISH) h = sleeve__hash_int(h, part->layers);
  return h;
}

uint64_t sleeve_style_hash(const sleeve_style *style) {
  sleeve__parts s;
  uint64_t h = sleeve__hash_int(0xcbf29ce484222325ull, SLEEVE_GENERATOR);
  int i, k;
  sleeve__resolve(style, &s);
  h = sleeve__hash_int(h, s.mood_count);
  for (i = 0; i < s.mood_count; i++) {
    const sleeve_mood_def *d = s.moods[i].mood ? s.moods[i].mood : &sleeve_moods[0];
    const uint16_t *row = s.rows ? s.moods[i].composition_weights : 0;
    h = sleeve__hash_int(h, d->hue_lo);
    h = sleeve__hash_int(h, d->hue_hi);
    for (k = 0; k < SLEEVE_ROLES; k++) {
      const sleeve_role_range *r = &d->roles[k];
      if (d->exact[k].a == 255) {
        h = sleeve__hash_int(h, 1);
        h = sleeve__hash_int(h, ((int64_t)d->exact[k].r << 16) | (d->exact[k].g << 8) |
                                    d->exact[k].b);
        continue;
      }
      h = sleeve__hash_int(h, 0);
      h = sleeve__hash_int(h, r->hue_lo);
      h = sleeve__hash_int(h, r->hue_hi);
      h = sleeve__hash_int(h, r->light_lo);
      h = sleeve__hash_int(h, r->light_hi);
      h = sleeve__hash_int(h, r->chroma_lo);
      h = sleeve__hash_int(h, r->chroma_hi);
    }
    h = sleeve__hash_int(h, s.moods[i].weight);
    h = sleeve__hash_int(h, row != 0);
    for (k = 0; row && k < s.composition_count; k++) h = sleeve__hash_int(h, row[k]);
  }
  h = sleeve__hash_int(h, s.composition_count);
  for (i = 0; i < s.composition_count; i++)
    h = sleeve__hash_part(h, &s.compositions[i], SLEEVE_COMPOSE);
  h = sleeve__hash_int(h, s.palette_hook_count);
  for (i = 0; i < s.palette_hook_count; i++)
    h = sleeve__hash_part(h, &s.palette_hooks[i], SLEEVE_PALETTE);
  h = sleeve__hash_int(h, s.finisher_count);
  for (i = 0; i < s.finisher_count; i++) h = sleeve__hash_part(h, &s.finishers[i], SLEEVE_FINISH);
  return sleeve__mix(h);
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
  int y, x, n, run = 0, previous = -1;
  if (width < 1 || height < 1 || width > 16384 || height > 16384) return 0;
  n = width * 3;
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
