/* sleeve's checks. Each prints one line per check and the program exits non-zero when any fails.

   usage: check [--golden DIR] [--scratch DIR] [--require-svg] [--write-goldens] [CHECK...]

   CHECK is any of descriptions, rasters, antialiasing, png, svg, variety, api, extremes,
   default-style,
   style-shares, style-hooks, builder, containment, style-check, style-hash, given-palette and
   style-determinism; all by default.
   --golden names the directory of golden files (tests/golden by default); --write-goldens
   writes them instead of checking them. --scratch is where the SVG check writes its files
   (the system's temporary directory by default). The SVG check renders with rsvg-convert, or
   the command SLEEVE_SVG_RENDERER names; without one it is skipped unless --require-svg. */

#define _POSIX_C_SOURCE 200809L
#define SLEEVE_IMPLEMENTATION
#include "sleeve.h"
#include "../tools/styles.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *golden_dir = "tests/golden";
static const char *scratch_dir = 0;
static int write_goldens = 0, require_svg = 0, failures = 0;

static void report(const char *check, int ok, const char *detail) {
  printf("%s %s: %s\n", ok ? "pass" : "FAIL", check, detail);
  if (!ok) failures++;
}

static uint64_t fnv(uint64_t h, const void *bytes, size_t n) {
  const uint8_t *p = bytes;
  size_t i;
  for (i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
  return h;
}

static char *read_file(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  char *data;
  long n;
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  data = malloc((size_t)n + 1);
  if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
    free(data);
    data = 0;
  }
  fclose(f);
  if (data) data[n] = 0;
  if (length) *length = (size_t)n;
  return data;
}

static int write_file(const char *path, const void *data, size_t n) {
  FILE *f = fopen(path, "wb");
  int ok = f && fwrite(data, 1, n, f) == n;
  if (f && fclose(f) != 0) ok = 0;
  return ok;
}

static uint8_t *render(const sleeve_cover *c, int w, int h) {
  uint8_t *p = malloc((size_t)w * h * 4);
  if (sleeve_render(c, w, h, p, (ptrdiff_t)w * 4) != 0) {
    free(p);
    return 0;
  }
  return p;
}

/* ---- Determinism: descriptions -------------------------------------------------------- */

/* The seeds whose descriptions are kept in full: the first few, the ends of the range, and
   keys hashed from text. */
static const char *const golden_seeds[] = {
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
  "4294967295", "18446744073709551615", "key:", "key:Album Title\tArtist", "key:サウンド",
};
#define GOLDEN_SEEDS (sizeof golden_seeds / sizeof golden_seeds[0])
#define DIGEST_SEEDS 20000

static uint64_t seed_of(const char *text) {
  if (strncmp(text, "key:", 4) == 0) return sleeve_hash(text + 4, strlen(text + 4));
  return strtoull(text, 0, 10);
}

static size_t descriptions_text(char *out, size_t capacity) {
  size_t n = 0, i;
  uint64_t digest = 0xcbf29ce484222325ull;
  char line[8192];
  for (i = 0; i < GOLDEN_SEEDS; i++) {
    sleeve_cover c;
    sleeve_generate(seed_of(golden_seeds[i]), &c);
    n += (size_t)snprintf(out + n, capacity - n, "seed %s\n", golden_seeds[i]);
    n += sleeve_describe(&c, out + n, capacity - n);
  }
  for (i = 0; i < DIGEST_SEEDS; i++) {
    sleeve_cover c;
    size_t k;
    sleeve_generate(i, &c);
    k = sleeve_describe(&c, line, sizeof line);
    digest = fnv(digest, line, k);
  }
  n += (size_t)snprintf(out + n, capacity - n, "digest of seeds 0 to %d: %016llx\n",
                        DIGEST_SEEDS - 1, (unsigned long long)digest);
  return n;
}

static void check_descriptions(void) {
  static char text[1 << 20];
  char path[1024], detail[256];
  size_t n = descriptions_text(text, sizeof text), m = 0, i, line = 1;
  char *golden;
  snprintf(path, sizeof path, "%s/descriptions.txt", golden_dir);
  if (write_goldens) {
    report("descriptions", write_file(path, text, n), "written");
    return;
  }
  golden = read_file(path, &m);
  if (!golden) {
    report("descriptions", 0, "no golden file");
    return;
  }
  for (i = 0; i < n && i < m && text[i] == golden[i]; i++)
    if (text[i] == '\n') line++;
  if (i == n && n == m) snprintf(detail, sizeof detail, "%zu seeds in full and %d by digest",
                                 GOLDEN_SEEDS, DIGEST_SEEDS);
  else snprintf(detail, sizeof detail, "differs from the golden file at line %zu", line);
  report("descriptions", i == n && n == m, detail);
  free(golden);
}

/* ---- Determinism: rasters ------------------------------------------------------------- */

#define RASTER_SIZE 64

/* The first seed of each composition, so the golden rasters cover them all. */
static int raster_seeds(uint64_t seeds[SLEEVE_COMPOSITIONS]) {
  int found = 0, i;
  uint64_t s;
  for (i = 0; i < SLEEVE_COMPOSITIONS; i++) seeds[i] = UINT64_MAX;
  for (s = 0; found < SLEEVE_COMPOSITIONS && s < 100000; s++) {
    sleeve_cover c;
    sleeve_generate(s, &c);
    if (seeds[c.composition] == UINT64_MAX) {
      seeds[c.composition] = s;
      found++;
    }
  }
  return found;
}

static void check_rasters(void) {
  uint64_t seeds[SLEEVE_COMPOSITIONS];
  int i, worst = 0, ok = 1, missing = 0;
  char detail[256];
  raster_seeds(seeds);
  for (i = 0; i < SLEEVE_COMPOSITIONS; i++) {
    sleeve_cover c;
    char path[1024];
    uint8_t *pixels;
    sleeve_generate(seeds[i], &c);
    pixels = render(&c, RASTER_SIZE, RASTER_SIZE);
    snprintf(path, sizeof path, "%s/raster-%llu.png", golden_dir, (unsigned long long)seeds[i]);
    if (write_goldens) {
      size_t bound = sleeve_png_bound(RASTER_SIZE, RASTER_SIZE), n;
      uint8_t *png = malloc(bound);
      n = sleeve_png(pixels, RASTER_SIZE, RASTER_SIZE, RASTER_SIZE * 4, png, bound);
      if (!n || !write_file(path, png, n)) ok = 0;
      free(png);
    } else {
      int w, h, comp, k;
      uint8_t *golden = stbi_load(path, &w, &h, &comp, 4);
      if (!golden || w != RASTER_SIZE || h != RASTER_SIZE) {
        missing++;
        ok = 0;
      } else {
        for (k = 0; k < RASTER_SIZE * RASTER_SIZE * 4; k++) {
          int d = abs((int)golden[k] - (int)pixels[k]);
          if (d > worst) worst = d;
        }
      }
      stbi_image_free(golden);
    }
    free(pixels);
  }
  if (write_goldens) {
    report("rasters", ok, "written");
    return;
  }
  if (missing) snprintf(detail, sizeof detail, "%d golden rasters missing", missing);
  else snprintf(detail, sizeof detail,
                "%d covers at %d px, one per composition: largest difference %d (at most 1)",
                SLEEVE_COMPOSITIONS, RASTER_SIZE, worst);
  report("rasters", ok && worst <= 1, detail);
}

/* ---- Anti-aliasing against a supersampled reference ------------------------------------ */

/* A layer's premultiplied colour at a point, in double precision, from the description's
   definition alone. */
static void reference_layer(const sleeve_layer *l, double x, double y, double out[4]) {
  const double unit = SLEEVE_UNIT;
  double t, period = l->stops[l->stop_count - 1].at;
  int i, j, ch;
  const sleeve_colour *c0, *c1;
  double f = 0;
  if (l->kind == SLEEVE_CHECKER) {
    long nx = (long)floor((x * unit - l->cx) / l->ex), ny = (long)floor((y * unit - l->cy) / l->ex);
    const sleeve_colour *c = &l->stops[(nx + ny) & 1].colour;
    out[0] = c->r * c->a / 255.0;
    out[1] = c->g * c->a / 255.0;
    out[2] = c->b * c->a / 255.0;
    out[3] = c->a;
    return;
  }
  if (l->kind == SLEEVE_LINEAR) {
    double th = l->angle * 3.14159265358979323846 / 180.0;
    double dx = sin(th), dy = -cos(th), len = fabs(dx) + fabs(dy);
    t = ((x - 0.5) * dx + (y - 0.5) * dy + 0.5 * len) * unit;
  } else {
    double u = x * unit - l->cx, v = (y * unit - l->cy) * l->ex / l->ey;
    t = sqrt(u * u + v * v);
  }
  if (l->repeating) t -= period * floor(t / period);
  i = 0;
  j = 0;
  if (t <= l->stops[0].at) i = j = 0;
  else if (t >= l->stops[l->stop_count - 1].at) i = j = l->stop_count - 1;
  else {
    for (i = 0; i + 1 < l->stop_count; i++)
      if (t >= l->stops[i].at && t < l->stops[i + 1].at) break;
    j = i + 1;
    f = (t - l->stops[i].at) / (double)(l->stops[j].at - l->stops[i].at);
  }
  c0 = &l->stops[i].colour;
  c1 = &l->stops[j].colour;
  {
    double a0 = c0->a / 255.0, a1 = c1->a / 255.0;
    double p0[4] = {c0->r * a0, c0->g * a0, c0->b * a0, c0->a};
    double p1[4] = {c1->r * a1, c1->g * a1, c1->b * a1, c1->a};
    for (ch = 0; ch < 4; ch++) out[ch] = p0[ch] + (p1[ch] - p0[ch]) * f;
  }
}

/* Each pixel as the mean of n x n samples of the composited cover. */
static void reference_render(const sleeve_cover *c, int w, int h, int n, uint8_t *out) {
  int x, y, sx, sy, i, ch;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++) {
      double sum[3] = {0, 0, 0};
      for (sy = 0; sy < n; sy++)
        for (sx = 0; sx < n; sx++) {
          double px = (x + (sx + 0.5) / n) / w, py = (y + (sy + 0.5) / n) / h;
          double acc[4] = {c->base.r, c->base.g, c->base.b, 255}, src[4];
          for (i = 0; i < c->layer_count; i++) {
            double keep;
            reference_layer(&c->layers[i], px, py, src);
            keep = 1.0 - src[3] / 255.0;
            for (ch = 0; ch < 4; ch++) acc[ch] = src[ch] + acc[ch] * keep;
          }
          for (ch = 0; ch < 3; ch++) sum[ch] += acc[ch];
        }
      for (ch = 0; ch < 3; ch++) {
        double v = sum[ch] / ((double)n * n) + 0.5;
        out[(y * w + x) * 4 + ch] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
      }
      out[(y * w + x) * 4 + 3] = 255;
    }
}

static sleeve_colour rgba(uint32_t v) {
  sleeve_colour c;
  c.r = (uint8_t)(v >> 24);
  c.g = (uint8_t)(v >> 16);
  c.b = (uint8_t)(v >> 8);
  c.a = (uint8_t)v;
  return c;
}

static sleeve_layer *scene_layer(sleeve_cover *c, int kind) {
  sleeve_layer *l = &c->layers[c->layer_count++];
  memset(l, 0, sizeof *l);
  l->kind = (uint8_t)kind;
  l->ex = l->ey = SLEEVE_UNIT;
  return l;
}

static void scene_stop(sleeve_layer *l, int32_t at, uint32_t colour) {
  l->stops[l->stop_count].at = at;
  l->stops[l->stop_count].colour = rgba(colour);
  l->stop_count++;
}

/* Hand-made covers that stress edges: every kind, at angles, thin, repeating finer than a
   pixel, and translucent. */
