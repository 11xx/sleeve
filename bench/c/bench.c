/* Times sleeve's C alone, natively: each cover generated and rendered for a run of seeds at each
   size, reported as JSON with the same statistics as sleeve-bench.

   usage: sleeve-bench-c [--seeds N] [--first SEED] [--sizes PX,PX,...] [--warmup N] [--label TEXT]
   A size of 0 times generating descriptions alone. */

#define _POSIX_C_SOURCE 199309L
#define SLEEVE_IMPLEMENTATION
#define SLEEVE_NO_STDIO
#include "../../sleeve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static int compare(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

/* The q quantile of sorted samples, as sleeve-bench takes it. */
static uint64_t quantile(const uint64_t *sorted, int n, double q) {
  double at = q * n;
  int i = (int)at;
  if ((double)i < at) i++;
  i -= 1;
  if (i < 0) i = 0;
  if (i > n - 1) i = n - 1;
  return sorted[i];
}

int main(int argc, char **argv) {
  int seeds = 1000, warmup = 20, sizes[16] = {64, 216, 512}, size_count = 3, i, s;
  uint64_t first = 0;
  const char *label = "default";
  for (i = 1; i < argc; i++) {
    if (i + 1 >= argc) return fprintf(stderr, "missing value for %s\n", argv[i]), 2;
    if (strcmp(argv[i], "--seeds") == 0) seeds = atoi(argv[++i]);
    else if (strcmp(argv[i], "--first") == 0) first = strtoull(argv[++i], 0, 10);
    else if (strcmp(argv[i], "--warmup") == 0) warmup = atoi(argv[++i]);
    else if (strcmp(argv[i], "--label") == 0) label = argv[++i];
    else if (strcmp(argv[i], "--sizes") == 0) {
      char *p = argv[++i];
      size_count = 0;
      while (*p && size_count < 16) {
        sizes[size_count++] = (int)strtol(p, &p, 10);
        if (*p == ',') p++;
      }
    } else return fprintf(stderr, "unknown argument %s\n", argv[i]), 2;
  }
  if (seeds < 1) return fprintf(stderr, "--seeds must be at least 1\n"), 2;
  printf("{\n  \"label\": \"%s\",\n  \"seeds\": %d,\n  \"first_seed\": %llu,\n  \"warmup\": %d,\n"
         "  \"runs\": [\n", label, seeds, (unsigned long long)first, warmup);
  for (s = 0; s < size_count; s++) {
    int size = sizes[s];
    size_t bytes = (size_t)size * size * 4;
    uint8_t *pixels = malloc(bytes > 0 ? bytes : 1);
    uint64_t *ns = malloc(sizeof *ns * (size_t)seeds), total = 0, digest = 0;
    sleeve_cover cover;
    if (!pixels || !ns) return fprintf(stderr, "out of memory\n"), 1;
    fprintf(stderr, "timing %d covers at %d px\n", seeds, size);
    for (i = 0; i < warmup; i++) {
      sleeve_generate(first + (uint64_t)i, &cover);
      if (size > 0) sleeve_render(&cover, size, size, pixels, (ptrdiff_t)size * 4);
    }
    for (i = 0; i < seeds; i++) {
      uint64_t t0 = now_ns(), t1;
      sleeve_generate(first + (uint64_t)i, &cover);
      if (size > 0) sleeve_render(&cover, size, size, pixels, (ptrdiff_t)size * 4);
      t1 = now_ns();
      ns[i] = t1 - t0;
      total += ns[i];
      /* the digest sleeve-bench reports: each cover's hash, mixed in turn */
      digest ^= sleeve_hash(pixels, bytes);
      digest ^= digest >> 33;
      digest *= 0xff51afd7ed558ccdull;
      digest ^= digest >> 33;
      digest *= 0xc4ceb9fe1a85ec53ull;
      digest ^= digest >> 33;
    }
    qsort(ns, (size_t)seeds, sizeof *ns, compare);
    printf("    {\"size\": %d, \"language\": \"c\", \"total_ms\": %.3f, \"mean_us\": %.3f, "
           "\"median_us\": %.3f, \"p95_us\": %.3f, \"p99_us\": %.3f, \"max_us\": %.3f, "
           "\"c_digest\": \"%016llx\"}%s\n",
           size, total / 1e6, total / 1e3 / seeds, quantile(ns, seeds, 0.5) / 1e3,
           quantile(ns, seeds, 0.95) / 1e3, quantile(ns, seeds, 0.99) / 1e3, ns[seeds - 1] / 1e3,
           (unsigned long long)digest, s + 1 < size_count ? "," : "");
    free(pixels);
    free(ns);
  }
  printf("  ]\n}\n");
  return 0;
}
