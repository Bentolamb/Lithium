# Lithium

A standalone Scratch-compatible visual programming environment with a C++ runtime.
License: AGPL-3.0 (see `LICENSE`).

## Status
Milestone 4: minimal browser shell (open/run an `.sb3`, stage view, variables) on top of the M2/M3 runtime.

| Piece | State |
|---|---|
| Value type and coercion (`value.h`) | ported from scratch-vm `Cast`, incl. JS `Number()`/`String()` quirks |
| Scheduler (`sequencer.cpp`, `thread.cpp`) | literal port of `stepThreads`/`stepThread`: 60/30 Hz frames, 75% work budget, redraw gating, turbo, warp timer, loop yields |
| Blocks (`primitives.cpp`) | green flag; repeat, forever, if, if/else, repeat until, while, wait, wait until, stop; set/change variable; arithmetic, comparison, and/or/not, join; move steps, x/y set/change, go to x y, x/y/direction reporters |
| SB3 loader (`sb3.cpp`) | reads `project.json`: targets, layers, variables, block graph, inputs (shadows, obscured shadows, inline variable reporters), fields. Unknown opcodes load with a warning and run as no-ops |
| `tools/run_sb3.js` | Node runner: unzips an `.sb3`, runs it in the WASM runtime, prints final variables and positions |
| Web shell (`web/`) | open or drop an `.sb3`, Go/Stop, turbo, 30 fps mode, stage canvas (sprites as markers), live variables and sprite table, load log. A runner, not an editor yet: no block editing, costumes or sounds |
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
- SB3: lists, broadcasts, costumes/sounds and shadow menu blocks are not loaded; the host unzips the `.sb3` and passes the `project.json` text.
- Fixtures in `tests/projects/` are the user's TurboWarp projects (`.sb3` plus the extracted `project.json` used by the tests).

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

## Web shell
After building WASM (`build-wasm/`), from the repo root:
```
node tools/serve.js
```
then open http://localhost:8080/web/ . `Enter` = Go, `Esc` = Stop.
`node tests/web/ui_test.js` drives the page in headless Chromium (needs `playwright-core`);
it uses a mock WASM module by default and `--real` to test against `build-wasm/`.

## Run an .sb3
```
node tools/run_sb3.js tests/projects/count.sb3 --wasm build-wasm/lithium_wasm.js
node tools/run_sb3.js tests/projects/timing.sb3 --seconds 3
node tools/run_sb3.js tests/projects/slide.sb3 --renderer --frames 11
```
Options: `--seconds N` (default 5), `--frames N`, `--renderer`, `--turbo`, `--compat`.

## Layout
- `runtime/`: the C++ runtime (no platform dependencies).
- `tests/`: runtime tests; `tests/projects/`: SB3 fixtures.
- `web/`: browser shell. `tools/`: Node SB3 runner and static server.
- `third_party/`: vendored nlohmann/json (MIT).
- `docs/`: design notes.

## Third-party code
- nlohmann/json 3.11.3 (MIT), `third_party/nlohmann/json.hpp`.
- Catch2 3.5.4 (BSL-1.0), fetched at configure time for native tests only.
