/* A libFuzzer harness for sleeve, built and run by `make fuzz` under ASan and UBSan. Each input
   is a seed and choices for one of four kinds of cover:
   - a hand-written layer at the edges of what sleeve_check accepts: any angle, centres and
     shapes from one unit to 100 sides, stops anywhere within 100 sides, plain or repeating;
   - a style of the most moods and compositions, every weight at its largest, the widest mood,
     palette hooks and finishers with parameters at their ends;
   - a given palette of 0 to 17 colours;
   - a code hook calling every builder call with arbitrary arguments.
   Whatever it draws, the cover passes sleeve_check, generates the same twice, renders, and
   writes as SVG, text and PNG into buffers of any capacity. A sanitizer report, a timeout or
   a failed assertion is a failure. */

#define SLEEVE_NO_STDIO
#define SLEEVE_IMPLEMENTATION
#include "sleeve.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct input {
  const uint8_t *data;
  size_t size, at;
} input;

static uint32_t byte(input *in) {
  return in->size ? in->data[in->at++ % in->size] : 0;
}

static int32_t word(input *in) {
  uint32_t v = byte(in);
  v = v << 8 | byte(in);
  v = v << 8 | byte(in);
  v = v << 8 | byte(in);
  return (int32_t)v;
}

/* A value in [lo, hi], often one of its ends. */
static int32_t within(input *in, int32_t lo, int32_t hi) {
  uint32_t pick = byte(in) % 4;
  if (pick == 0) return lo;
  if (pick == 1) return hi;
  return (int32_t)(lo + (int64_t)((uint32_t)word(in) % ((uint64_t)hi - lo + 1)));
}

static sleeve_colour colour(input *in) {
  sleeve_colour c;
  c.r = (uint8_t)byte(in);
  c.g = (uint8_t)byte(in);
  c.b = (uint8_t)byte(in);
  c.a = (uint8_t)byte(in);
  return c;
}

static void edge_layer(input *in, sleeve_cover *c) {
  const int32_t far = 100 * SLEEVE_UNIT;
  sleeve_layer *l = &c->layers[0];
  int i;
  c->base = colour(in);
  c->base.a = 255;
  c->layer_count = 1;
  l->kind = (uint8_t)(1 + byte(in) % 3);
  l->repeating = l->kind != SLEEVE_CHECKER && (byte(in) & 1);
  l->angle = word(in);
  l->cx = within(in, -far, far);
  l->cy = within(in, -far, far);
  l->ex = within(in, 1, far);
  l->ey = within(in, 1, far);
  l->stop_count = (uint8_t)(l->kind == SLEEVE_CHECKER ? 2
                            : l->repeating ? 2 + byte(in) % (SLEEVE_MAX_STOPS - 1)
                                           : 1 + byte(in) % SLEEVE_MAX_STOPS);
  for (i = 0; i < l->stop_count; i++) {
    l->stops[i].at = within(in, i ? l->stops[i - 1].at : -far, far);
    l->stops[i].colour = colour(in);
  }
  if (l->repeating) {
    l->stops[0].at = 0;
    for (i = 1; i < l->stop_count; i++)
      if (l->stops[i].at < l->stops[i - 1].at) l->stops[i].at = l->stops[i - 1].at;
    if (l->stops[l->stop_count - 1].at <= 0) l->stops[l->stop_count - 1].at = within(in, 1, far);
  }
}

static input *hook_input;

