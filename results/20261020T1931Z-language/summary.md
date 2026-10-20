# sleeve's language, measured

The rule, set before measuring: sleeve is Haskell only if, on every host measured, Haskell
generates and rasterizes within 1.25× of C's time; otherwise C, and production keeps one.

The work on every host: covers for seeds 0–999, each generated and rendered at 64, 216 and
512 px, after 20 warm-up covers, the two languages taking turns cover by cover in one process
(`bench/`, `sleeve-bench bench`). Haskell is a port written for speed: strict loops over unboxed
arrays that allocate nothing per pixel (5–10 KB a cover, all in generating and preparing the
description). On every host both languages wrote the same descriptions and identical rasters
(largest difference 0, and `sleeve-bench check` passing natively and under WAMR on the phone).

| Host | Size | C, 1,000 covers | Haskell, 1,000 covers | Haskell ÷ C, total | Haskell ÷ C, median | Largest difference |
| --- | --- | --- | --- | --- | --- | --- |
| Desktop, native | 64 | 0.17 s | 0.27 s | 1.531 | 1.389 | 0 |
| Desktop, native | 216 | 1.41 s | 1.82 s | 1.29 | 1.137 | 0 |
| Desktop, native | 512 | 6.89 s | 8.29 s | 1.203 | 1.168 | 0 |
| Desktop, Helium (WebAssembly) | 64 | 0.22 s | 0.48 s | 2.224 | 2.286 | 0 |
| Desktop, Helium (WebAssembly) | 216 | 1.70 s | 3.74 s | 2.201 | 2.273 | 0 |
| Desktop, Helium (WebAssembly) | 512 | 8.35 s | 18.43 s | 2.208 | 2.256 | 0 |
| Note 10+, WAMR AOT, big cores | 64 | 0.33 s | 0.99 s | 3.011 | 3.268 | 0 |
| Note 10+, WAMR AOT, big cores | 216 | 2.65 s | 8.24 s | 3.107 | 3.498 | 0 |
| Note 10+, WAMR AOT, big cores | 512 | 13.71 s | 36.41 s | 2.656 | 2.793 | 0 |
| Note 10+, WAMR AOT, unpinned | 64 | 0.37 s | 1.05 s | 2.804 | 3.034 | 0 |
| Note 10+, WAMR AOT, unpinned | 216 | 3.05 s | 8.62 s | 2.828 | 3.084 | 0 |
| Note 10+, WAMR AOT, unpinned | 512 | 19.25 s | 49.52 s | 2.573 | 2.562 | 0 |

C alone, native (`c/bench.c`), for the same 1,000 covers at 64 / 216 / 512 px:

| Host | 64 px | 216 px | 512 px | Median at 512 px |
| --- | --- | --- | --- | --- |
| Desktop, gcc -O2 | 0.17 s | 1.40 s | 6.83 s | 6.29 ms |
| Note 10+, NDK clang -O2, big cores | 0.27 s | 2.14 s | 10.78 s | 9.62 ms |
| Note 10+, NDK clang -O2, unpinned | 0.61 s | 4.39 s | 19.07 s | 17.08 ms |

**Decision: C.** Haskell misses the rule on every host: by a little natively on the desktop at
the smaller sizes (1.29–1.53×), and by 2.2× in the browser and 2.6–3.1× on the phone, where
GHC's WebAssembly code generator meets clang's. The library is `sleeve.h`; the Haskell port stays
in `bench/` as the measurement's comparison only.

Determinism across platforms (`cross-platform.txt`): C's rasters are byte-identical natively on
the desktop, in the browser and under WAMR on the phone; native C on the phone, built by a
compiler that fuses multiplies and adds, is within one level (a few hundred channels in millions).
The checks pass natively on the phone (`check-android-native.txt`).

Files: `native.json` and `native-c.json` (desktop, with `load.txt`), `web-helium.json`,
`android-*.json` (with `device.jsonl`), `check-*.txt`, `machine.json`.
