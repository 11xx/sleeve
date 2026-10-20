# sleeve's styles and given palettes, looked at and timed

Every sheet is `sleeve sheet` output, seeds from 0 on, 128 px a cover, and can be redrawn
from the command shown.

## Styles

`sleeve sheet --style NAME --size 128 --columns 8 32 style-NAME.png`

| Sheet | Style | Fingerprint | What it shows |
| --- | --- | --- | --- |
| `style-default.png` | default | `15a46aaa23eb9849` | sleeve's own covers. The same covers as `sleeve sheet` without a style, byte for byte |
| `style-riso.png` | riso | `70eaf05cbb6af4ca` | the default compositions and moods, with every palette pressed to two inks (blue `#24418f`, cream `#f4ecdc`) by the duotone palette hook. The lines finisher draws a fine screen in the deep ink over each cover, like a risograph print. The screen is faint at this size and plain at 512 px |
| `style-brand.png` | brand | `86a72312c646ea32` | one mood that pins deep, pale and accent to navy `#14213d`, cream `#f4f1ea` and amber `#fca311`. The mid and second accent are drawn from narrow ranges around them, so every cover reads as one identity across all twelve compositions |
| `style-gallery.png` | gallery | `7ba13c2b94b47df7` | three compositions, weighted 5 : 3 : 2. One is arch, a code hook on the builder: an arch standing on a floor with a sun or moon inside it. The others are orbs and split, built-ins with their own parameters. Each sits on a mat in the pale role (the frame finisher, given two layers), as a print hung in a gallery |

## Given palettes

`sleeve sheet --palette COLOURS --size 128 --columns 8 16 OUT.png`, in the default style.
A given palette takes the place of the mood's draws, so a seed's composition and geometry come
from a different point of its stream: each sheet shows sixteen covers of its own in an album's
colours, not the default sheet's layouts recoloured.

| Sheet | Given | Roles assigned (deep, mid, pale, accent, accent2) | What it shows |
| --- | --- | --- | --- |
| `palette-d62828.png` | one red | `#3d0a08 #c74f46 #fde0dc #d62828 #fe786c` | one colour becomes the accent; the four other roles are derived from it, so a single colour still gives a dark, a light and a middle |
| `palette-1d3557-f77f00.png` | navy, orange | `#1d3557 #c66f2e #fee1cf #f77f00 #b05903` | navy is dark enough to be deep and orange is the accent. Pale, mid and the second accent are derived from the orange, the most chromatic colour |
| `palette-000000-ffffff.png` | black, white | `#000000 #636363 #ffffff #a4a4a4 #747474` | without chroma, the derived roles stay grey. The covers are in greyscale, with no invented hue |
| `palette-264653-2a9d8f-e9c46a-f4a261-e76f51.png` | five colours | `#264653 #2a9d8f #e9c46a #e76f51 #f4a261` | five colours fill all five roles by lightness and chroma, with nothing derived |
| `gallery-palette-264653-2a9d8f-e9c46a-f4a261-e76f51.png` | the same five, in gallery | the same | a given palette takes the place of a style's moods but keeps its compositions and finishers: gallery's arches and mats, in the album's colours |

## Generation time

The default style's generation, before (`4569c8d`) and after this branch. The machine is an
AMD Ryzen 7 5800X3D (16 threads) with GCC 16.2.1 `-std=c99 -O2`. It was shared with other
work: the 1-minute load average was 2.7–6.7 throughout.

| Measure | Before | After |
| --- | --- | --- |
| `sleeve_generate` alone, 1,000,000 seeds, best of 7 runs, best of 5 rounds (`gentime.c`) | 554.7 ns a cover | 559.0 ns a cover |
| the same, CPU cycles and instructions for 3 × 1,000,000 seeds (`perf stat`) | 7.42 G cycles, 18.10 G instructions | 7.45 G cycles, 19.01 G instructions |
| `sleeve sheet --size 8 --gap 0 --columns 64 4096`, wall time, best of 7 | 126.8 ms | 127.3 ms |

After is 0.8% slower in generation alone and 0.5% in cycles, within the round-to-round
spread (554.7–561.4 ns before, 559.0–567.7 ns after). The sheet's time is the same, and the
two sheets are identical. The extra instructions come from the style lookups: picking
through the style's tables and checking whether its parts are built in. They mostly overlap
the work that was already there.
