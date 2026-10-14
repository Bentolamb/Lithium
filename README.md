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
| WASM build (Emscripten) | not built yet; Emscripten could not be downloaded in the dev sandbox |
| Editor, SB3 loader, TXT export | not started |

## Build and test (native)
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```
Requires CMake 3.20+, a C++20 compiler, and network access for Catch2 (fetched from GitHub).

## Layout
- `runtime/`: the C++ runtime (no platform dependencies).
- `tests/`: runtime tests.
- `docs/`: design notes.
