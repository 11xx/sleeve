/* sleeve: writes covers, contact sheets and descriptions from the command line.
   See usage() below and the README. */

#define SLEEVE_IMPLEMENTATION
#include "../sleeve.h"
#include "styles.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void usage(FILE *to) {
  fputs("usage:\n"
        "  sleeve cover [--size PX] [--svg] [--out PATH] [LOOK] SEED...\n"
        "      Writes each seed's cover, PX pixels square (512 by default), as a PNG, or as an\n"
        "      SVG with --svg. One seed goes to PATH; several go into the directory PATH, each\n"
        "      named by its seed in hexadecimal. PATH defaults to the current directory.\n"
        "  sleeve sheet [--size PX] [--columns N] [--gap PX] [--first SEED] [LOOK] COUNT OUT.png\n"
        "      Writes COUNT covers from seed FIRST (0 by default) on, PX pixels each (160 by\n"
        "      default), in a grid N wide (8 by default) with GAP pixels between (8 by default).\n"
        "  sleeve describe [LOOK] SEED...\n"
        "      Prints each seed's description.\n"
        "  sleeve seed SEED...\n"
        "      Prints each seed as a decimal number.\n"
        "  sleeve styles\n"
        "      Lists the styles, each with its fingerprint.\n"
        "  sleeve pieces\n"
        "      Lists the built-in compositions, palette hooks and finishers with their\n"
        "      parameters' ranges and defaults.\n"
        "SEED is a decimal number, a 0x-prefixed hexadecimal one, or key:TEXT for the hash of\n"
        "TEXT's bytes. LOOK is any of:\n"
        "  --style NAME       draws in a style: default, riso, brand or gallery\n"
        "  --palette COLOURS  draws from the given colours, 1 to 16 as #rrggbb separated by\n"
        "                     commas, in place of the style's moods\n",
        to);
}

/* A style by name, or NULL. */
static const sleeve_style *find_style(const char *name) {
  int i;
  for (i = 0; i < EXAMPLE_STYLES; i++)
    if (strcmp(example_styles[i]->name, name) == 0) return example_styles[i];
  return 0;
}

/* How covers are drawn: a style, and a palette given for each cover or none. */
typedef struct look {
  const sleeve_style *style;
  sleeve_colour colours[SLEEVE_MAX_GIVEN];
  int count;
} look;

static int parse_palette(const char *text, look *l) {
  l->count = 0;
  while (*text) {
    unsigned v = 0;
    int digits = 0;
    if (l->count == SLEEVE_MAX_GIVEN) return -1;
    if (*text == '#') text++;
    for (; digits < 6; digits++, text++) {
      char c = *text;
      int d = c >= '0' && c <= '9' ? c - '0'
            : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
      if (d < 0) return -1;
      v = v * 16 + (unsigned)d;
    }
    l->colours[l->count].r = (uint8_t)(v >> 16);
    l->colours[l->count].g = (uint8_t)(v >> 8);
    l->colours[l->count].b = (uint8_t)v;
    l->colours[l->count].a = 255;
    l->count++;
    if (*text == ',') text++;
    else if (*text) return -1;
  }
  return l->count > 0 ? 0 : -1;
}

/* Takes a LOOK option at argv[*i]: 1 when taken, 0 when it is another option, -1 when bad. */
static int parse_look(int argc, char **argv, int *i, look *l) {
  if (strcmp(argv[*i], "--style") == 0 && *i + 1 < argc) {
    if (!(l->style = find_style(argv[++*i]))) {
      fprintf(stderr, "sleeve: no style %s\n", argv[*i]);
      return -1;
    }
    return 1;
  }
  if (strcmp(argv[*i], "--palette") == 0 && *i + 1 < argc) {
    if (parse_palette(argv[++*i], l)) {
      fprintf(stderr, "sleeve: not a palette of 1 to 16 colours: %s\n", argv[*i]);
      return -1;
    }
    return 1;
  }
  return 0;
}

static void draw(const look *l, uint64_t seed, sleeve_cover *cover) {
  if (l->count) sleeve_generate_with_palette(seed, l->style, l->colours, l->count, cover);
  else sleeve_generate_styled(seed, l->style, cover);
}