/* Every builder call with arbitrary arguments. */
static void wild(sleeve_builder *b, const void *params) {
  input *in = hook_input;
  static uint16_t weights[SLEEVE_MAX_PICK + 1];
  int calls = (int)(byte(in) % 40), i;
  (void)params;
  for (i = 0; i <= SLEEVE_MAX_PICK; i++) weights[i] = UINT16_MAX;
  for (i = 0; i < calls; i++) {
    sleeve_layer *l;
    int32_t a = word(in), x = word(in), y = word(in);
    switch (byte(in) % 12) {
    case 0: (void)sleeve_below(b, a); break;
    case 1: (void)sleeve_range(b, a, x); break;
    case 2: (void)sleeve_chance(b, a); break;
    case 3: (void)sleeve_pick(b, weights, (int)(byte(in) % 4 == 0 ? a : x % 40000)); break;
    case 4:
      l = sleeve_add_layer(b, (int)(a % 5), (int)x);
      l->angle = y;
      sleeve_add_stop(l, word(in), colour(in));
      break;
    case 5:
      l = sleeve_add_linear(b, a);
      sleeve_add_band(l, sleeve_along(l, x), sleeve_along(l, y), colour(in));
      break;
    case 6:
      l = sleeve_add_radial(b, a, x);
      l->repeating = 1;
      sleeve_add_band(l, 0, y, colour(in));
      break;
    case 7: sleeve_add_disc(b, a, x, y, colour(in)); break;
    case 8: sleeve_add_glow(b, a, x, y, word(in), word(in), word(in), colour(in), word(in)); break;
    case 9: b->palette[byte(in) % SLEEVE_ROLES] = sleeve_oklch(a, x, y); break;
    case 10: b->palette[byte(in) % SLEEVE_ROLES] = sleeve_mix(colour(in), colour(in), a); break;
    default: {
      int32_t l2, c2, h2;
      sleeve_to_oklch(sleeve_alpha(colour(in), a), &l2, &c2, &h2);
      (void)sleeve_layers_left(b);
    }
    }
  }
}

