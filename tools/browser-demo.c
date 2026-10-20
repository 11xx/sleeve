/* The page's entry points into sleeve: one cover at a time, into one buffer the page reads. */
#define SLEEVE_NO_STDIO
#define SLEEVE_IMPLEMENTATION
#include "../sleeve.h"

#define LARGEST 640

static uint8_t pixels[LARGEST * LARGEST * 4];
static sleeve_cover cover;

__attribute__((export_name("pixels"))) uint8_t *page_pixels(void) { return pixels; }

/* Generates the cover of seed hi:lo and rasterizes it size x size; 0, or -1 on a bad size. */
__attribute__((export_name("cover"))) int page_cover(uint32_t hi, uint32_t lo, int size) {
  if (size < 1 || size > LARGEST) return -1;
  sleeve_generate(((uint64_t)hi << 32) | lo, &cover);
  return sleeve_render(&cover, size, size, pixels, (ptrdiff_t)size * 4);
}

__attribute__((export_name("composition"))) const char *page_composition(void) {
  return sleeve_composition_name(cover.composition);
}

__attribute__((export_name("mood"))) const char *page_mood(void) {
  return sleeve_mood_name(cover.mood);
}
