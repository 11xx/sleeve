# Changelog

A release is named by its date, `YYYY.M.D`; `SLEEVE_VERSION` in `sleeve.h`, `sleeve --version`
and the newest heading here agree, and `make check` holds them to it.

## 2026.10.25

The first release of sleeve as a repository of its own, with the history of its development.

### Added

- `sleeve.h`: covers generated from a 64-bit seed (`SLEEVE_GENERATOR` 1), rasterized at any size
  from 1 to 16384 a side, in whole or by rows, and written as SVG and PNG, with no allocation,
  no global state and no dependency beyond the C standard library.
- Styles: moods, built-in compositions, palette hooks and finishers, a builder for compositions
  of one's own, a palette of 1 to 16 colours given for one call, `sleeve_style_check` and
  `sleeve_style_hash`.
- `SLEEVE_VERSION`, the release, for a copy of the header kept in another project to name.
- The command-line tool (`tools/sleeve.c`): `cover`, `sheet`, `describe`, `seed`, `styles`,
  `pieces` and `--version`.
- Checks (`make check`, `faults`, `sanitize`, `sanitize-faults`, `fuzz`, `freestanding`), the
  browser demo, and the language measurement in `bench/` with its results.
