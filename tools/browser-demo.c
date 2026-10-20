/* The page's entry points into sleeve: one cover at a time, into one buffer the page reads,
   in the style the page picked, from a palette the page gave or the style's moods. */
#define SLEEVE_NO_STDIO
#define SLEEVE_IMPLEMENTATION
#include "../sleeve.h"
#include "styles.h"

#define LARGEST 640

static uint8_t pixels[LARGEST * LARGEST * 4];
static uint8_t given[SLEEVE_MAX_GIVEN * 3];
static sleeve_colour colours[SLEEVE_MAX_GIVEN];
static int colour_count;
static const sleeve_style *style = &sleeve_default_style;
static sleeve_cover cover;

__attribute__((export_name("pixels"))) uint8_t *page_pixels(void) { return pixels; }

__attribute__((export_name("style_count"))) int page_style_count(void) { return EXAMPLE_STYLES; }

__attribute__((export_name("style_name"))) const char *page_style_name(int i) {
  return i >= 0 && i < EXAMPLE_STYLES ? example_styles[i]->name : "";
}

/* Picks the style covers are drawn in; 0, or -1 for no such style. */
__attribute__((export_name("set_style"))) int page_set_style(int i) {
  if (i < 0 || i >= EXAMPLE_STYLES) return -1;
  style = example_styles[i];
  return 0;
}

/* Where the page writes a palette, three bytes (r, g, b) a colour. */
__attribute__((export_name("palette"))) uint8_t *page_palette(void) { return given; }

/* Takes the first `count` colours written to the palette, or none for a count of 0; 0, or -1
   for a count out of range. */
__attribute__((export_name("set_palette"))) int page_set_palette(int count) {
  int i;
  if (count < 0 || count > SLEEVE_MAX_GIVEN) return -1;
  for (i = 0; i < count; i++) {
    colours[i].r = given[i * 3];
    colours[i].g = given[i * 3 + 1];
    colours[i].b = given[i * 3 + 2];
    colours[i].a = 255;
  }
  colour_count = count;
  return 0;
}

/* Generates the cover of seed hi:lo and rasterizes it size x size; 0, or -1 on a bad size. */
__attribute__((export_name("cover"))) int page_cover(uint32_t hi, uint32_t lo, int size) {
  uint64_t seed = ((uint64_t)hi << 32) | lo;
  if (size < 1 || size > LARGEST) return -1;
  if (colour_count) sleeve_generate_with_palette(seed, style, colours, colour_count, &cover);
  else sleeve_generate_styled(seed, style, &cover);
  return sleeve_render(&cover, size, size, pixels, (ptrdiff_t)size * 4);
}

__attribute__((export_name("composition"))) const char *page_composition(void) {
  return sleeve_style_composition_name(style, cover.composition);
}

__attribute__((export_name("mood"))) const char *page_mood(void) {
  return sleeve_style_mood_name(style, cover.mood);
}
