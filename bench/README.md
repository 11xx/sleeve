# sleeve-bench

The measurement that chose sleeve's language: `sleeve.h`'s generator and rasterizer beside a
Haskell port of both, compared and timed on the same seeds. The Haskell port is not a second
implementation of sleeve; it exists to be measured against the C.

- `src/Sleeve/` is the port: `Generate` follows `sleeve_generate` draw for draw, `Raster`
  follows `sleeve_render` operation for operation, as strict loops over unboxed arrays that
  allocate nothing per pixel. `cbits/bench.c` compiles `sleeve.h` into the same program.
- `sleeve-bench check` compares the two languages' descriptions for 20,005 seeds and rasters
  at 64, 216 and 512 px, and shows that the comparison catches a neighbouring seed's description
  and a raster two levels off in one byte.
- `sleeve-bench bench [--seeds N] [--sizes PX,...]` times generating and rendering each cover in
  each language, the two taking turns, and prints JSON: per size and language the total, mean,
  median, p95, p99 and largest times, the bytes Haskell allocated a cover, the largest difference
  between the languages' rasters, and a digest of C's rasters that is equal across hosts when
  their bytes are.
- `c/bench.c` times C alone with the same statistics, for hosts that run native code.

| Runner | Host |
| --- | --- |
| `run-native.sh RESULTS_DIR` | the desktop, natively, and C alone built by the system compiler |
| `run-browser.sh RESULTS_DIR [LABEL]` | a headless Chromium-family browser (`CHROMIUM`), cross-origin isolated, fresh profile |
| `run-android.sh RESULTS_DIR` | an Android device: WAMR compiled ahead of time for both languages, and C alone from the NDK |

Building needs GHC 9.12 and cabal (`cabal build exe:sleeve-bench`), and for WebAssembly
ghc-wasm-meta's `wasm32-wasi-cabal --builddir=dist-newstyle/wasm build exe:sleeve-bench`. Each
runner's header names what else it needs.