static int scene(int which, sleeve_cover *c) {
  sleeve_layer *l;
  memset(c, 0, sizeof *c);
  c->base = rgba(0x101418ff);
  switch (which) {
  case 0: /* a hard split at 30 degrees */
    l = scene_layer(c, SLEEVE_LINEAR);
    l->angle = 30;
    scene_stop(l, 0, 0xf0f0f0ff);
    scene_stop(l, 6100, 0xf0f0f0ff);
    scene_stop(l, 6100, 0x202020ff);
    return 1;
  case 1: /* vertical bands a third of a pixel apart at 48 px */
    l = scene_layer(c, SLEEVE_LINEAR);
    l->angle = 90;
    scene_stop(l, 3333, 0x00000000);
    scene_stop(l, 3333, 0xff8040ff);
    scene_stop(l, 3400, 0xff8040ff);
    scene_stop(l, 3400, 0x40a0ffff);
    scene_stop(l, 3700, 0x40a0ffff);
    scene_stop(l, 3700, 0x40a0ff00);
    return 1;
  case 2: /* a disc and an ellipse */
    l = scene_layer(c, SLEEVE_RADIAL);
    l->cx = 3700;
    l->cy = 4100;
    scene_stop(l, 0, 0xffe0a0ff);
    scene_stop(l, 2600, 0xffe0a0ff);
    scene_stop(l, 2600, 0xffe0a000);
    l = scene_layer(c, SLEEVE_RADIAL);
    l->cx = 6400;
    l->cy = 6900;
    l->ey = 4300;
    scene_stop(l, 0, 0x2080c0ff);
    scene_stop(l, 3100, 0x2080c0ff);
    scene_stop(l, 3100, 0x2080c000);
    return 1;
  case 3: /* stripes at 63 degrees, 2.4 px apart at 48 px */
    l = scene_layer(c, SLEEVE_LINEAR);
    l->angle = 63;
    l->repeating = 1;
    scene_stop(l, 0, 0xe8d8b0ff);
    scene_stop(l, 250, 0xe8d8b0ff);
    scene_stop(l, 250, 0x30406cff);
    scene_stop(l, 500, 0x30406cff);
    return 1;
  case 4: /* grooves finer than a pixel at 48 px, translucent, around an outside centre */
    l = scene_layer(c, SLEEVE_RADIAL);
    l->cx = -1500;
    l->cy = 11000;
    l->repeating = 1;
    scene_stop(l, 0, 0xffffff00);
    scene_stop(l, 90, 0xffffff00);
    scene_stop(l, 90, 0xffffffa0);
    scene_stop(l, 160, 0xffffffa0);
    return 1;
  case 5: /* rings */
    l = scene_layer(c, SLEEVE_RADIAL);
    l->cx = 5000;
    l->cy = 5000;
    scene_stop(l, 0, 0xd04030ff);
    scene_stop(l, 1300, 0xd04030ff);
    scene_stop(l, 1300, 0xf4ead0ff);
    scene_stop(l, 1700, 0xf4ead0ff);
    scene_stop(l, 1700, 0x2a6050ff);
    scene_stop(l, 2400, 0x2a6050ff);
    scene_stop(l, 2400, 0x2a605000);
    return 1;
  case 6: /* a checker off the pixel grid */
    l = scene_layer(c, SLEEVE_CHECKER);
    l->cx = 130;
    l->cy = -270;
    l->ex = 730;
    scene_stop(l, 0, 0xf2ead8ff);
    scene_stop(l, 0, 0x3a5f9aff);
    return 1;
  case 7: /* a smooth wash under a translucent band */
    l = scene_layer(c, SLEEVE_LINEAR);
    l->angle = 160;
    scene_stop(l, 0, 0x2c1b3fff);
    scene_stop(l, 6000, 0x9b3a6bff);
    scene_stop(l, 10500, 0xf7a15cff);
    l = scene_layer(c, SLEEVE_LINEAR);
    l->angle = 5;
    scene_stop(l, 4000, 0xffffff00);
    scene_stop(l, 4000, 0xffffff8c);
    scene_stop(l, 4300, 0xffffff8c);
    scene_stop(l, 4300, 0xffffff00);
    return 1;
  }
  return 0;
}

typedef struct difference {
  int worst;
  long over2, channels;
  double mean;
} difference;

static difference compare(const uint8_t *a, const uint8_t *b, int w, int h) {
  difference d = {0, 0, 0, 0};
  long k;
  double sum = 0;
  for (k = 0; k < (long)w * h * 4; k++) {
    int v;
    if (k % 4 == 3) continue;
    v = abs((int)a[k] - (int)b[k]);
    if (v > d.worst) d.worst = v;
    if (v > 2) d.over2++;
    sum += v;
    d.channels++;
  }
  d.mean = sum / (double)d.channels;
  return d;
}

/* Anti-aliasing is checked twice against the reference, at 48 px with 64 x 64 samples a pixel:
   - each layer alone over the base, which isolates the filter: a layer is drawn as its exact
     box-filtered colour wherever it is locally straight;
   - each whole cover, where layers are filtered one at a time and then composited, as browsers
     draw CSS backgrounds: two edges sharing a pixel are blended as if independent, which is
     the remaining difference.
   A cover drawn without anti-aliasing misses both by far: its edge pixels are off by up to the
   colours' contrast. */
#define AA_SIZE 48
#define AA_SAMPLES 64
#define AA_COVERS 24
#define AA_LAYER_WORST 4
#define AA_LAYER_MEAN 0.5
#define AA_COVER_MEAN 0.6
#define AA_COVER_OVER8_SHARE 0.002

static int aa_cover(int i, sleeve_cover *c) {
  if (i < 8) return scene(i, c);
  sleeve_generate((uint64_t)(i - 8), c);
  return 1;
}

static void check_antialiasing(void) {
  uint8_t *ref = malloc(AA_SIZE * AA_SIZE * 4);
  int i, ok = 1, layers = 0, layer_worst = 0, cover_worst = 0;
  long over8 = 0, channels = 0;
  double layer_mean = 0, cover_mean = 0;
  char detail[1024];
  for (i = 0; i < AA_COVERS; i++) {
    sleeve_cover c, alone;
    uint8_t *mine;
    difference d;
    int k;
    long j;
    aa_cover(i, &c);
    for (k = 0; k < c.layer_count; k++) {
      alone = c;
      alone.layer_count = 1;
      alone.layers[0] = c.layers[k];
      mine = render(&alone, AA_SIZE, AA_SIZE);
      reference_render(&alone, AA_SIZE, AA_SIZE, AA_SAMPLES, ref);
      d = compare(mine, ref, AA_SIZE, AA_SIZE);
      if (getenv("SLEEVE_VERBOSE"))
        printf("  %s %d layer %d: largest %d, mean %.3f\n", i < 8 ? "scene" : "seed",
               i < 8 ? i : i - 8, k, d.worst, d.mean);
      if (d.worst > layer_worst) layer_worst = d.worst;
      if (d.mean > layer_mean) layer_mean = d.mean;
      layers++;
      free(mine);
    }
    mine = render(&c, AA_SIZE, AA_SIZE);
    reference_render(&c, AA_SIZE, AA_SIZE, AA_SAMPLES, ref);
    d = compare(mine, ref, AA_SIZE, AA_SIZE);
    for (j = 0; j < AA_SIZE * AA_SIZE * 4; j++)
      if (j % 4 != 3 && abs((int)mine[j] - (int)ref[j]) > 8) over8++;
    channels += d.channels;
    if (getenv("SLEEVE_VERBOSE"))
      printf("  %s %d whole: largest %d, mean %.3f\n", i < 8 ? "scene" : "seed", i < 8 ? i : i - 8,
             d.worst, d.mean);
    if (d.worst > cover_worst) cover_worst = d.worst;
    if (d.mean > cover_mean) cover_mean = d.mean;
    free(mine);
  }
  if (layer_worst > AA_LAYER_WORST || layer_mean > AA_LAYER_MEAN || cover_mean > AA_COVER_MEAN ||
      (double)over8 / channels > AA_COVER_OVER8_SHARE)
    ok = 0;
  snprintf(detail, sizeof detail,
           "%d layers alone: largest difference %d (at most %d), worst mean %.3f (at most %.2f); "
           "%d whole covers: worst mean %.3f (at most %.1f), %.3f%% of channels off by more "
           "than 8 (at most %.1f%%), largest %d",
           layers, layer_worst, AA_LAYER_WORST, layer_mean, AA_LAYER_MEAN, AA_COVERS, cover_mean,
           AA_COVER_MEAN, 100.0 * over8 / channels, 100.0 * AA_COVER_OVER8_SHARE, cover_worst);
  report("antialiasing", ok, detail);
  free(ref);
}

/* ---- PNG ------------------------------------------------------------------------------ */

static void check_png(void) {
  static const int sizes[][2] = {{1, 1}, {7, 5}, {64, 64}, {217, 217}, {300, 40}};
  enum { images = 40 };
  int i, ok = 1, decoded_same = 0, refused = 0;
  size_t total = 0, raw = 0;
  char detail[256];
  for (i = 0; i < images; i++) {
    int w = sizes[i % 5][0], h = sizes[i % 5][1], dw, dh, comp, k, same;
    sleeve_cover c;
    uint8_t *pixels, *png, *decoded;
    size_t bound = sleeve_png_bound(w, h), n;
    sleeve_generate((uint64_t)i * 7919u, &c);
    pixels = render(&c, w, h);
    png = malloc(bound);
    n = sleeve_png(pixels, w, h, (ptrdiff_t)w * 4, png, bound);
    if (n == 0 || n > bound) {
      ok = 0;
      free(png);
      free(pixels);
      continue;
    }
    if (sleeve_png(pixels, w, h, (ptrdiff_t)w * 4, png, n - 1) == 0) refused++;
    else ok = 0;
    decoded = stbi_load_from_memory(png, (int)n, &dw, &dh, &comp, 3);
    same = decoded && dw == w && dh == h && comp == 3;
    for (k = 0; same && k < w * h; k++)
      if (memcmp(decoded + k * 3, pixels + k * 4, 3) != 0) same = 0;
    if (same) decoded_same++;
    else ok = 0;
    stbi_image_free(decoded);
    total += n;
    raw += (size_t)w * h * 3;
    free(png);
    free(pixels);
  }
  snprintf(detail, sizeof detail,
           "%d of %d images decode with stb_image to the pixels encoded, at %.1f%% of raw size; "
           "%d of %d refuse one byte too little capacity",
           decoded_same, images, raw > 0 ? 100.0 * total / raw : 0.0, refused, images);
  report("png", ok, detail);
}

/* ---- SVG against the raster ----------------------------------------------------------- */

/* The SVG is rendered by another program at 16 times the size and averaged down in 16 x 16
   blocks, which approximates the box-filtered cover the raster draws, and compared with the
   raster. The largest differences are where two layers' edges share a pixel, as in the
   anti-aliasing check's whole covers. */
#define SVG_SIZE 64
#define SVG_SCALE 16
#define SVG_MEAN 0.6
#define SVG_OVER8_SHARE 0.002
#define SVG_WORST 48

#ifdef __wasi__
/* WASI has no processes, so no renderer can run there. */
static int run(const char *command) {
  (void)command;
  return -1;
}
#else
static int run(const char *command) { return system(command); }
#endif

static void check_svg(void) {
  const char *renderer = getenv("SLEEVE_SVG_RENDERER");
  char command[8192], svg_path[2048], png_path[2048], detail[1024];
  int i, ok = 1, count = 0, worst = 0;
  long over8 = 0, channels = 0;
  double mean_worst = 0;
  if (!renderer) renderer = "rsvg-convert";
  if (!scratch_dir) scratch_dir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
  snprintf(command, sizeof command, "command -v %s >/dev/null 2>&1", renderer);
  if (run(command) != 0) {
    report("svg", !require_svg, "skipped: no SVG renderer (rsvg-convert, or SLEEVE_SVG_RENDERER)");
    return;
  }
  for (i = 0; i < 8 + 40; i++) {
    sleeve_cover c;
    size_t length;
    char *svg;
    uint8_t *mine, *theirs, *down;
    int w, h, comp, x, y, sx, sy, ch, k;
    difference d;
    aa_cover(i, &c);
    length = sleeve_svg(&c, SVG_SIZE * SVG_SCALE, SVG_SIZE * SVG_SCALE, 0, 0);
    svg = malloc(length + 1);
    sleeve_svg(&c, SVG_SIZE * SVG_SCALE, SVG_SIZE * SVG_SCALE, svg, length + 1);
    snprintf(svg_path, sizeof svg_path, "%s/sleeve-check-%d.svg", scratch_dir, i);
    snprintf(png_path, sizeof png_path, "%s/sleeve-check-%d.png", scratch_dir, i);
    write_file(svg_path, svg, length);
    free(svg);
    snprintf(command, sizeof command, "%s -o '%s' '%s'", renderer, png_path, svg_path);
    if (run(command) != 0) {
      ok = 0;
      continue;
    }
    theirs = stbi_load(png_path, &w, &h, &comp, 4);
    remove(svg_path);
    remove(png_path);
    if (!theirs || w != SVG_SIZE * SVG_SCALE || h != SVG_SIZE * SVG_SCALE) {
      ok = 0;
      stbi_image_free(theirs);
      continue;
    }
    down = malloc(SVG_SIZE * SVG_SIZE * 4);
    for (y = 0; y < SVG_SIZE; y++)
      for (x = 0; x < SVG_SIZE; x++)
        for (ch = 0; ch < 4; ch++) {
          int sum = 0;
          for (sy = 0; sy < SVG_SCALE; sy++)
            for (sx = 0; sx < SVG_SCALE; sx++)
              sum += theirs[((y * SVG_SCALE + sy) * w + x * SVG_SCALE + sx) * 4 + ch];
          down[(y * SVG_SIZE + x) * 4 + ch] =
              (uint8_t)((sum + SVG_SCALE * SVG_SCALE / 2) / (SVG_SCALE * SVG_SCALE));
        }
    mine = render(&c, SVG_SIZE, SVG_SIZE);
    d = compare(mine, down, SVG_SIZE, SVG_SIZE);
    for (k = 0; k < SVG_SIZE * SVG_SIZE * 4; k++)
      if (k % 4 != 3 && abs((int)mine[k] - (int)down[k]) > 8) over8++;
    if (getenv("SLEEVE_VERBOSE"))
      printf("  %s %d: largest %d, mean %.3f\n", i < 8 ? "scene" : "seed", i < 8 ? i : i - 8,
             d.worst, d.mean);
    if (d.worst > worst) worst = d.worst;
    if (d.mean > mean_worst) mean_worst = d.mean;
    channels += d.channels;
    count++;
    free(mine);
    free(down);
    stbi_image_free(theirs);
  }
  if (count != 8 + 40 || mean_worst > SVG_MEAN || worst > SVG_WORST ||
      (double)over8 / channels > SVG_OVER8_SHARE)
    ok = 0;
  snprintf(detail, sizeof detail,
           "%d covers rendered by %s at %dx and averaged down to %d px: worst mean difference "
           "from the raster %.3f (at most %.1f), %.3f%% of channels off by more than 8 (at most "
           "%.1f%%), largest %d (at most %d)",
           count, renderer, SVG_SCALE, SVG_SIZE, mean_worst, SVG_MEAN,
           channels ? 100.0 * over8 / channels : 0.0, 100.0 * SVG_OVER8_SHARE, worst, SVG_WORST);
  report("svg", ok, detail);
}