static int parse_seed(const char *text, uint64_t *seed) {
  char *end;
  if (strncmp(text, "key:", 4) == 0) {
    *seed = sleeve_hash(text + 4, strlen(text + 4));
    return 0;
  }
  if (*text == '-' || *text == 0) return -1;
  errno = 0;
  *seed = strtoull(text, &end, 0);
  return errno || *end ? -1 : 0;
}

static int parse_int(const char *text, int lo, int hi, int *out) {
  char *end;
  long v;
  errno = 0;
  v = strtol(text, &end, 10);
  if (errno || *end || v < lo || v > hi) return -1;
  *out = (int)v;
  return 0;
}

static int is_directory(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int cover_command(int argc, char **argv) {
  int size = 512, svg = 0, i, first, failed = 0, taken;
  const char *out = ".";
  look l = {&sleeve_default_style, {{0, 0, 0, 0}}, 0};
  for (i = 0; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
    if ((taken = parse_look(argc, argv, &i, &l)) != 0) {
      if (taken < 0) return 2;
    } else if (strcmp(argv[i], "--svg") == 0) svg = 1;
    else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      if (parse_int(argv[++i], 1, 16384, &size)) return fprintf(stderr, "bad size\n"), 2;
    } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
    else return usage(stderr), 2;
  }
  first = i;
  if (first == argc) return usage(stderr), 2;
  for (i = first; i < argc; i++) {
    uint64_t seed;
    sleeve_cover cover;
    char path[4096];
    int result;
    if (parse_seed(argv[i], &seed)) {
      fprintf(stderr, "sleeve: not a seed: %s\n", argv[i]);
      return 2;
    }
    draw(&l, seed, &cover);
    if (argc - first == 1 && !is_directory(out)) snprintf(path, sizeof path, "%s", out);
    else
      snprintf(path, sizeof path, "%s/%016llx.%s", out, (unsigned long long)seed,
               svg ? "svg" : "png");
    result = svg ? sleeve_write_svg(path, &cover, size, size)
                 : sleeve_write_png(path, &cover, size, size);
    if (result) {
      fprintf(stderr, "sleeve: could not write %s\n", path);
      failed = 1;
    }
  }
  return failed;
}

static int sheet_command(int argc, char **argv) {
  int size = 160, columns = 8, gap = 8, count, rows, width, height, i;
  uint64_t first = 0;
  const char *path;
  uint8_t *pixels, *png;
  size_t bound, length;
  int result = 1, taken;
  look l = {&sleeve_default_style, {{0, 0, 0, 0}}, 0};
  for (i = 0; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
    if (i + 1 >= argc) return usage(stderr), 2;
    if ((taken = parse_look(argc, argv, &i, &l)) != 0) {
      if (taken < 0) return 2;
    } else if (strcmp(argv[i], "--size") == 0) {
      if (parse_int(argv[++i], 8, 4096, &size)) return fprintf(stderr, "bad size\n"), 2;
    } else if (strcmp(argv[i], "--columns") == 0) {
      if (parse_int(argv[++i], 1, 256, &columns)) return fprintf(stderr, "bad columns\n"), 2;
    } else if (strcmp(argv[i], "--gap") == 0) {
      if (parse_int(argv[++i], 0, 256, &gap)) return fprintf(stderr, "bad gap\n"), 2;
    } else if (strcmp(argv[i], "--first") == 0) {
      if (parse_seed(argv[++i], &first)) return fprintf(stderr, "bad seed\n"), 2;
    } else return usage(stderr), 2;
  }
  if (argc - i != 2 || parse_int(argv[i], 1, 4096, &count)) return usage(stderr), 2;
  path = argv[i + 1];
  if (columns > count) columns = count;
  rows = (count + columns - 1) / columns;
  width = columns * size + (columns + 1) * gap;
  height = rows * size + (rows + 1) * gap;
  if (width > 16384 || height > 16384) return fprintf(stderr, "sheet too large\n"), 2;
  pixels = malloc((size_t)width * height * 4);
  bound = sleeve_png_bound(width, height);
  png = malloc(bound);
  if (!pixels || !png) return fprintf(stderr, "out of memory\n"), 1;
  for (i = 0; i < width * height; i++) {
    pixels[i * 4 + 0] = 0x24;
    pixels[i * 4 + 1] = 0x23;
    pixels[i * 4 + 2] = 0x22;
    pixels[i * 4 + 3] = 255;
  }
  for (i = 0; i < count; i++) {
    sleeve_cover cover;
    int x = gap + (i % columns) * (size + gap), y = gap + (i / columns) * (size + gap);
    draw(&l, first + (uint64_t)i, &cover);
    sleeve_render(&cover, size, size, pixels + ((size_t)y * width + x) * 4, (ptrdiff_t)width * 4);
  }
  length = sleeve_png(pixels, width, height, (ptrdiff_t)width * 4, png, bound);
  if (length > 0) {
    FILE *f = fopen(path, "wb");
    if (f && fwrite(png, 1, length, f) == length && fclose(f) == 0) result = 0;
    else fprintf(stderr, "sleeve: could not write %s\n", path);
  }
  free(pixels);
  free(png);
  return result;
}