/* A built-in piece's parameters, each at one of its ends or between. */
static void edge_params(input *in, const sleeve_piece *piece, sleeve_params *p) {
  int i;
  for (i = 0; i < piece->param_count; i++)
    p->v[i] = within(in, piece->params[i].lo, piece->params[i].hi);
  for (i = 0; i < piece->param_count; i++)
    if (piece->params[i].at_most >= 0 && p->v[i] > p->v[piece->params[i].at_most])
      p->v[i] = p->v[piece->params[i].at_most];
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static sleeve_style_part comps[SLEEVE_STYLE_MAX_COMPOSITIONS], hooks[SLEEVE_STYLE_MAX_HOOKS],
      fins[3];
  static sleeve_style_mood moods[SLEEVE_STYLE_MAX_MOODS];
  static sleeve_params comp_params[SLEEVE_STYLE_MAX_COMPOSITIONS], hook_params[8], fin_params[3];
  static sleeve_mood_def mood;
  static char text[4096];
  input in = {data, size, 0};
  sleeve_cover c, again;
  sleeve_style s;
  uint64_t seed = sleeve_hash(data, size);
  int w, h, i, n, kind = (int)(byte(&in) % 4);
  uint8_t *pixels, *out;
  size_t capacity, length;
  memset(&c, 0, sizeof c);
  memset(&s, 0, sizeof s);
  if (kind == 0) {
    edge_layer(&in, &c);
  } else if (kind == 1) {
    const sleeve_piece *pieces = sleeve_pieces(SLEEVE_COMPOSE, &n);
    const sleeve_piece *palettes = sleeve_pieces(SLEEVE_PALETTE, &w);
    const sleeve_piece *finishers = sleeve_pieces(SLEEVE_FINISH, &h);
    mood = sleeve_moods[0];
    mood.hue_lo = 359;
    mood.hue_hi = 718;
    for (i = 0; i < SLEEVE_ROLES; i++) {
      sleeve_role_range wide = {-360, 360, 0, 1000, 0, 400};
      mood.roles[i] = wide;
    }
    for (i = 0; i < SLEEVE_STYLE_MAX_COMPOSITIONS; i++) {
      const sleeve_piece *piece = &pieces[i % n];
      edge_params(&in, piece, &comp_params[i]);
      memset(&comps[i], 0, sizeof comps[i]);
      comps[i].name = piece->name;
      comps[i].run = piece->run;
      comps[i].params = &comp_params[i];
      comps[i].weight = UINT16_MAX;
      moods[i].mood = &mood;
      moods[i].weight = UINT16_MAX;
      moods[i].composition_weights = 0;
    }
    for (i = 0; i < SLEEVE_STYLE_MAX_HOOKS; i++) {
      const sleeve_piece *piece = &palettes[byte(&in) % (uint32_t)w];
      edge_params(&in, piece, &hook_params[i]);
      memset(&hooks[i], 0, sizeof hooks[i]);
      hooks[i].name = piece->name;
      hooks[i].run = piece->run;
      hooks[i].params = &hook_params[i];
    }
    for (i = 0; i < 3; i++) {
      const sleeve_piece *piece = &finishers[byte(&in) % (uint32_t)h];
      edge_params(&in, piece, &fin_params[i]);
      memset(&fins[i], 0, sizeof fins[i]);
      fins[i].name = piece->name;
      fins[i].run = piece->run;
      fins[i].params = &fin_params[i];
      fins[i].layers = 2;
    }
    s.moods = moods;
    s.mood_count = SLEEVE_STYLE_MAX_MOODS;
    s.compositions = comps;
    s.composition_count = SLEEVE_STYLE_MAX_COMPOSITIONS;
    s.palette_hooks = hooks;
    s.palette_hook_count = (int)(byte(&in) % (SLEEVE_STYLE_MAX_HOOKS + 1));
    s.finishers = fins;
    s.finisher_count = (int)(byte(&in) % 4);
    assert(sleeve_style_check(&s, text, sizeof text) == 0);
    (void)sleeve_style_hash(&s);
    memset(&again, 0xa5, sizeof again);
    sleeve_generate_styled(seed, &s, &c);
    sleeve_generate_styled(seed, &s, &again);
    assert(memcmp(&c, &again, sizeof c) == 0);
  } else if (kind == 2) {
    sleeve_colour given[SLEEVE_MAX_GIVEN + 1];
    int count = (int)(byte(&in) % (SLEEVE_MAX_GIVEN + 2));
    for (i = 0; i <= SLEEVE_MAX_GIVEN; i++) given[i] = colour(&in);
    memset(&c, 0xa5, sizeof c);
    again = c;
    if (sleeve_generate_with_palette(seed, NULL, given, count, &c) != 0) {
      assert(count == 0 || count > SLEEVE_MAX_GIVEN);
      assert(memcmp(&c, &again, sizeof c) == 0);
      return 0;
    }
    assert(count >= 1 && count <= SLEEVE_MAX_GIVEN);
    assert(sleeve_assign_palette(given, count, given) == 0);
  } else {
    sleeve_style_part part = {"wild", wild, 0, 1, 0, 1};
    s.compositions = &part;
    s.composition_count = 1;
    assert(sleeve_style_check(&s, NULL, 0) == 0);
    hook_input = &in;
    sleeve_generate_styled(seed, &s, &c);
  }
  assert(sleeve_check(&c) == 0);
  (void)sleeve_describe(&c, text, byte(&in) % sizeof text);
  switch (byte(&in) % 16) {
  case 0: w = 1, h = 16384; break;
  case 1: w = 16384, h = 1; break;
  default: w = 1 + (int)(byte(&in) % 48), h = 1 + (int)(byte(&in) % 48);
  }
  /* a tall cover renders a few of its rows, and a wide one comes seldom, so inputs stay quick */
  pixels = malloc((size_t)w * 4 * (size_t)(h < 4 ? h : 4));
  assert(pixels);
  assert(sleeve_render_rows(&c, w, h, 0, h < 4 ? h : 4, pixels, (ptrdiff_t)w * 4) == 0);
  length = sleeve_svg(&c, w, h, NULL, 0);
  assert(length > 0);
  capacity = byte(&in) % 2 ? length + 1 : byte(&in);
  out = malloc(capacity ? capacity : 1);
  assert(out);
  assert(sleeve_svg(&c, w, h, (char *)out, capacity) == length);
  if (capacity) assert(memchr(out, 0, capacity));
  free(out);
  capacity = byte(&in) % 2 ? sleeve_png_bound(w, 1) : byte(&in) * 16;
  out = malloc(capacity ? capacity : 1);
  assert(out);
  length = sleeve_png(pixels, w, 1, (ptrdiff_t)w * 4, out, capacity);
  assert(length <= capacity && (capacity < sleeve_png_bound(w, 1) || length > 0));
  free(out);
  free(pixels);
  return 0;
}
