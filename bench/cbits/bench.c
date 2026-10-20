/* The C side of the language measurement: sleeve.h itself, and the calls the Haskell port is
   compared with and timed against. */

#define SLEEVE_IMPLEMENTATION
#define SLEEVE_NO_STDIO
#include "../../sleeve.h"

#include "bench.h"

static void sleeve_bench_flatten(const sleeve_cover *cover, int32_t *out) {
  int i, j;
  out[0] = (int32_t)cover->generator;
  out[1] = cover->composition;
  out[2] = cover->mood;
  out[3] = cover->layer_count;
  out[4] = cover->palette_count;
  out[5] = (int32_t)sleeve_bench_pack(cover->base);
  for (i = 0; i < 5; i++) out[6 + i] = (int32_t)sleeve_bench_pack(cover->palette[i]);
  for (i = 0; i < SLEEVE_MAX_LAYERS; i++) {
    const sleeve_layer *l = &cover->layers[i];
    int32_t *w = out + SLEEVE_BENCH_LAYERS + SLEEVE_BENCH_LAYER_WORDS * i;
    w[0] = l->kind;
    w[1] = l->repeating;
    w[2] = l->stop_count;
    w[3] = l->angle;
    w[4] = l->cx;
    w[5] = l->cy;
    w[6] = l->ex;
    w[7] = l->ey;
    for (j = 0; j < SLEEVE_MAX_STOPS; j++) {
      w[8 + 2 * j] = l->stops[j].at;
      w[9 + 2 * j] = (int32_t)sleeve_bench_pack(l->stops[j].colour);
    }
  }
}

uint32_t sleeve_bench_pack(sleeve_colour c) {
  return (uint32_t)c.r | (uint32_t)c.g << 8 | (uint32_t)c.b << 16 | (uint32_t)c.a << 24;
}

intptr_t sleeve_bench_describe(uint64_t seed, int32_t *out) {
  sleeve_cover cover;
  sleeve_generate(seed, &cover);
  sleeve_bench_flatten(&cover, out);
  return 0;
}

intptr_t sleeve_bench_cover(uint64_t seed, intptr_t size, uint8_t *rgba) {
  sleeve_cover cover;
  sleeve_generate(seed, &cover);
  return sleeve_render(&cover, (int)size, (int)size, rgba, (ptrdiff_t)size * 4);
}

uint64_t sleeve_bench_hash(const uint8_t *bytes, intptr_t length) {
  return sleeve_hash(bytes, (size_t)length);
}
