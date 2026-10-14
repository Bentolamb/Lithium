# Lithium

A standalone Scratch-compatible visual programming environment with a C++ runtime.
License: AGPL-3.0 (see `LICENSE`).

## Status
Milestone 1 (in progress): repo skeleton, native C++ runtime stub, C API, tests.

| Piece | State |
|---|---|
| C++ runtime (`runtime/`) | stub only: version and frame counter |
| C API (`runtime/include/lithium/c_api.h`) | working, tested natively |
| Native tests (Catch2) | passing |
| WASM build (Emscripten 6.0.12) | working; built and smoke-tested from Node on Windows 11 (see below) |
| Editor, SB3 loader, TXT export | not started |

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
node tests/wasm_smoke.js build-wasm/lithium_wasm.js
```
Note: a `uint64_t` return crosses the WASM boundary as a BigInt. Prefer 32-bit
ints or doubles in hot-path APIs.

## Layout
- `runtime/`: the C++ runtime (no platform dependencies).
- `tests/`: runtime tests.
- `docs/`: design notes.