static int describe_command(int argc, char **argv, int seed_only) {
  int i, taken;
  look l = {&sleeve_default_style, {{0, 0, 0, 0}}, 0};
  for (i = 0; !seed_only && i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
    if ((taken = parse_look(argc, argv, &i, &l)) <= 0) return taken < 0 ? 2 : (usage(stderr), 2);
  if (i == argc) return usage(stderr), 2;
  for (; i < argc; i++) {
    uint64_t seed;
    sleeve_cover cover;
    char text[8192];
    if (parse_seed(argv[i], &seed)) {
      fprintf(stderr, "sleeve: not a seed: %s\n", argv[i]);
      return 2;
    }
    if (seed_only) {
      printf("%llu\n", (unsigned long long)seed);
      continue;
    }
    draw(&l, seed, &cover);
    sleeve_describe_styled(&cover, l.style, text, sizeof text);
    printf("seed %llu\n%s", (unsigned long long)seed, text);
  }
  return 0;
}

static int styles_command(void) {
  int i, failed = 0;
  for (i = 0; i < EXAMPLE_STYLES; i++) {
    char reason[256];
    int ok = sleeve_style_check(example_styles[i], reason, sizeof reason) == 0;
    printf("%-8s %016llx%s%s\n", example_styles[i]->name,
           (unsigned long long)sleeve_style_hash(example_styles[i]), ok ? "" : "  refused: ",
           reason);
    if (!ok) failed = 1;
  }
  return failed;
}

static int pieces_command(void) {
  static const char *const kinds[3] = {"composition", "palette", "finisher"};
  int kind, i, j, n;
  for (kind = SLEEVE_COMPOSE; kind <= SLEEVE_FINISH; kind++) {
    const sleeve_piece *pieces = sleeve_pieces(kind, &n);
    for (i = 0; i < n; i++) {
      printf("%s %s", kinds[kind], pieces[i].name);
      if (kind == SLEEVE_FINISH) printf(" (%d layer%s)", pieces[i].layers,
                                        pieces[i].layers == 1 ? "" : "s");
      putchar('\n');
      for (j = 0; j < pieces[i].param_count; j++) {
        const sleeve_param *p = &pieces[i].params[j];
        printf("  %-12s %d to %d, default %d", p->name, p->lo, p->hi, p->value);
        if (p->at_most >= 0) printf(", at most %s", pieces[i].params[p->at_most].name);
        putchar('\n');
      }
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) return usage(stderr), 2;
  if (strcmp(argv[1], "cover") == 0) return cover_command(argc - 2, argv + 2);
  if (strcmp(argv[1], "sheet") == 0) return sheet_command(argc - 2, argv + 2);
  if (strcmp(argv[1], "describe") == 0) return describe_command(argc - 2, argv + 2, 0);
  if (strcmp(argv[1], "seed") == 0) return describe_command(argc - 2, argv + 2, 1);
  if (strcmp(argv[1], "styles") == 0 && argc == 2) return styles_command();
  if (strcmp(argv[1], "pieces") == 0 && argc == 2) return pieces_command();
  if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) return usage(stdout), 0;
  return usage(stderr), 2;
}
