/* Example styles beside sleeve's default, for the command-line tool and the browser demo. Each
   shapes covers through a different part of a style:
   - riso: a palette hook, duotone, prints every palette in two inks, and a finisher lays fine
     lines over the cover, as a risograph print;
   - brand: one mood whose deep, pale and accent roles are pinned to exact colours;
   - gallery: three compositions, one of them a code hook built on the builder, each framed by
     a finisher as a print on a mat.
   Include it after sleeve.h; it needs no C library. */

static const sleeve_params riso_inks = {{0x24418f, 0xf4ecdc}};
static const sleeve_params riso_screen = {{SLEEVE_DEEP, 34, 45, 130, 40}};

static const sleeve_style_part riso_palette_hooks[] = {
  {"duotone", sleeve_palette_duotone, &riso_inks, 0, 0, 0},
};
static const sleeve_style_part riso_finishers[] = {
  {"lines", sleeve_finish_lines, &riso_screen, 0, 1, 0},
};

static const sleeve_style riso_style = {
  "riso", 0, 0, 0, 0, riso_palette_hooks, 1, riso_finishers, 1,
};

/* A navy, cream and amber identity: those three are exact, the rest drawn around them. */
static const sleeve_mood_def brand_mood = {
  "brand", 255, 265,
  {{0, 0, 0, 0, 0, 0}, {-10, 10, 420, 500, 70, 100}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
   {-175, -165, 820, 860, 70, 95}},
  {{0x14, 0x21, 0x3d, 255}, {0, 0, 0, 0}, {0xf4, 0xf1, 0xea, 255}, {0xfc, 0xa3, 0x11, 255},
   {0, 0, 0, 0}},
};

static const sleeve_style_mood brand_moods[] = {{&brand_mood, 1, 0}};

static const sleeve_style brand_style = {"brand", brand_moods, 1, 0, 0, 0, 0, 0, 0};

/* An arch standing on a floor in the middle of a plain ground, with a sun or moon inside it:
   a composition written on the builder alone. */
static void compose_arch(sleeve_builder *b, const void *params) {
  int dark = sleeve_chance(b, 45);
  int32_t half = sleeve_range(b, 1700, 2500), top = sleeve_range(b, 3400, 4600);
  int32_t floor_at = sleeve_range(b, 7500, 8500), sun = sleeve_range(b, 500, 900);
  int32_t sun_y = top + sleeve_range(b, -400, 1600);
  sleeve_colour ground = b->palette[dark ? SLEEVE_DEEP : SLEEVE_PALE];
  sleeve_colour arch = b->palette[sleeve_chance(b, 70) ? SLEEVE_MID : SLEEVE_ACCENT2];
  sleeve_colour floor = b->palette[dark ? SLEEVE_MID : SLEEVE_DEEP];
  sleeve_layer *l;
  (void)params;
  b->cover->base = ground;
  /* the arch's sides, capped at the top of its rounded head by the ground above them */
  l = sleeve_add_linear(b, 90);
  sleeve_add_stop(l, 5000 - half, sleeve_alpha(arch, 0));
  sleeve_add_band(l, 5000 - half, 5000 + half, arch);
  sleeve_add_stop(l, 5000 + half, sleeve_alpha(arch, 0));
  l = sleeve_add_linear(b, 180);
  sleeve_add_band(l, 0, top, ground);
  sleeve_add_stop(l, top, sleeve_alpha(ground, 0));
  sleeve_add_disc(b, 5000, top, half, arch);
  sleeve_add_disc(b, 5000, sun_y, sun, b->palette[dark ? SLEEVE_PALE : SLEEVE_ACCENT]);
  l = sleeve_add_linear(b, 180);
  sleeve_add_stop(l, floor_at, sleeve_alpha(floor, 0));
  sleeve_add_band(l, floor_at, SLEEVE_UNIT, floor);
}

static const sleeve_params gallery_orbs = {{50, 75, 40}};
static const sleeve_params gallery_split = {{30, 150, 20, 80, 1200, 2200}};
static const sleeve_params gallery_mat = {{SLEEVE_PALE, 255, 750}};

static const sleeve_style_part gallery_compositions[] = {
  {"arch", compose_arch, 0, 5, 0, 1},
  {"orbs", sleeve_compose_orbs, &gallery_orbs, 3, 0, 0},
  {"split", sleeve_compose_split, &gallery_split, 2, 0, 0},
};
static const sleeve_style_part gallery_finishers[] = {
  {"frame", sleeve_finish_frame, &gallery_mat, 0, 2, 0},
};

static const sleeve_style gallery_style = {
  "gallery", 0, 0, gallery_compositions, 3, 0, 0, gallery_finishers, 1,
};

static const sleeve_style *const example_styles[] = {
  &sleeve_default_style, &riso_style, &brand_style, &gallery_style,
};
#define EXAMPLE_STYLES ((int)(sizeof example_styles / sizeof example_styles[0]))
