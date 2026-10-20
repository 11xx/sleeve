/* Times sleeve_generate alone over N seeds (1,000,000 by default), best of R runs (7 by
   default). Build against either sleeve.h with: cc -std=c99 -O2 -I<dir of sleeve.h> gentime.c */
#define _POSIX_C_SOURCE 199309L
#define SLEEVE_IMPLEMENTATION
#include "sleeve.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
  long n = argc > 1 ? atol(argv[1]) : 1000000, i;
  int r, runs = argc > 2 ? atoi(argv[2]) : 7;
  double best = 1e9;
  uint64_t sink = 0;
  static sleeve_cover c;
  for (r = 0; r < runs; r++) {
    double t = now();
    for (i = 0; i < n; i++) {
      sleeve_generate((uint64_t)i, &c);
      sink += c.layers[0].stops[0].colour.r + c.layer_count;
    }
    t = now() - t;
    if (t < best) best = t;
  }
  printf("%ld seeds: best %.1f ns per cover (sink %llu)\n", n, best / n * 1e9,
         (unsigned long long)sink);
  return 0;
}
