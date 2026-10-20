#ifndef SLEEVE_BENCH_H
#define SLEEVE_BENCH_H

#include <stdint.h>

/* A description as 32-bit words: generator, composition, mood, layer count, palette count, the
   base colour and the five palette colours, then each of the twelve layers as kind, repeating,
   stop count, angle, cx, cy, ex, ey and eight (at, colour) pairs. A colour is r | g << 8 |
   b << 16 | a << 24. */
#define SLEEVE_BENCH_LAYERS 11
#define SLEEVE_BENCH_LAYER_WORDS 24
#define SLEEVE_BENCH_WORDS (SLEEVE_BENCH_LAYERS + 12 * SLEEVE_BENCH_LAYER_WORDS)

uint32_t sleeve_bench_pack(sleeve_colour c);
intptr_t sleeve_bench_describe(uint64_t seed, int32_t *out);
intptr_t sleeve_bench_cover(uint64_t seed, intptr_t size, uint8_t *rgba);
uint64_t sleeve_bench_hash(const uint8_t *bytes, intptr_t length);

#endif
