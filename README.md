# Lithium

A standalone Scratch-compatible visual programming environment with a C++ runtime.
License: AGPL-3.0 (see `LICENSE`).

## Status
Milestone 5: differential testing against real scratch-vm, plus operators, broadcasts, direction and list blocks. (M4 browser shell, M3 SB3 loader, M2 runtime core.)

| Piece | State |
|---|---|
| Value type and coercion (`value.h`) | ported from scratch-vm `Cast`, incl. JS `Number()`/`String()` quirks |
| Scheduler (`sequencer.cpp`, `thread.cpp`) | literal port of `stepThreads`/`stepThread`: 60/30 Hz frames, 75% work budget, redraw gating, turbo, warp timer, loop yields |
| Blocks (`primitives.cpp`) | green flag; repeat, forever, if, if/else, repeat until, while, wait, wait until, stop; set/change variable; operators (arithmetic, comparison, logic, join, letter of, length, contains, mod, round, math ops, random); broadcast, broadcast and wait, when I receive; list blocks (add, delete, insert, replace, item, item #, length, contains, contents, delete all); move steps, x/y set/change, go to x y, turn left/right, point in direction, x/y/direction reporters |
| SB3 loader (`sb3.cpp`) | reads `project.json`: targets, layers, variables, block graph, inputs (shadows, obscured shadows, inline variable reporters), fields. Unknown opcodes load with a warning and run as no-ops |
| `tools/run_sb3.js` | Node runner: unzips an `.sb3`, runs it in the WASM runtime, prints final variables and positions |
| Web shell (`web/`) | open or drop an `.sb3`, Go/Stop, turbo, 30 fps mode, stage canvas (sprites as markers), live variables and sprite table, load log. A runner, not an editor yet: no block editing, costumes or sounds |
| C API | create/step/green flag/stop, built-in demo projects, variable and position readback, manual clock |
| Native tests (Catch2) | passing |
| WASM build (Emscripten 6.0.12) | working on Windows 11 |
| Editor, SB3 loader, TXT export | not started |

### Known gaps
- No custom blocks/procedures, clones, or most other hat types yet.
- Reporters are evaluated synchronously; an unknown reporter evaluates to `""` (stands in for JS `undefined`).
- Sprites are not fenced to the stage (needs renderer bounds).
- `sin/cos/tan` may differ from V8 by a few ulp (tolerated in the diff harness; a deterministic math layer is planned).
- Strings use UTF-16 semantics (length, letter of, comparison) and full Unicode case folding; the final-sigma rule is approximate.
- SB3: costumes/sounds are not loaded; the host unzips the `.sb3` and passes the `project.json` text.
- Fixtures in `tests/projects/` are the user's TurboWarp projects.

## Differential testing
`tests/diff/` generates `project.json` programs, runs them in scratch-vm 5.0.300 (headless, Node) and in `lithium_run`, and compares variables, lists, positions, directions and thread counts.
```
cd tests/diff && npm install && node run_diff.js        # --only <substring> filters cases
```
Current: 44/44 cases identical (17k values; max libm difference 2 ulp).

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
