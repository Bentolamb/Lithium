# Lithium

A standalone Scratch-compatible visual programming environment with a C++ runtime.
License: AGPL-3.0 (see `LICENSE`).

## Status
Milestone 2: Scratch value semantics, thread scheduler and a first block set, tested natively and in WASM.

| Piece | State |
|---|---|
| Value type and coercion (`value.h`) | ported from scratch-vm `Cast`, incl. JS `Number()`/`String()` quirks |
| Scheduler (`sequencer.cpp`, `thread.cpp`) | literal port of `stepThreads`/`stepThread`: 60/30 Hz frames, 75% work budget, redraw gating, turbo, warp timer, loop yields |
| Blocks (`primitives.cpp`) | green flag; repeat, forever, if, if/else, repeat until, while, wait, wait until, stop; set/change variable; arithmetic, comparison, and/or/not, join; move steps, x/y set/change, go to x y, x/y/direction reporters |
| C API | create/step/green flag/stop, built-in demo projects, variable and position readback, manual clock |
| Native tests (Catch2) | passing |
| WASM build (Emscripten 6.0.12) | working on Windows 11 |
| Editor, SB3 loader, TXT export | not started |

### Known gaps (M2)
- Case-insensitive comparison folds ASCII letters only.
- No custom blocks/procedures, clones, broadcasts, or other hat types yet.
- Reporters are evaluated synchronously; an unknown reporter evaluates to `""` (stands in for JS `undefined`).
- Sprites are not fenced to the stage (needs renderer bounds).
- `Math.sin/cos` may differ from V8 by 1 ulp.
- Projects can only be built in C++ or loaded from the built-in demos until the SB3 loader (M4).

## Build and test (native)
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```
Requires CMake 3.20+, a C++20 compiler, and network access for Catch2 (fetched from GitHub).

## Build and test (WASM, Windows)
Verified with emsdk 6.0.12 and Ninja 1.13.2 (`pip install --user ninja`):
```
call C:\dev\emsdk\emsdk_env.bat
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
node tests/wasm_smoke.js build-wasm/lithium_wasm.js   # runs the C API against built-in projects
```
Note: a `uint64_t` return crosses the WASM boundary as a BigInt, so the C API
uses 32-bit ints and doubles.

## Layout
- `runtime/`: the C++ runtime (no platform dependencies).
- `tests/`: runtime tests.
- `docs/`: design notes.