/* ---- Variety -------------------------------------------------------------------------- */

#define VARIETY_SEEDS 20000
#define THUMB 12

static void thumbnail(uint64_t seed, uint8_t *out) {
  sleeve_cover c;
  sleeve_generate(seed, &c);
  sleeve_render(&c, THUMB, THUMB, out, THUMB * 4);
}

static int hue_bin(sleeve_colour c) {
  int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
  int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
  double h, d = mx - mn;
  if (d < 24) return -1;
  if (mx == c.r) h = fmod((c.g - c.b) / d, 6.0);
  else if (mx == c.g) h = (c.b - c.r) / d + 2;
  else h = (c.r - c.g) / d + 4;
  h *= 60;
  if (h < 0) h += 360;
  return (int)(h / 30) % 12;
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

static void check_variety(void) {
  static uint64_t digests[VARIETY_SEEDS];
  long composition[SLEEVE_COMPOSITIONS] = {0}, mood[SLEEVE_MOODS] = {0}, hues[12] = {0};
  long layers[SLEEVE_MAX_LAYERS + 1] = {0}, same_neighbours = 0, total_m = 0;
  double share_c[SLEEVE_COMPOSITIONS];
  long window[SLEEVE_COMPOSITIONS] = {0};
  int skewed_windows = 0, first_skewed = 0;
  int i, k, ok = 1, prev_c = -1, prev_m = -1, worst_dev_c = 0, worst_dev_m = 0, hue_bins = 0;
  int duplicates = 0, close_neighbours = 0;
  double closest = 1e9;
  char detail[1024], line[8192];
  uint8_t a[THUMB * THUMB * 4], b[THUMB * THUMB * 4];
  const sleeve_style *style = &sleeve_default_style;
  for (i = 0; i < SLEEVE_MOODS; i++) total_m += style->moods[i].weight;
  /* a composition's share: its weight's share under each mood, by the mood's share */
  for (i = 0; i < SLEEVE_COMPOSITIONS; i++) share_c[i] = 0;
  for (k = 0; k < SLEEVE_MOODS; k++) {
    const uint16_t *row = style->moods[k].composition_weights;
    long sum = 0;
    for (i = 0; i < SLEEVE_COMPOSITIONS; i++) sum += row ? row[i] : style->compositions[i].weight;
    for (i = 0; i < SLEEVE_COMPOSITIONS; i++)
      share_c[i] += (double)style->moods[k].weight / total_m *
                    (row ? row[i] : style->compositions[i].weight) / sum;
  }
  for (i = 0; i < VARIETY_SEEDS; i++) {
    sleeve_cover c;
    size_t n;
    sleeve_generate((uint64_t)i, &c);
    composition[c.composition]++;
    mood[c.mood]++;
    layers[c.layer_count]++;
    if (c.composition == prev_c && c.mood == prev_m) same_neighbours++;
    window[c.composition]++;
    if (i % 64 == 63) {
      /* a run of 64 neighbouring seeds, against the shares by Pearson's chi-squared */
      double chi = 0;
      for (k = 0; k < SLEEVE_COMPOSITIONS; k++) {
        double expected = 64 * share_c[k];
        chi += (window[k] - expected) * (window[k] - expected) / expected;
        window[k] = 0;
      }
      if (chi > 31.26) {
        skewed_windows++;
        if (i == 63) first_skewed = 1;
      }
    }
    prev_c = c.composition;
    prev_m = c.mood;
    for (k = 0; k < c.palette_count; k++) {
      int bin = hue_bin(c.palette[k]);
      if (bin >= 0) hues[bin]++;
    }
    n = sleeve_describe(&c, line, sizeof line);
    /* the composition and mood names aside, so equal art counts as a duplicate */
    digests[i] = fnv(0xcbf29ce484222325ull, strchr(line, '\n'), n - (size_t)(strchr(line, '\n') - line));
  }
  /* each composition and mood within 12% of its share */
  for (i = 0; i < SLEEVE_COMPOSITIONS; i++) {
    double expected = VARIETY_SEEDS * share_c[i];
    int dev = (int)(100.0 * fabs(composition[i] - expected) / expected + 0.5);
    if (dev > worst_dev_c) worst_dev_c = dev;
  }
  for (i = 0; i < SLEEVE_MOODS; i++) {
    double expected = (double)VARIETY_SEEDS * style->moods[i].weight / total_m;
    int dev = (int)(100.0 * fabs(mood[i] - expected) / expected + 0.5);
    if (dev > worst_dev_m) worst_dev_m = dev;
  }
  for (i = 0; i < 12; i++)
    if (hues[i] > VARIETY_SEEDS / 100) hue_bins++;
  qsort(digests, VARIETY_SEEDS, sizeof digests[0], cmp_u64);
  for (i = 1; i < VARIETY_SEEDS; i++)
    if (digests[i] == digests[i - 1]) duplicates++;
  /* neighbours on a contact sheet: the next seed, and the one a row of eight below */
  for (i = 0; i < 1000; i++) {
    int step;
    for (step = 1; step <= 8; step += 7) {
      double diff = 0;
      thumbnail((uint64_t)i, a);
      thumbnail((uint64_t)(i + step), b);
      for (k = 0; k < THUMB * THUMB * 4; k++)
        if (k % 4 != 3) diff += abs((int)a[k] - (int)b[k]);
      diff /= THUMB * THUMB * 3;
      if (diff < closest) closest = diff;
      if (diff < 12) close_neighbours++;
    }
  }
  if (worst_dev_c > 12 || worst_dev_m > 12 || hue_bins < 11 || duplicates > 0 ||
      close_neighbours > 5 || same_neighbours > 3 * VARIETY_SEEDS / 100 || skewed_windows > 3 ||
      first_skewed)
    ok = 0;
  snprintf(detail, sizeof detail,
           "%d seeds: compositions within %d%% of their shares and moods within %d%% (at most "
           "12%%); %d of %d runs of 64 neighbouring seeds skewed beyond chance at p = 0.001 (at "
           "most 3), %s; palettes in %d of 12 hue sectors (at least 11); %d duplicate covers (none); "
           "%ld neighbours sharing composition and mood (at most 3%%); of 2000 neighbouring "
           "pairs, %d differ by under 12 levels a channel (at most 5), the closest by %.1f",
           VARIETY_SEEDS, worst_dev_c, worst_dev_m, skewed_windows, VARIETY_SEEDS / 64, first_skewed ? "among them the first, a sheet's default seeds, which must not be"
                        : "the first, a sheet's default seeds, not among them",
           hue_bins, duplicates, same_neighbours,
           close_neighbours, closest);
  report("variety", ok, detail);
  (void)layers;
}

#define HASH_EMPTY 0xefd01f60ba992926ull
#define HASH_SLEEVE 0xf6f05973edcde22cull

/* ---- API edges ------------------------------------------------------------------------ */

static void check_api(void) {
  sleeve_cover c, bad;
  uint8_t whole[40 * 30 * 4], parts[40 * 30 * 4], small[16];
  int ok = 1, y;
  size_t n;
  char text[64];
  sleeve_generate(42, &c);
  /* rows rendered in pieces equal the whole */
  sleeve_render(&c, 40, 30, whole, 40 * 4);
  memset(parts, 0, sizeof parts);
  for (y = 0; y < 30; y += 7) sleeve_render_rows(&c, 40, 30, y, y + 7, parts, 40 * 4);
  if (memcmp(whole, parts, sizeof whole) != 0) ok = 0;
  /* invalid descriptions and sizes are refused */
  bad = c;
  bad.layers[0].stop_count = 0;
  if (sleeve_render(&bad, 4, 4, small, 16) != -1) ok = 0;
  bad = c;
  bad.base.a = 254;
  if (sleeve_render(&bad, 4, 4, small, 16) != -1) ok = 0;
  if (sleeve_render(&c, 0, 4, small, 16) != -1) ok = 0;
  if (sleeve_svg(&bad, 4, 4, text, sizeof text) != 0) ok = 0;
  /* the SVG's sizing call agrees with the writing call, and a short buffer is terminated */
  n = sleeve_svg(&c, 64, 64, 0, 0);
  if (n == 0 || sleeve_svg(&c, 64, 64, text, sizeof text) != n || strlen(text) != sizeof text - 1)
    ok = 0;
  /* the hash is stable */
  if (sleeve_hash("", 0) != HASH_EMPTY || sleeve_hash("sleeve", 6) != HASH_SLEEVE) ok = 0;
  report("api", ok, "row ranges equal the whole; refusals; sizing calls; the hash's values");
}

/* ---- Extremes -------------------------------------------------------------------------- */

/* A hand-written cover of one layer of a kind, red at 0 to blue at a side. */
static sleeve_cover one_layer(int kind) {
  sleeve_cover c;
  sleeve_layer *l = &c.layers[0];
  memset(&c, 0, sizeof c);
  c.base.a = 255;
  c.layer_count = 1;
  l->kind = (uint8_t)kind;
  l->stop_count = 2;
  l->cx = l->cy = 0;
  l->ex = l->ey = SLEEVE_UNIT;
  l->stops[0].colour.r = l->stops[0].colour.a = 255;
  l->stops[1].colour.b = l->stops[1].colour.a = 255;
  l->stops[1].at = SLEEVE_UNIT;
  return c;
}

/* What a check that has not finished by its deadline was doing. */
static const char *deadline_what = "";

static void deadline_passed(int sig) {
  static const char head[] = "FAIL extremes: did not finish within 20 s: ";
  ssize_t written = write(1, head, sizeof head - 1);
  written += write(1, deadline_what, strlen(deadline_what));
  written += write(1, "\n", 1);
  (void)sig;
  (void)written;
  _exit(1);
}

/* Descriptions at the edges of what sleeve_check accepts draw and write what they mean: any
   angle as itself modulo 360, a gradient line 100 sides long at its exact SVG coordinates,
   and a repeating ellipse far from its centre in bounded time; sizes beyond the writers' are
   refused. */
static void check_extremes(void) {
  static const int32_t angles[] = {INT32_MIN, INT32_MIN + 1, -721, -90, 450, INT32_MAX};
  static const int sizes[][2] = {{INT_MAX, 1}, {INT_MIN, 1}, {1, INT_MAX}, {INT_MAX, INT_MAX}};
  static uint8_t column[16384 * 4];
  static char svg[2][4096];
  uint8_t pixels[2][6 * 6 * 4];
  const char *failed = 0;
  char detail[512];
  size_t i, n;
  int k;
  /* any angle draws as the angle in [0, 360) it equals */
  for (i = 0; i < sizeof angles / sizeof angles[0] && !failed; i++)
    for (k = SLEEVE_LINEAR; k <= SLEEVE_RADIAL && !failed; k++) {
      sleeve_cover c = one_layer(k), same = c;
      c.layers[0].angle = angles[i];
      same.layers[0].angle = (int32_t)(((int64_t)angles[i] % 360 + 360) % 360);
      if (sleeve_check(&c) || sleeve_render(&c, 6, 6, pixels[0], 6 * 4) ||
          sleeve_render(&same, 6, 6, pixels[1], 6 * 4) ||
          memcmp(pixels[0], pixels[1], sizeof pixels[0]) != 0)
        failed = "an angle beyond [0, 360) draws apart from its equal within it";
      else if (sleeve_svg(&c, 6, 6, svg[0], sizeof svg[0]) >= sizeof svg[0] ||
               sleeve_svg(&same, 6, 6, svg[1], sizeof svg[1]) >= sizeof svg[1] ||
               strcmp(svg[0], svg[1]) != 0)
        failed = "an angle beyond [0, 360) writes apart from its equal within it";
      if (failed)
        snprintf(detail, sizeof detail, "angle %ld: %s", (long)angles[i], failed);
    }
  /* a line to a stop 100 sides along, rightward and leftward */
  for (k = 0; k < 2 && !failed; k++) {
    static const char *const ends[2] = {" x1=\"0\" y1=\"5000\" x2=\"1000000\" y2=\"5000\"",
                                        " x1=\"10000\" y1=\"5000\" x2=\"-990000\" y2=\"5000\""};
    sleeve_cover c = one_layer(SLEEVE_LINEAR);
    c.layers[0].angle = k ? 270 : 90;
    c.layers[0].stops[1].at = 100 * SLEEVE_UNIT;
    n = sleeve_svg(&c, 6, 6, 0, 0);
    if (sleeve_check(&c) || n == 0 || sleeve_svg(&c, 6, 6, svg[0], sizeof svg[0]) != n ||
        !strstr(svg[0], ends[k])) {
      failed = "a gradient line 100 sides long is written at the wrong coordinates";
      snprintf(detail, sizeof detail, "angle %d: %s, not%s", c.layers[0].angle, failed, ends[k]);
    }
  }
  /* sizes beyond 16384 are refused before any arithmetic on them */
  for (i = 0; i < sizeof sizes / sizeof sizes[0] && !failed; i++)
    if (sleeve_png_bound(sizes[i][0], sizes[i][1]) != 0 ||
        sleeve_png(column, sizes[i][0], sizes[i][1], 4, column, sizeof column) != 0) {
      failed = "a PNG size beyond 16384 is not refused";
      snprintf(detail, sizeof detail, "%d x %d: %s", sizes[i][0], sizes[i][1], failed);
    }
  /* a repeating ellipse 100 sides from its centre, 10^6 times wider than tall, over 1 x 16384 */
  if (!failed) {
    sleeve_cover c = one_layer(SLEEVE_RADIAL);
    sleeve_layer *l = &c.layers[0];
    l->repeating = 1;
    l->cy = -100 * SLEEVE_UNIT;
    l->ex = 100 * SLEEVE_UNIT;
    l->ey = 1;
    l->stops[1].at = 5 * SLEEVE_UNIT;
    deadline_what = "a repeating ellipse 100 sides from its centre over 1 x 16384 pixels";
    fflush(stdout);
    signal(SIGALRM, deadline_passed);
    alarm(20);
    if (sleeve_check(&c) || sleeve_render(&c, 1, 16384, column, 4)) {
      failed = "a repeating ellipse far from its centre was refused";
      snprintf(detail, sizeof detail, "%s", failed);
    }
    alarm(0);
  }
  if (!failed)
    snprintf(detail, sizeof detail, "%d angles from INT32_MIN to INT32_MAX draw and write as "
             "their equals in [0, 360); a line 100 sides long is written at its exact ends; "
             "PNG sizes beyond 16384 are refused; a repeating ellipse 100 sides off draws "
             "1 x 16384 within 20 s", (int)(sizeof angles / sizeof angles[0]));
  report("extremes", !failed, detail);
}

/* ---- Styles ---------------------------------------------------------------------------- */

#define STYLE_SEEDS 20000

static int same_cover(const sleeve_cover *a, const sleeve_cover *b) {
  return memcmp(a, b, sizeof *a) == 0;
}

static sleeve_colour hex(uint32_t v) {
  sleeve_colour c;
  c.r = (uint8_t)(v >> 16);
  c.g = (uint8_t)(v >> 8);
  c.b = (uint8_t)v;
  c.a = 255;
  return c;
}

static int same_colour(sleeve_colour a, sleeve_colour b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static int hue_distance(int32_t a, int32_t b) {
  int d = abs(a - b) % 360;
  return d > 180 ? 360 - d : d;
}

/* The default style as a skin compiler would declare it: built-ins found by name, weights as
   numbers. */
static const char *const default_mood_names[SLEEVE_MOODS] = {
  "dusk", "sea", "earth", "paper-warm", "paper-cool", "neon", "mist", "garden", "duotone", "ember",
};
static const uint16_t default_mood_weights[SLEEVE_MOODS] = {12, 12, 11, 9, 8, 9, 10, 10, 10, 9};
static const char *const default_composition_names[SLEEVE_COMPOSITIONS] = {
  "horizon", "glow", "disc", "pair", "stripes", "bands", "split", "bars", "constellation",
  "grooves", "orbs", "checks",
};
static const uint16_t default_composition_weights[SLEEVE_COMPOSITIONS] = {14, 6, 12, 4, 12, 10,
                                                                           12, 6, 7, 6, 8, 3};
static const uint16_t neon_row[SLEEVE_COMPOSITIONS] = {14, 6, 12, 4, 0, 0, 0, 18, 14, 0, 0, 0};

static void declared_default(sleeve_style *style, sleeve_style_mood *moods,
                             sleeve_style_part *parts) {
  int i;
  for (i = 0; i < SLEEVE_MOODS; i++) {
    moods[i].mood = sleeve_find_mood(default_mood_names[i]);
    moods[i].weight = default_mood_weights[i];
    moods[i].composition_weights = i == SLEEVE_NEON ? neon_row : 0;
  }
  for (i = 0; i < SLEEVE_COMPOSITIONS; i++) {
    const sleeve_piece *piece = sleeve_find_piece(SLEEVE_COMPOSE, default_composition_names[i]);
    memset(&parts[i], 0, sizeof parts[i]);
    parts[i].name = default_composition_names[i];
    parts[i].run = piece ? piece->run : 0;
    parts[i].weight = default_composition_weights[i];
  }
  memset(style, 0, sizeof *style);
  style->name = "declared";
  style->moods = moods;
  style->mood_count = SLEEVE_MOODS;
  style->compositions = parts;
  style->composition_count = SLEEVE_COMPOSITIONS;
}

/* The default style through the styled call, as a copy declared from names, and as a style
   with every part empty, draws what sleeve_generate draws. */
static void check_default_style(void) {
  sleeve_style_mood moods[SLEEVE_MOODS];
  sleeve_style_part parts[SLEEVE_COMPOSITIONS];
  sleeve_style declared, empty;
  int i, first_declared = -1, first_empty = -1, first_styled = -1, prints_agree;
  char detail[512];
  declared_default(&declared, moods, parts);
  memset(&empty, 0, sizeof empty);
  for (i = 0; i < STYLE_SEEDS; i++) {
    sleeve_cover plain, styled, copy, none;
    sleeve_generate((uint64_t)i, &plain);
    sleeve_generate_styled((uint64_t)i, &sleeve_default_style, &styled);
    sleeve_generate_styled((uint64_t)i, &declared, &copy);
    sleeve_generate_styled((uint64_t)i, &empty, &none);
    if (first_styled < 0 && !same_cover(&plain, &styled)) first_styled = i;
    if (first_declared < 0 && !same_cover(&plain, &copy)) first_declared = i;
    if (first_empty < 0 && !same_cover(&plain, &none)) first_empty = i;
  }
  prints_agree = sleeve_style_hash(&declared) == sleeve_style_hash(&sleeve_default_style) &&
                 sleeve_style_hash(&empty) == sleeve_style_hash(&sleeve_default_style);
  if (first_styled >= 0)
    snprintf(detail, sizeof detail, "seed %d differs between sleeve_generate and the default "
             "style through sleeve_generate_styled", first_styled);
  else if (first_declared >= 0)
    snprintf(detail, sizeof detail, "seed %d differs between sleeve_generate and the default "
             "style declared from names", first_declared);
  else if (first_empty >= 0)
    snprintf(detail, sizeof detail, "seed %d differs between sleeve_generate and a style with "
             "every part empty", first_empty);
  else if (!prints_agree)
    snprintf(detail, sizeof detail, "the declared copy or the empty style fingerprints apart "
             "from the default style");
  else
    snprintf(detail, sizeof detail, "%d seeds: the default style through the styled call, "
             "declared from names, and empty, each draw sleeve_generate's bytes and share its "
             "fingerprint", STYLE_SEEDS);
  report("default-style", first_styled < 0 && first_declared < 0 && first_empty < 0 && prints_agree,
         detail);
}

/* A style's moods and compositions are drawn in the shares it declares, a mood's own weights
   apply under it, and names come through the style. */
static void check_style_shares(void) {
  static const uint16_t ember_weights[4] = {1, 2, 0, 0};
  sleeve_style_mood moods[3] = {{0, 1, 0}, {0, 3, 0}, {0, 6, ember_weights}};
  sleeve_style_part parts[4] = {
    {"horizon", sleeve_compose_horizon, 0, 1, 0, 0}, {"disc", sleeve_compose_disc, 0, 2, 0, 0},
    {"bars", sleeve_compose_bars, 0, 3, 0, 0}, {"orbs", sleeve_compose_orbs, 0, 4, 0, 0},
  };
  sleeve_style style = {"shares", moods, 3, parts, 4, 0, 0, 0, 0};
  long by_mood[3] = {0}, open[4] = {0}, ember[4] = {0}, open_total = 0, ember_total = 0;
  double worst_mood = 0, worst_open = 0, ember_horizon;
  int i, k, misnamed = 0;
  char detail[512];
  moods[0].mood = sleeve_find_mood("dusk");
  moods[1].mood = sleeve_find_mood("sea");
  moods[2].mood = sleeve_find_mood("ember");
  for (i = 0; i < STYLE_SEEDS; i++) {
    sleeve_cover c;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    if (c.mood > 2 || c.composition > 3) {
      misnamed++;
      continue;
    }
    by_mood[c.mood]++;
    if (c.mood == 2) ember[c.composition]++, ember_total++;
    else open[c.composition]++, open_total++;
    if (strcmp(sleeve_style_composition_name(&style, c.composition), parts[c.composition].name) ||
        strcmp(sleeve_style_mood_name(&style, c.mood), moods[c.mood].mood->name))
      misnamed++;
  }
  for (k = 0; k < 3; k++) {
    double expected = STYLE_SEEDS * moods[k].weight / 10.0;
    double dev = fabs(by_mood[k] - expected) / expected;
    if (dev > worst_mood) worst_mood = dev;
  }
  for (k = 0; k < 4; k++) {
    double expected = open_total * parts[k].weight / 10.0;
    double dev = fabs(open[k] - expected) / expected;
    if (dev > worst_open) worst_open = dev;
  }
  ember_horizon = ember_total ? (double)ember[0] / ember_total : 0;
  snprintf(detail, sizeof detail,
           "%d seeds in a style of 3 moods (1:3:6) and 4 compositions (1:2:3:4): moods within "
           "%.1f%% of their shares and compositions within %.1f%% (at most 6%% and 10%%); under "
           "the mood with its own weights (1:2:0:0), %ld covers of a composition weighted 0 "
           "(none) and horizon's share %.3f (1/3, within 0.03); %d covers named or indexed "
           "outside the style (none)",
           STYLE_SEEDS, 100 * worst_mood, 100 * worst_open, ember[2] + ember[3], ember_horizon,
           misnamed);
  report("style-shares", worst_mood <= 0.06 && worst_open <= 0.10 && ember[2] + ember[3] == 0 &&
         fabs(ember_horizon - 1.0 / 3) <= 0.03 && misnamed == 0, detail);
}

/* Palette hooks transform the palette before composing, in order, and not a given palette;
   finishers paint over the composition. */
static void check_style_hooks(void) {
  static const sleeve_params to_x = {{0x3366cc, 1000}}, to_y = {{0xcc6633, 1000}};
  static const sleeve_params blue = {{200}};
  static const sleeve_params inks = {{0x202a44, 0xf0e4c8}};
  static const sleeve_params middle = {{400, 600}};
  static const sleeve_params grey = {{0}};
  static const sleeve_params wash = {{SLEEVE_ACCENT, 40}};
  const sleeve_style_part tint_x = {"tint", sleeve_palette_tint, &to_x, 0, 0, 0};
  const sleeve_style_part tint_y = {"tint", sleeve_palette_tint, &to_y, 0, 0, 0};
  sleeve_style_part one[1], two[2], finish[2];
  sleeve_style style;
  const char *failed = 0;
  char detail[512];
  int i, k, seeds = 200;
  one[0] = tint_x;
  two[0] = tint_x;
  two[1] = tint_y;
  for (i = 0; i < seeds; i++) {
    sleeve_cover c, given;
    sleeve_colour album[3], roles[5];
    int32_t l, ch, h, lo = 1000, hi = 0, dark = 0, light = 0;
    memset(&style, 0, sizeof style);
    style.palette_hooks = one;
    style.palette_hook_count = 1;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    for (k = 0; k < 5; k++)
      if (!failed && !same_colour(c.palette[k], hex(0x3366cc)))
        failed = "tint did not turn the palette";
    if (!failed && !same_colour(c.base, hex(0x3366cc)))
      failed = "the composition drew from the untinted palette";
    album[0] = hex(0x101820), album[1] = hex(0xf2e8d0), album[2] = hex(0xc0392b);
    sleeve_generate_with_palette((uint64_t)i, &style, album, 3, &given);
    sleeve_assign_palette(album, 3, roles);
    if (!failed && memcmp(given.palette, roles, sizeof roles) != 0)
      failed = "a palette hook ran on a given palette";
    style.palette_hooks = two;
    style.palette_hook_count = 2;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    if (!failed && !same_colour(c.palette[0], hex(0xcc6633)))
      failed = "two palette hooks did not run in order";
    two[0].run = sleeve_palette_monochrome, two[0].params = &blue;
    two[1].run = sleeve_palette_chroma, two[1].params = 0;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    for (k = 0; k < 5; k++) {
      sleeve_to_oklch(c.palette[k], &l, &ch, &h);
      if (!failed && ch >= 30 && hue_distance(h, 200) > 8)
        failed = "monochrome left a colour off its hue";
    }
    two[0] = tint_x, two[1] = tint_y;
    one[0].run = sleeve_palette_duotone, one[0].params = &inks;
    style.palette_hooks = one;
    style.palette_hook_count = 1;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    for (k = 0; k < 5; k++) {
      sleeve_to_oklch(c.palette[k], &l, &ch, &h);
      if (l < lo) lo = l, dark = k;
      if (l > hi) hi = l, light = k;
    }
    if (!failed && (!same_colour(c.palette[dark], hex(0x202a44)) ||
                    !same_colour(c.palette[light], hex(0xf0e4c8))))
      failed = "duotone did not print the darkest and lightest colours in its inks";
    one[0].run = sleeve_palette_lightness, one[0].params = &middle;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    for (k = 0; k < 5; k++) {
      sleeve_to_oklch(c.palette[k], &l, &ch, &h);
      if (!failed && (l < 395 || l > 605)) failed = "lightness left a colour outside its range";
    }
    one[0].run = sleeve_palette_chroma, one[0].params = &grey;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    for (k = 0; k < 5; k++) {
      sleeve_to_oklch(c.palette[k], &l, &ch, &h);
      if (!failed && ch > 4) failed = "chroma 0 left a colour coloured";
    }
    one[0] = tint_x;
    /* finishers: a vignette and a frame, then a wash and lines */
    memset(&style, 0, sizeof style);
    memset(finish, 0, sizeof finish);
    finish[0].name = "vignette", finish[0].run = sleeve_finish_vignette, finish[0].layers = 1;
    finish[1].name = "frame", finish[1].run = sleeve_finish_frame, finish[1].layers = 2;
    style.finishers = finish;
    style.finisher_count = 2;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    {
      const sleeve_layer *v = &c.layers[c.layer_count - 3], *f = &c.layers[c.layer_count - 2];
      static uint8_t corner[64 * 64 * 4];
      if (!failed && (c.layer_count < 4 || v->kind != SLEEVE_RADIAL || v->cx != 5000 ||
                      f->kind != SLEEVE_LINEAR || f->angle != 90 || f->stop_count != 6 ||
                      c.layers[c.layer_count - 1].angle != 180))
        failed = "a vignette and a frame are not the top three layers";
      sleeve_render(&c, 64, 64, corner, 64 * 4);
      if (!failed && (corner[0] != c.palette[SLEEVE_PALE].r ||
                      corner[1] != c.palette[SLEEVE_PALE].g ||
                      corner[2] != c.palette[SLEEVE_PALE].b))
        failed = "the frame does not paint the corner pale";
    }
    finish[0].name = "wash", finish[0].run = sleeve_finish_wash, finish[0].params = &wash;
    finish[1].name = "lines", finish[1].run = sleeve_finish_lines, finish[1].layers = 1;
    sleeve_generate_styled((uint64_t)i, &style, &c);
    if (!failed && (c.layers[c.layer_count - 2].stop_count != 1 ||
                    c.layers[c.layer_count - 2].stops[0].colour.a != 40 ||
                    !c.layers[c.layer_count - 1].repeating))
      failed = "a wash and lines are not the top two layers";
    if (failed) break;
  }
  if (failed) snprintf(detail, sizeof detail, "seed %d: %s", i, failed);
  else snprintf(detail, sizeof detail, "%d seeds: tint, two tints in order, monochrome, duotone, "
                "lightness and chroma transform the palette the composition draws from, and not "
                "a given palette; vignette, frame, wash and lines paint on top", seeds);
  report("style-hooks", !failed, detail);
}

/* ---- Hooks the builder and containment checks run ---- */

/* A horizon with a ring hung over it: a built-in composition, then an overlay. */
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

/* What the hook below saw of the builder. */
static struct {
  int left_before, left_after, left_full, range_reversed, below_zero, count_full;
} seen;

/* A disc on a pale ground, then layers until none are left, noting what the builder says. */
static void target(sleeve_builder *b, const void *params) {
  int i;
  (void)params;
  b->cover->base = b->palette[SLEEVE_PALE];
  seen.left_before = sleeve_layers_left(b);
  sleeve_add_disc(b, 5000, 5000, 2000, b->palette[SLEEVE_ACCENT]);
  seen.left_after = sleeve_layers_left(b);
  seen.range_reversed = sleeve_range(b, 5, 3);
  seen.below_zero = sleeve_below(b, 0);
  for (i = 0; i < 20; i++) {
    sleeve_layer *l = sleeve_add_linear(b, 45);
    sleeve_add_stop(l, 0, sleeve_alpha(b->palette[SLEEVE_DEEP], 0));
  }
  seen.left_full = sleeve_layers_left(b);
  seen.count_full = b->cover->layer_count;
}

/* Thirty layers of twelve stops each, the stops in falling order. */
static void greedy(sleeve_builder *b, const void *params) {
  int i, j;
  (void)params;
  seen.left_before = sleeve_layers_left(b);
  for (i = 0; i < 30; i++) {
    sleeve_layer *l = sleeve_add_linear(b, i * 7);
    for (j = 0; j < 12; j++) sleeve_add_stop(l, 3000 - j * 100, b->palette[j % 5]);
  }
}

/* Writes what the builder would refuse straight into the description. */
static void vandal(sleeve_builder *b, const void *params) {
  sleeve_layer *l;
  int i;
  (void)params;
  l = sleeve_add_radial(b, 5000, 5000);
  sleeve_add_band(l, 0, 3000, b->palette[SLEEVE_ACCENT]);
  l->ex = 0;
  l = sleeve_add_layer(b, SLEEVE_LINEAR, 1);
  sleeve_add_band(l, 500, 900, b->palette[SLEEVE_PALE]);
  sleeve_add_linear(b, 30);
  l = sleeve_add_layer(b, 9, 0);
  sleeve_add_stop(l, 0, b->palette[SLEEVE_MID]);
  l = sleeve_add_linear(b, 0);
  sleeve_add_stop(l, 0, b->palette[SLEEVE_MID]);
  l->stops[1].at = -5;
  l->stop_count = 2;
  b->limit = 1000;
  for (i = 0; i < 20; i++) sleeve_add_stop(sleeve_add_linear(b, i), 0, b->palette[i % 5]);
  b->cover->base.a = 0;
  b->cover->palette[2].a = 3;
  b->cover->layer_count = 200;
}

static void vandal_palette(sleeve_builder *b, const void *params) {
  int i;
  (void)params;
  for (i = 0; i < SLEEVE_ROLES; i++) b->palette[i].a = 0;
  sleeve_add_stop(sleeve_add_linear(b, 0), 0, b->palette[0]);
}

/* A custom composition built on the builder, alone and over a built-in one, renders and passes
   sleeve_check; the builder's generator and budget answer as documented. */
static void check_builder(void) {
  sleeve_style_part ringed[1] = {{"ringed-horizon", ringed_horizon, 0, 1, 0, 1}};
  sleeve_style_part plain[1] = {{"horizon", sleeve_compose_horizon, 0, 1, 0, 0}};
  sleeve_style_part own[1] = {{"target", target, 0, 1, 0, 1}};
  sleeve_style with_ring = {"ringed", 0, 0, ringed, 1, 0, 0, 0, 0};
  sleeve_style horizon = {"horizon", 0, 0, plain, 1, 0, 0, 0, 0};
  sleeve_style mine = {"target", 0, 0, own, 1, 0, 0, 0, 0};
  const char *failed = 0;
  char detail[512];
  int i, seeds = 500;
  for (i = 0; i < seeds; i++) {
    sleeve_cover a, b;
    uint8_t px[16 * 16 * 4];
    const sleeve_layer *ring;
    sleeve_generate_styled((uint64_t)i, &with_ring, &a);
    sleeve_generate_styled((uint64_t)i, &horizon, &b);
    ring = &a.layers[a.layer_count - 1];
    if (sleeve_check(&a) || sleeve_render(&a, 16, 16, px, 16 * 4))
      failed = "a horizon with a ring does not pass sleeve_check";
    else if (a.layer_count != b.layer_count + 1 ||
             memcmp(a.layers, b.layers, sizeof a.layers[0] * b.layer_count) != 0)
      failed = "a built-in horizon called from a hook drew other layers than on its own";
    else if (ring->kind != SLEEVE_RADIAL || ring->stop_count != 4 || ring->stops[1].colour.a != 255)
      failed = "the ring over the horizon is not its top layer";
    memset(&seen, 0, sizeof seen);
    sleeve_generate_styled((uint64_t)i, &mine, &a);
    sleeve_render(&a, 16, 16, px, 16 * 4);
    if (failed) break;
    if (sleeve_check(&a)) failed = "a composition on the builder does not pass sleeve_check";
    else if (!same_colour(sleeve_alpha(a.layers[0].stops[0].colour, 255),
                          a.palette[SLEEVE_ACCENT]) ||
             px[(8 * 16 + 8) * 4] != a.palette[SLEEVE_ACCENT].r ||
             px[(8 * 16 + 8) * 4 + 1] != a.palette[SLEEVE_ACCENT].g)
      failed = "the disc is not drawn in the accent at the centre";
    else if (px[0] != a.palette[SLEEVE_PALE].r || px[1] != a.palette[SLEEVE_PALE].g ||
             px[2] != a.palette[SLEEVE_PALE].b)
      failed = "the disc covers the corner, which is the pale ground";
    else if (seen.left_before != SLEEVE_MAX_LAYERS || seen.left_after != SLEEVE_MAX_LAYERS - 1 ||
             seen.left_full != 0 || seen.count_full != SLEEVE_MAX_LAYERS)
      failed = "sleeve_layers_left does not count down to 0 at the limit";
    else if (seen.range_reversed != 5 || seen.below_zero != 0)
      failed = "sleeve_range or sleeve_below misanswers a reversed or empty range";
    if (failed) break;
  }
  if (failed) snprintf(detail, sizeof detail, "seed %d: %s", i, failed);
  else snprintf(detail, sizeof detail, "%d seeds: a horizon with a ring over it and a disc on a "
                "ground, both on the builder, pass sleeve_check and render as drawn; the "
                "builder's budget and generator answer as documented", seeds);
  report("builder", !failed, detail);
}

/* Random parameters within a built-in piece's ranges. */
static void random_params(const sleeve_piece *piece, sleeve_params *params, uint64_t *state) {
  int32_t *v = params->v;
  int i;
  for (i = 0; i < piece->param_count; i++) {
    const sleeve_param *p = &piece->params[i];
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    v[i] = p->lo + (int32_t)((*state >> 33) % (uint64_t)(p->hi - p->lo + 1));
  }
  for (i = 0; i < piece->param_count; i++) {
    const sleeve_param *p = &piece->params[i];
    if (p->at_most >= 0 && v[i] > v[p->at_most]) v[i] = v[p->at_most];
  }
}

/* Hooks that overrun the budgets or write invalid layers leave a cover that passes
   sleeve_check, finishers keep the layers they reserve, and built-in pieces with any
   parameters in range stay within bounds. */
static void check_containment(void) {
  sleeve_style_part greed[1] = {{"greedy", greedy, 0, 1, 0, 1}};
  sleeve_style_part wreck[1] = {{"vandal", vandal, 0, 1, 0, 1}};
  sleeve_style_part wreck_palette[1] = {{"vandal", vandal_palette, 0, 0, 0, 1}};
  sleeve_style_part frame[1] = {{"frame", sleeve_finish_frame, 0, 0, 2, 0}};
  sleeve_style greedy_style = {"greedy", 0, 0, greed, 1, 0, 0, frame, 1};
  sleeve_style vandal_style = {"vandal", 0, 0, wreck, 1, wreck_palette, 1, 0, 0};
  const char *failed = 0;
  char detail[512];
  int i, k, fuzzed = 0;
  uint64_t state = 0x5eed;
  for (i = 0; i < 300 && !failed; i++) {
    sleeve_cover c;
    sleeve_generate_styled((uint64_t)i, &greedy_style, &c);
    if (sleeve_check(&c))
      failed = "thirty layers of twelve stops leave a cover sleeve_check refuses";
    else if (seen.left_before != SLEEVE_MAX_LAYERS - 2)
      failed = "the composition was not told the frame's two layers are kept";
    else if (c.layer_count != SLEEVE_MAX_LAYERS || c.layers[10].angle != 90 ||
             c.layers[11].angle != 180 || c.layers[10].stop_count != 6)
      failed = "the frame finisher's layers are missing from the top of a full cover";
    for (k = 0; k < 10 && !failed; k++)
      if (c.layers[k].stop_count != SLEEVE_MAX_STOPS)
        failed = "a layer kept more stops than allowed";
    sleeve_generate_styled((uint64_t)i, &vandal_style, &c);
    if (failed) break;
    if (sleeve_check(&c)) failed = "invalid layers written by a hook were kept";
    else if (c.layer_count > SLEEVE_MAX_LAYERS || c.base.a != 255)
      failed = "the count or base was left out of bounds";
    for (k = 0; k < 5 && !failed; k++)
      if (c.palette[k].a != 255) failed = "a palette colour was left translucent";
  }
  /* built-in pieces with random parameters in range */
  for (k = 0; k < 3000 && !failed; k++) {
    int n_comp, n_pal, n_fin, j;
    const sleeve_piece *comps = sleeve_pieces(SLEEVE_COMPOSE, &n_comp);
    const sleeve_piece *pals = sleeve_pieces(SLEEVE_PALETTE, &n_pal);
    const sleeve_piece *fins = sleeve_pieces(SLEEVE_FINISH, &n_fin);
    sleeve_params pc, pp, pf[2];
    sleeve_style_part comp, pal, fin[2];
    sleeve_style style;
    sleeve_cover c;
    char reason[256];
    const sleeve_piece *a = &comps[k % n_comp], *p = &pals[(k / 3) % n_pal];
    memset(&style, 0, sizeof style);
    random_params(a, &pc, &state);
    random_params(p, &pp, &state);
    memset(&comp, 0, sizeof comp);
    memset(&pal, 0, sizeof pal);
    memset(fin, 0, sizeof fin);
    comp.name = a->name;
    comp.run = a->run;
    comp.params = &pc;
    comp.weight = 1;
    pal.name = p->name;
    pal.run = p->run;
    pal.params = &pp;
    for (j = 0; j < 2; j++) {
      const sleeve_piece *f = &fins[(k / 7 + j) % n_fin];
      random_params(f, &pf[j], &state);
      fin[j].name = f->name;
      fin[j].run = f->run;
      fin[j].params = &pf[j];
      fin[j].layers = (uint8_t)f->layers;
    }
    style.compositions = &comp, style.composition_count = 1;
    style.palette_hooks = &pal, style.palette_hook_count = k % 2;
    style.finishers = fin, style.finisher_count = k % 3;
    if (sleeve_style_check(&style, reason, sizeof reason)) {
      failed = "parameters drawn within the ranges were refused";
      break;
    }
    for (j = 0; j < 3 && !failed; j++) {
      sleeve_generate_styled((uint64_t)(k * 3 + j), &style, &c);
      if (sleeve_check(&c) || c.layer_count > SLEEVE_MAX_LAYERS)
        failed = "a built-in piece with parameters in range drew a cover sleeve_check refuses";
    }
    fuzzed++;
  }
  if (failed) snprintf(detail, sizeof detail, "%s", failed);
  else snprintf(detail, sizeof detail, "300 seeds each of a hook adding 30 layers of 12 falling "
                "stops under a 2-layer frame, and of hooks writing invalid layers, counts, base "
                "and palette, pass sleeve_check with the frame on top; %d styles of built-in "
                "pieces with random parameters in range pass too", fuzzed);
  report("containment", !failed, detail);
}

/* ---- Parameters ------------------------------------------------------------------------ */

/* Every built-in parameter's named index, as sleeve.h's enums give them. */
static const struct {
  const char *piece, *name;
  int index;
} named_params[] = {
  {"horizon", "night", SLEEVE_HORIZON_NIGHT}, {"horizon", "level", SLEEVE_HORIZON_LEVEL},
  {"horizon", "tilt", SLEEVE_HORIZON_TILT}, {"horizon", "horizon_min", SLEEVE_HORIZON_HORIZON_MIN},
  {"horizon", "horizon_max", SLEEVE_HORIZON_HORIZON_MAX}, {"horizon", "sun", SLEEVE_HORIZON_SUN},
  {"horizon", "setting", SLEEVE_HORIZON_SETTING}, {"horizon", "sun_min", SLEEVE_HORIZON_SUN_MIN},
  {"horizon", "sun_max", SLEEVE_HORIZON_SUN_MAX}, {"horizon", "band", SLEEVE_HORIZON_BAND},
  {"horizon", "lines", SLEEVE_HORIZON_LINES}, {"glow", "dusk", SLEEVE_GLOW_DUSK},
  {"glow", "radius_min", SLEEVE_GLOW_RADIUS_MIN}, {"glow", "radius_max", SLEEVE_GLOW_RADIUS_MAX},
  {"glow", "alpha_min", SLEEVE_GLOW_ALPHA_MIN}, {"glow", "alpha_max", SLEEVE_GLOW_ALPHA_MAX},
  {"glow", "sun", SLEEVE_GLOW_SUN}, {"disc", "record", SLEEVE_DISC_RECORD},
  {"disc", "centred", SLEEVE_DISC_CENTRED}, {"disc", "label_min", SLEEVE_DISC_LABEL_MIN},
  {"disc", "label_max", SLEEVE_DISC_LABEL_MAX}, {"disc", "rings_min", SLEEVE_DISC_RINGS_MIN},
  {"disc", "rings_max", SLEEVE_DISC_RINGS_MAX}, {"disc", "halo", SLEEVE_DISC_HALO},
  {"disc", "wash", SLEEVE_DISC_WASH}, {"pair", "dark", SLEEVE_PAIR_DARK},
  {"pair", "spread_min", SLEEVE_PAIR_SPREAD_MIN}, {"pair", "spread_max", SLEEVE_PAIR_SPREAD_MAX},
  {"pair", "hub_min", SLEEVE_PAIR_HUB_MIN}, {"pair", "hub_max", SLEEVE_PAIR_HUB_MAX},
  {"pair", "strip", SLEEVE_PAIR_STRIP}, {"pair", "spots", SLEEVE_PAIR_SPOTS},
  {"stripes", "solid", SLEEVE_STRIPES_SOLID},
  {"stripes", "colours_min", SLEEVE_STRIPES_COLOURS_MIN},
  {"stripes", "colours_max", SLEEVE_STRIPES_COLOURS_MAX},
  {"stripes", "wobble", SLEEVE_STRIPES_WOBBLE}, {"stripes", "disc", SLEEVE_STRIPES_DISC},
  {"bands", "sheen", SLEEVE_BANDS_SHEEN}, {"bands", "bands_min", SLEEVE_BANDS_BANDS_MIN},
  {"bands", "bands_max", SLEEVE_BANDS_BANDS_MAX}, {"bands", "thin", SLEEVE_BANDS_THIN},
  {"bands", "tilt", SLEEVE_BANDS_TILT}, {"bands", "disc", SLEEVE_BANDS_DISC},
  {"split", "angle_min", SLEEVE_SPLIT_ANGLE_MIN}, {"split", "angle_max", SLEEVE_SPLIT_ANGLE_MAX},
  {"split", "cross", SLEEVE_SPLIT_CROSS}, {"split", "disc", SLEEVE_SPLIT_DISC},
  {"split", "disc_min", SLEEVE_SPLIT_DISC_MIN}, {"split", "disc_max", SLEEVE_SPLIT_DISC_MAX},
  {"bars", "vertical", SLEEVE_BARS_VERTICAL}, {"bars", "bars_min", SLEEVE_BARS_BARS_MIN},
  {"bars", "bars_max", SLEEVE_BARS_BARS_MAX}, {"bars", "tilt", SLEEVE_BARS_TILT},
  {"constellation", "stars_min", SLEEVE_CONSTELLATION_STARS_MIN},
  {"constellation", "stars_max", SLEEVE_CONSTELLATION_STARS_MAX},
  {"constellation", "moon", SLEEVE_CONSTELLATION_MOON},
  {"constellation", "star_min", SLEEVE_CONSTELLATION_STAR_MIN},
  {"constellation", "star_max", SLEEVE_CONSTELLATION_STAR_MAX},
  {"grooves", "lines", SLEEVE_GROOVES_LINES}, {"grooves", "two_tone", SLEEVE_GROOVES_TWO_TONE},
  {"grooves", "disc", SLEEVE_GROOVES_DISC}, {"orbs", "wash", SLEEVE_ORBS_WASH},
  {"orbs", "edge", SLEEVE_ORBS_EDGE}, {"orbs", "oval", SLEEVE_ORBS_OVAL},
  {"checks", "tile_min", SLEEVE_CHECKS_TILE_MIN}, {"checks", "tile_max", SLEEVE_CHECKS_TILE_MAX},
  {"checks", "dark", SLEEVE_CHECKS_DARK}, {"checks", "disc", SLEEVE_CHECKS_DISC},
  {"tint", "colour", SLEEVE_TINT_COLOUR}, {"tint", "amount", SLEEVE_TINT_AMOUNT},
  {"chroma", "scale", SLEEVE_CHROMA_SCALE}, {"monochrome", "hue", SLEEVE_MONOCHROME_HUE},
  {"lightness", "lo", SLEEVE_LIGHTNESS_LO}, {"lightness", "hi", SLEEVE_LIGHTNESS_HI},
  {"duotone", "dark", SLEEVE_DUOTONE_DARK}, {"duotone", "light", SLEEVE_DUOTONE_LIGHT},
  {"vignette", "role", SLEEVE_VIGNETTE_ROLE}, {"vignette", "alpha", SLEEVE_VIGNETTE_ALPHA},
  {"vignette", "inner", SLEEVE_VIGNETTE_INNER}, {"vignette", "outer", SLEEVE_VIGNETTE_OUTER},
  {"wash", "role", SLEEVE_WASH_ROLE}, {"wash", "alpha", SLEEVE_WASH_ALPHA},
  {"frame", "role", SLEEVE_FRAME_ROLE}, {"frame", "alpha", SLEEVE_FRAME_ALPHA},
  {"frame", "width", SLEEVE_FRAME_WIDTH}, {"lines", "role", SLEEVE_LINES_ROLE},
  {"lines", "alpha", SLEEVE_LINES_ALPHA}, {"lines", "angle", SLEEVE_LINES_ANGLE},
  {"lines", "pitch", SLEEVE_LINES_PITCH}, {"lines", "width", SLEEVE_LINES_WIDTH},
};

/* A builder for one piece run by hand: an empty cover, a palette and a generator state. */
static void hand_builder(sleeve_builder *b, sleeve_cover *c, uint64_t state) {
  int i;
  memset(b, 0, sizeof *b);
  memset(c, 0, sizeof *c);
  b->cover = c;
  b->rng.state = state;
  b->limit = SLEEVE_MAX_LAYERS;
  for (i = 0; i < SLEEVE_ROLES; i++) b->palette[i] = hex(0x203040u + 0x302010u * (uint32_t)i);
}

/* A piece run by hand with these parameters, as the cover, palette and generator it leaves. */
static void run_piece(const sleeve_piece *piece, const sleeve_params *params, uint64_t state,
                      sleeve_builder *b, sleeve_cover *c) {
  hand_builder(b, c, state);
  piece->run(b, params);
  b->cover = 0;
}

/* Built-in parameters are an array whose named indices are sleeve_find_param's, and whose
   defaults are the pieces' own; a piece run by hand with values beyond its ranges draws as
   with the nearest in range. */
static void check_parameters(void) {
  const char *failed = 0;
  char detail[512];
  int i, kind, n, k, pieces = 0;
  for (i = 0; i < (int)(sizeof named_params / sizeof named_params[0]) && !failed; i++) {
    const sleeve_piece *piece = 0;
    for (kind = SLEEVE_COMPOSE; kind <= SLEEVE_FINISH && !piece; kind++)
      piece = sleeve_find_piece(kind, named_params[i].piece);
    if (!piece || sleeve_find_param(piece, named_params[i].name) != named_params[i].index)
      failed = "a named index is not its parameter's";
    if (failed)
      snprintf(detail, sizeof detail, "%s %s: %s", named_params[i].piece, named_params[i].name,
               failed);
  }
  for (kind = SLEEVE_COMPOSE; kind <= SLEEVE_FINISH && !failed; kind++) {
    const sleeve_piece *list = sleeve_pieces(kind, &n);
    for (k = 0; k < n && !failed; k++) {
      const sleeve_piece *piece = &list[k];
      sleeve_params low, high, lo, hi;
      int count = 0, j;
      uint64_t seed;
      for (j = 0; j < (int)(sizeof named_params / sizeof named_params[0]); j++)
        count += strcmp(named_params[j].piece, piece->name) == 0;
      if (count != piece->param_count || piece->param_count > SLEEVE_MAX_PARAMS)
        failed = "its named indices are not its parameters";
      for (j = 0; j < piece->param_count && !failed; j++)
        if (piece->defaults->v[j] != piece->params[j].value) failed = "a default is not its own";
      memset(&low, 0, sizeof low);
      memset(&high, 0, sizeof high);
      for (j = 0; j < piece->param_count; j++) {
        low.v[j] = INT32_MIN;
        high.v[j] = INT32_MAX;
        lo.v[j] = piece->params[j].lo;
        hi.v[j] = piece->params[j].hi;
      }
      for (seed = 0; seed < 50 && !failed; seed++) {
        sleeve_builder b[2];
        sleeve_cover c[2];
        run_piece(piece, &low, seed, &b[0], &c[0]);
        run_piece(piece, &lo, seed, &b[1], &c[1]);
        if (memcmp(b, b + 1, sizeof b[0]) || memcmp(c, c + 1, sizeof c[0]))
          failed = "INT32_MIN for every parameter draws apart from each one's lowest";
        run_piece(piece, &high, seed, &b[0], &c[0]);
        run_piece(piece, &hi, seed, &b[1], &c[1]);
        if (!failed && (memcmp(b, b + 1, sizeof b[0]) || memcmp(c, c + 1, sizeof c[0])))
          failed = "INT32_MAX for every parameter draws apart from each one's highest";
      }
      if (failed) snprintf(detail, sizeof detail, "%s: %s", piece->name, failed);
      pieces++;
    }
  }
  if (!failed)
    snprintf(detail, sizeof detail, "%d named indices are sleeve_find_param's and %d pieces' "
             "defaults their own; each piece run by hand with every parameter at INT32_MIN or "
             "INT32_MAX draws as at its lowest or highest, over 50 seeds",
             (int)(sizeof named_params / sizeof named_params[0]), pieces);
  report("parameters", !failed, detail);
}

/* ---- Style checks and fingerprints ---- */

static sleeve_mood_def test_mood;
static sleeve_style_mood test_moods[2];
static sleeve_params test_horizon;
static sleeve_style_part test_parts[2];
static sleeve_style_part test_finishers[4];
static sleeve_style_part test_hooks[1];
static uint16_t test_row[2];
static sleeve_style test_style;

/* A valid style using every part, rebuilt before each change a check makes to it. */
static sleeve_style *fresh_style(void) {
  static const sleeve_params warm = {{0xf0d0a0, 200}};
  test_mood = *sleeve_find_mood("sea");
  test_mood.name = "test";
  test_moods[0].mood = &test_mood, test_moods[0].weight = 2, test_moods[0].composition_weights = 0;
  test_moods[1].mood = sleeve_find_mood("ember"), test_moods[1].weight = 1;
  test_row[0] = 1, test_row[1] = 0;
  test_moods[1].composition_weights = test_row;
  test_horizon = *sleeve_find_piece(SLEEVE_COMPOSE, "horizon")->defaults;
  memset(test_parts, 0, sizeof test_parts);
  test_parts[0].name = "horizon", test_parts[0].run = sleeve_compose_horizon;
  test_parts[0].params = &test_horizon, test_parts[0].weight = 3;
  test_parts[1].name = "ringed", test_parts[1].run = ringed_horizon, test_parts[1].weight = 1;
  test_parts[1].version = 1;
  memset(test_finishers, 0, sizeof test_finishers);
  test_finishers[0].name = "frame", test_finishers[0].run = sleeve_finish_frame;
  test_finishers[0].layers = 2;
  test_finishers[1] = test_finishers[2] = test_finishers[3] = test_finishers[0];
  memset(test_hooks, 0, sizeof test_hooks);
  test_hooks[0].name = "tint";
  test_hooks[0].run = sleeve_palette_tint;
  test_hooks[0].params = &warm;
  memset(&test_style, 0, sizeof test_style);
  test_style.name = "test";
  test_style.moods = test_moods, test_style.mood_count = 2;
  test_style.compositions = test_parts, test_style.composition_count = 2;
  test_style.palette_hooks = test_hooks, test_style.palette_hook_count = 1;
  test_style.finishers = test_finishers, test_style.finisher_count = 1;
  return &test_style;
}

/* Invalid styles are refused with a reason a compiler can report; valid ones are accepted. */
static void check_style_check(void) {
  static const char *const expected[] = {
    "the moods' weights are all 0", "hue_lo 400 is outside 0 to 359",
    "mid light_hi 1200 is outside 520 to 1000", "accent chroma_hi 10 is outside 70 to 400",
    "exact alpha is neither 0 nor 255", "the compositions' weights are all 0",
    "compositions[0] (horizon): sun 101 is outside 0 to 100",
    "compositions[0] (horizon): sun_min 1600 is above sun_max 1500", "has no function",
    "has no name", "reserves 1 layers but \"frame\" adds up to 2",
    "the finishers reserve 8 layers, more than 6", "mood_count 33 is outside 0 to 32",
    "moods[1] (ember): its composition weights are all 0",
    "runs the built-in palette hook \"tint\", which is not a composition",
    "moods[0]: has no mood definition", "palette_hook_count is set but its list is NULL",
  };
  const int cases = (int)(sizeof expected / sizeof expected[0]);
  const sleeve_style *valid[] = {&sleeve_default_style, &riso_style, &brand_style, &gallery_style};
  sleeve_style empty;
  char reason[256], detail[1024], tiny[8];
  int i, refused = 0, accepted = 0, ok = 1;
  memset(&empty, 0, sizeof empty);
  detail[0] = 0;
  for (i = 0; i < cases; i++) {
    sleeve_style *s = fresh_style();
    switch (i) {
    case 0: test_moods[0].weight = test_moods[1].weight = 0; break;
    case 1: test_mood.hue_lo = 400; break;
    case 2: test_mood.roles[SLEEVE_MID].light_hi = 1200; break;
    case 3: test_mood.roles[SLEEVE_ACCENT].chroma_hi = 10; break;
    case 4: test_mood.exact[SLEEVE_DEEP].a = 7; break;
    case 5: test_parts[0].weight = test_parts[1].weight = 0; break;
    case 6: test_horizon.v[SLEEVE_HORIZON_SUN] = 101; break;
    case 7: test_horizon.v[SLEEVE_HORIZON_SUN_MIN] = 1600; break;
    case 8: test_parts[1].run = 0; break;
    case 9: test_parts[1].name = 0; break;
    case 10: test_finishers[0].layers = 1; break;
    case 11: s->finisher_count = 4; break;
    case 12: s->mood_count = 33; break;
    case 13: test_row[0] = 0; break;
    case 14: test_parts[1].run = sleeve_palette_tint; break;
    case 15: test_moods[0].mood = 0; break;
    default: s->palette_hooks = 0, s->palette_hook_count = 2; break;
    }
    if (sleeve_style_check(s, reason, sizeof reason) == 0) {
      if (ok) snprintf(detail, sizeof detail, "accepted a style that should fail with \"%s\"",
                       expected[i]);
      ok = 0;
    } else if (!strstr(reason, expected[i])) {
      if (ok) snprintf(detail, sizeof detail, "refused for \"%s\", expected \"%s\"", reason,
                       expected[i]);
      ok = 0;
    } else {
      refused++;
    }
  }
  for (i = 0; i < 5; i++) {
    const sleeve_style *s = i < 4 ? valid[i] : fresh_style();
    if (sleeve_style_check(s, reason, sizeof reason) == 0 && reason[0] == 0) accepted++;
    else if (ok) {
      snprintf(detail, sizeof detail, "refused the valid style %s: %s", s->name, reason);
      ok = 0;
    }
  }
  if (sleeve_style_check(&empty, reason, sizeof reason) == 0) accepted++;
  else if (ok) snprintf(detail, sizeof detail, "refused the empty style: %s", reason), ok = 0;
  fresh_style();
  test_horizon.v[SLEEVE_HORIZON_SUN] = 101;
  if (sleeve_style_check(&test_style, tiny, sizeof tiny) == 0 || strlen(tiny) != sizeof tiny - 1) {
    if (ok) snprintf(detail, sizeof detail, "a reason cut to a small capacity is not terminated");
    ok = 0;
  }
  if (ok) snprintf(detail, sizeof detail, "%d invalid styles refused, each for its reason; the "
                   "default, example, empty and a full test style (%d) accepted; a reason cut "
                   "short stays terminated", refused, accepted);
  report("style-check", ok, detail);
}

/* Equal styles fingerprint equal, any change that decides covers changes the fingerprint, and
   names do not. */
static void check_style_hash(void) {
  static const char *const changes[] = {
    "a mood's weight", "a role's lightness range", "an exact colour", "a mood's composition weight",
    "a composition's weight", "a built-in's parameter", "a finisher's parameter",
    "the layers a finisher keeps", "an added finisher", "a palette hook's parameter",
    "a code hook's name", "a code hook's version", "the order of compositions",
  };
  static const sleeve_params thin = {{SLEEVE_PALE, 255, 300}};
  static const sleeve_params cool = {{0xa0c0f0, 200}};
  const int n = (int)(sizeof changes / sizeof changes[0]);
  sleeve_style_mood moods[SLEEVE_MOODS];
  sleeve_style_part parts[SLEEVE_COMPOSITIONS], swapped[2];
  sleeve_style declared;
  sleeve_params defaults;
  uint64_t base;
  char detail[512];
  const char *failed = 0, *unchanged = 0;
  int i;
  declared_default(&declared, moods, parts);
  if (sleeve_style_hash(&declared) != sleeve_style_hash(&sleeve_default_style))
    failed = "the default style declared from names fingerprints apart from it";
  base = sleeve_style_hash(fresh_style());
  defaults = *sleeve_find_piece(SLEEVE_COMPOSE, "horizon")->defaults;
  test_parts[0].params = &defaults;
  if (!failed && sleeve_style_hash(&test_style) != base)
    failed = "default parameters written out fingerprint apart from the same as NULL";
  test_parts[0].params = 0;
  if (!failed && sleeve_style_hash(&test_style) != base)
    failed = "NULL parameters fingerprint apart from the defaults written out";
  fresh_style();
  test_style.name = "renamed";
  test_mood.name = "renamed";
  if (!failed && sleeve_style_hash(&test_style) != base)
    failed = "renaming the style or a mood changed the fingerprint";
  for (i = 0; i < n && !failed && !unchanged; i++) {
    fresh_style();
    switch (i) {
    case 0: test_moods[0].weight = 3; break;
    case 1: test_mood.roles[SLEEVE_PALE].light_lo += 1; break;
    case 2: test_mood.exact[SLEEVE_ACCENT] = hex(0xfca311); break;
    case 3: test_row[1] = 1; break;
    case 4: test_parts[0].weight = 4; break;
    case 5: test_horizon.v[SLEEVE_HORIZON_NIGHT] = 41; break;
    case 6: test_finishers[0].params = &thin; break;
    case 7: test_finishers[0].layers = 3; break;
    case 8: test_style.finisher_count = 2; break;
    case 9: test_hooks[0].params = &cool; break;
    case 10: test_parts[1].name = "ringed-2"; break;
    case 11: test_parts[1].version = 2; break;
    default:
      swapped[0] = test_parts[1], swapped[1] = test_parts[0];
      test_style.compositions = swapped;
      break;
    }
    if (sleeve_style_hash(&test_style) == base) unchanged = changes[i];
  }
  if (failed) snprintf(detail, sizeof detail, "%s", failed);
  else if (unchanged)
    snprintf(detail, sizeof detail, "changing %s left the fingerprint as it was", unchanged);
  else snprintf(detail, sizeof detail, "equal styles fingerprint equal, NULL parameters as the "
                "defaults and names aside; each of %d changes that decide covers changes it", n);
  report("style-hash", !failed && !unchanged, detail);
}

/* ---- A palette given for the call ---- */

static int lightness_of(sleeve_colour c) {
  int32_t l, ch, h;
  sleeve_to_oklch(c, &l, &ch, &h);
  return l;
}

/* A given palette's colours are assigned to the roles by lightness and chroma whatever their
   order, empty roles are derived from them, and the cover keeps them. */
static void check_given_palette(void) {
  static const uint32_t five[5] = {0x5c677d, 0xf4f4f0, 0xd62828, 0x101010, 0xc9b79c};
  sleeve_colour in[SLEEVE_MAX_GIVEN + 1], roles[5], first[5];
  sleeve_style_part bars[1] = {{"bars", sleeve_compose_bars, 0, 1, 0, 0}};
  sleeve_style only_bars = {"bars", 0, 0, bars, 1, 0, 0, 0, 0};
  const char *failed = 0;
  char detail[512], text[4096];
  int i, k, perm[5] = {0, 1, 2, 3, 4}, permutations = 0;
  /* five colours: deep, pale, accent and mid given; the second accent derived */
  for (i = 0; i < 5; i++) in[i] = hex(five[i]);
  sleeve_assign_palette(in, 5, first);
  if (!same_colour(first[SLEEVE_DEEP], hex(0x101010)) ||
      !same_colour(first[SLEEVE_PALE], hex(0xf4f4f0)) ||
      !same_colour(first[SLEEVE_ACCENT], hex(0xd62828)) ||
      !same_colour(first[SLEEVE_MID], hex(0x5c677d)))
    failed = "five colours: deep is not the darkest, pale the lightest, the accent the most "
             "chromatic or mid the nearest the middle";
  for (i = 0; i < 5 && !failed; i++)
    if (same_colour(first[SLEEVE_ACCENT2], in[i]))
      failed = "five colours: a grey sand was taken as the second accent";
  /* every order of the five gives the same roles */
  for (;;) {
    int j, l, t;
    for (i = 0; i < 5; i++) in[i] = hex(five[perm[i]]);
    sleeve_assign_palette(in, 5, roles);
    if (memcmp(roles, first, sizeof roles) != 0 && !failed)
      failed = "the roles depend on the colours' order";
    permutations++;
    for (j = 3; j >= 0 && perm[j] > perm[j + 1]; j--) {}
    if (j < 0) break;
    for (l = 4; perm[l] < perm[j]; l--) {}
    t = perm[j], perm[j] = perm[l], perm[l] = t;
    for (l = j + 1, k = 4; l < k; l++, k--) t = perm[l], perm[l] = perm[k], perm[k] = t;
  }
  /* one colour: a saturated red is the accent, the rest its tones */
  in[0] = hex(0xd62828);
  sleeve_assign_palette(in, 1, roles);
  if (!failed && (!same_colour(roles[SLEEVE_ACCENT], in[0]) ||
                  lightness_of(roles[SLEEVE_DEEP]) > 300 ||
                  lightness_of(roles[SLEEVE_PALE]) < 900 ||
                  lightness_of(roles[SLEEVE_MID]) <= lightness_of(roles[SLEEVE_DEEP]) ||
                  lightness_of(roles[SLEEVE_MID]) >= lightness_of(roles[SLEEVE_PALE])))
    failed = "one red: it is not the accent over darker, lighter and middle tones";
  for (i = 0; i < 5 && !failed; i++) {
    int32_t l, ch, h, l0, ch0, h0;
    sleeve_to_oklch(roles[i], &l, &ch, &h);
    sleeve_to_oklch(in[0], &l0, &ch0, &h0);
    if (ch >= 30 && hue_distance(h, h0) > 10) failed = "one red: a derived role left its hue";
  }
  in[0] = hex(0x1d3557);
  sleeve_assign_palette(in, 1, roles);
  if (!failed && !same_colour(roles[SLEEVE_DEEP], in[0])) failed = "one navy is not deep";
  in[0] = hex(0xf1e3c8);
  sleeve_assign_palette(in, 1, roles);
  if (!failed && !same_colour(roles[SLEEVE_PALE], in[0])) failed = "one cream is not pale";
  in[0] = hex(0x000000), in[1] = hex(0xffffff);
  sleeve_assign_palette(in, 2, roles);
  if (!failed) {
    int32_t l, ch, h;
    sleeve_to_oklch(roles[SLEEVE_MID], &l, &ch, &h);
    if (!same_colour(roles[SLEEVE_DEEP], in[0]) || !same_colour(roles[SLEEVE_PALE], in[1]) ||
        ch > 10)
      failed = "black and white are not deep and pale with a grey between";
  }
  /* counts, alpha, and the cover */
  for (i = 0; i <= SLEEVE_MAX_GIVEN; i++) {
    in[i] = hex(0x102030 + 0x0a0a0a * (uint32_t)i);
    in[i].a = 0;
  }
  {
    sleeve_cover c, before;
    memset(&c, 0x5a, sizeof c);
    before = c;
    if (!failed && (sleeve_generate_with_palette(1, 0, in, 0, &c) != -1 ||
                    sleeve_generate_with_palette(1, 0, in, SLEEVE_MAX_GIVEN + 1, &c) != -1 ||
                    !same_cover(&c, &before) || sleeve_assign_palette(in, 0, roles) != -1))
      failed = "0 or 17 colours were not refused with the cover untouched";
    if (!failed && sleeve_generate_with_palette(1, 0, in, SLEEVE_MAX_GIVEN, &c) != 0)
      failed = "16 colours were refused";
    for (k = 0; k < 5 && !failed; k++)
      if (c.palette[k].a != 255) failed = "a given colour's alpha was kept";
    for (i = 0; i < 300 && !failed; i++) {
      for (k = 0; k < 5; k++) in[k] = hex(five[k]);
      sleeve_generate_with_palette((uint64_t)i, &only_bars, in, 5, &c);
      sleeve_describe_styled(&c, &only_bars, text, sizeof text);
      if (memcmp(c.palette, first, sizeof first) != 0)
        failed = "the cover's palette is not the colours as assigned";
      else if (c.mood != SLEEVE_NO_MOOD || strncmp(text, "sleeve 1 bars given\n", 20) != 0)
        failed = "a cover from a given palette is not of mood \"given\"";
      else if (c.composition != 0 || sleeve_check(&c))
        failed = "the composition did not follow the style";
      sleeve_generate_with_palette((uint64_t)i, &riso_style, in, 5, &c);
      if (!failed && memcmp(c.palette, first, sizeof first) != 0)
        failed = "a style's palette hook ran on a given palette";
    }
  }
  if (failed) snprintf(detail, sizeof detail, "%s", failed);
  else snprintf(detail, sizeof detail, "five colours take their roles in all %d orders; one red, "
                "one navy, one cream, and black with white take theirs, with tones derived; 0 "
                "and 17 colours refused; covers keep the colours as assigned, of mood \"given\", "
                "composed by the style without its palette hooks", permutations);
  report("given-palette", !failed, detail);
}

/* ---- Determinism per style ---- */

#define STYLE_DIGEST_SEEDS 2000

/* A style using every built-in palette hook and finisher, as the goldens' last style. */
static const sleeve_style_part every_hook[5] = {
  {"tint", sleeve_palette_tint, 0, 0, 0, 0}, {"chroma", sleeve_palette_chroma, 0, 0, 0, 0},
  {"lightness", sleeve_palette_lightness, 0, 0, 0, 0},
  {"monochrome", sleeve_palette_monochrome, 0, 0, 0, 0},
  {"duotone", sleeve_palette_duotone, 0, 0, 0, 0},
};
static const sleeve_style_part every_finisher[4] = {
  {"vignette", sleeve_finish_vignette, 0, 0, 1, 0}, {"wash", sleeve_finish_wash, 0, 0, 1, 0},
  {"frame", sleeve_finish_frame, 0, 0, 2, 0}, {"lines", sleeve_finish_lines, 0, 0, 1, 0},
};

static size_t styles_text(char *out, size_t capacity) {
  static const uint32_t album[5] = {0x264653, 0x2a9d8f, 0xe9c46a, 0xf4a261, 0xe76f51};
  sleeve_style hooks = {"hooks", 0, 0, 0, 0, every_hook, 4, every_finisher, 4};
  sleeve_style duotone = {"duotone", 0, 0, 0, 0, every_hook + 4, 1, 0, 0};
  const sleeve_style *styles[EXAMPLE_STYLES + 2];
  sleeve_colour colours[5];
  size_t n = 0;
  int s, i, k, count = 0;
  char line[8192];
  for (s = 0; s < EXAMPLE_STYLES; s++) styles[count++] = example_styles[s];
  styles[count++] = &hooks;
  styles[count++] = &duotone;
  for (k = 0; k < 5; k++) colours[k] = hex(album[k]);
  for (s = 0; s < count + 2; s++) {
    const sleeve_style *style = s < count ? styles[s] : (s == count ? &sleeve_default_style
                                                                    : &gallery_style);
    uint64_t digest = 0xcbf29ce484222325ull;
    for (i = 0; i < STYLE_DIGEST_SEEDS; i++) {
      sleeve_cover c;
      if (s < count) sleeve_generate_styled((uint64_t)i, style, &c);
      else sleeve_generate_with_palette((uint64_t)i, style, colours, 5, &c);
      digest = fnv(digest, line, sleeve_describe_styled(&c, style, line, sizeof line));
    }
    n += (size_t)snprintf(out + n, capacity - n,
                          "%s%s: fingerprint %016llx, seeds 0 to %d %016llx\n", style->name,
                          s < count ? "" : " with a given palette",
                          (unsigned long long)sleeve_style_hash(style), STYLE_DIGEST_SEEDS - 1,
                          (unsigned long long)digest);
  }
  return n;
}

/* Each style draws the same descriptions and fingerprint as its golden, whatever the cover
   held before. */
static void check_style_determinism(void) {
  static char text[1 << 14];
  char path[1024], detail[512];
  size_t n = styles_text(text, sizeof text), m = 0, i, line = 1;
  char *golden;
  int s, seed, stale = -1;
  snprintf(path, sizeof path, "%s/styles.txt", golden_dir);
  if (write_goldens) {
    report("style-determinism", write_file(path, text, n), "written");
    return;
  }
  for (s = 0; s < EXAMPLE_STYLES && stale < 0; s++)
    for (seed = 0; seed < 200 && stale < 0; seed++) {
      sleeve_cover a, b;
      memset(&a, 0x00, sizeof a);
      memset(&b, 0xa5, sizeof b);
      sleeve_generate_styled((uint64_t)seed, example_styles[s], &a);
      sleeve_generate_styled((uint64_t)seed, example_styles[s], &b);
      if (!same_cover(&a, &b)) stale = s;
    }
  golden = read_file(path, &m);
  if (!golden) {
    report("style-determinism", 0, "no golden file");
    return;
  }
  for (i = 0; i < n && i < m && text[i] == golden[i]; i++)
    if (text[i] == '\n') line++;
  if (stale >= 0)
    snprintf(detail, sizeof detail, "style %s draws different bytes over different old covers",
             example_styles[stale]->name);
  else if (i != n || n != m)
    snprintf(detail, sizeof detail, "differs from the golden file at line %zu", line);
  else
    snprintf(detail, sizeof detail, "%d seeds each of the example styles, every built-in hook, "
             "and given palettes equal their golden digests and fingerprints, whatever the cover "
             "held before", STYLE_DIGEST_SEEDS);
  report("style-determinism", stale < 0 && i == n && n == m, detail);
  free(golden);
}

static const struct {
  const char *name;
  void (*run)(void);
  int golden; /* writes a golden file under --write-goldens */
} checks[] = {
  {"descriptions", check_descriptions, 1}, {"rasters", check_rasters, 1},
  {"antialiasing", check_antialiasing, 0}, {"png", check_png, 0}, {"svg", check_svg, 0},
  {"variety", check_variety, 0}, {"api", check_api, 0},
  {"extremes", check_extremes, 0}, {"default-style", check_default_style, 0},
  {"style-shares", check_style_shares, 0}, {"style-hooks", check_style_hooks, 0},
  {"builder", check_builder, 0}, {"containment", check_containment, 0},
  {"parameters", check_parameters, 0},
  {"style-check", check_style_check, 0}, {"style-hash", check_style_hash, 0},
  {"given-palette", check_given_palette, 0},
  {"style-determinism", check_style_determinism, 1},
};
#define CHECKS ((int)(sizeof checks / sizeof checks[0]))

int main(int argc, char **argv) {
  const char *only[CHECKS];
  int n_only = 0, i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--golden") == 0 && i + 1 < argc) golden_dir = argv[++i];
    else if (strcmp(argv[i], "--scratch") == 0 && i + 1 < argc) scratch_dir = argv[++i];
    else if (strcmp(argv[i], "--write-goldens") == 0) write_goldens = 1;
    else if (strcmp(argv[i], "--require-svg") == 0) require_svg = 1;
    else if (argv[i][0] != '-' && n_only < CHECKS) only[n_only++] = argv[i];
    else {
      fprintf(stderr, "usage: check [--golden DIR] [--scratch DIR] [--require-svg] "
                      "[--write-goldens] [CHECK...]\n");
      return 2;
    }
  }
  for (i = 0; i < CHECKS; i++) {
    int k, want = n_only == 0;
    for (k = 0; k < n_only; k++)
      if (strcmp(only[k], checks[i].name) == 0) want = 1;
    if (!want || (write_goldens && !checks[i].golden)) continue;
    checks[i].run();
  }
  return failures ? 1 : 0;
}
