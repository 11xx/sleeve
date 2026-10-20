/* sleeve's checks. Each prints one line per check and the program exits non-zero when any fails.

   usage: check [--golden DIR] [--scratch DIR] [--require-svg] [--write-goldens] [CHECK...]

   CHECK is any of descriptions, rasters, antialiasing, png, svg, variety, api; all by default.
   --golden names the directory of golden files (tests/golden by default); --write-goldens
   writes them instead of checking them. --scratch is where the SVG check writes its files
   (the system's temporary directory by default). The SVG check renders with rsvg-convert, or
   the command SLEEVE_SVG_RENDERER names; without one it is skipped unless --require-svg. */

#define SLEEVE_IMPLEMENTATION
#include "sleeve.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char **argv) {
  const char *only[16];
  int n_only = 0, i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--golden") == 0 && i + 1 < argc) golden_dir = argv[++i];
    else if (strcmp(argv[i], "--scratch") == 0 && i + 1 < argc) scratch_dir = argv[++i];
    else if (strcmp(argv[i], "--write-goldens") == 0) write_goldens = 1;
    else if (strcmp(argv[i], "--require-svg") == 0) require_svg = 1;
    else if (argv[i][0] != '-' && n_only < 16) only[n_only++] = argv[i];
    else {
      fprintf(stderr, "usage: check [--golden DIR] [--scratch DIR] [--require-svg] "
                      "[--write-goldens] [CHECK...]\n");
      return 2;
    }
  }
  for (i = 0; i < 7; i++) {
    static const char *const names[7] = {"descriptions", "rasters", "antialiasing", "png",
                                         "svg", "variety", "api"};
    int k, want = n_only == 0;
    for (k = 0; k < n_only; k++)
      if (strcmp(only[k], names[i]) == 0) want = 1;
    if (!want) continue;
    if (write_goldens && i > 1) continue;
    switch (i) {
    case 0: check_descriptions(); break;
    case 1: check_rasters(); break;
    case 2: check_antialiasing(); break;
    case 3: check_png(); break;
    case 4: check_svg(); break;
    case 5: check_variety(); break;
    default: check_api(); break;
    }
  }
  return failures ? 1 : 0;
}
