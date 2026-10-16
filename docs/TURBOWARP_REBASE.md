# TurboWarp-first Lithium rebuild

This branch is for a **TurboWarp-first** rebuild. The current Lithium runtime is a feature reference, **not** the baseline for Scratch behavior.

## Non-negotiable constraints

- Keep TurboWarp's editor, renderer, audio, project loading and JavaScript VM working as a known-good reference while porting execution.
- Do not claim that compiling the C++ runtime to WebAssembly automatically compiles arbitrary JavaScript or Scratch scripts to Wasm.
- Port Scratch semantics against the actual TurboWarp/scratch-vm implementation. Compare observable state and event ordering, including yields, warp mode, clones, lists, casts, broadcasts, timers and extensions.
- Retain current Lithium capabilities, including Packin, OutList and any SmartRepeat/procedure/SIMD code that is **actually present** on the audited revision. Treat undocumented or absent features as work items, not completed work.
- Keep `main` untouched until tests and review.

## Upstream sources

Use the official TurboWarp repositories as upstreams:

- https://github.com/TurboWarp/scratch-gui
- https://github.com/TurboWarp/scratch-vm
- https://github.com/TurboWarp/scratch-render

Record immutable commit SHAs after cloning, then build the unmodified editor/VM before changing runtime code. Follow the build instructions and supported Node version in each checked-out upstream revision; do not guess package-manager commands.

## First executable milestone

1. Clone official upstreams into a separate, ignored workspace.
2. Record upstream SHAs and licenses.
3. Install dependencies using each upstream's actual lockfile and documented commands.
4. Run unmodified TurboWarp and its tests.
5. Establish differential fixtures with expected output from the reference VM.
6. Add a runtime-selection interface only after reference tests pass.
7. Port small execution slices to C++/Wasm with JavaScript fallback, comparing results at each step.

## Environment limitation

The initial coding container cannot resolve github.com for `git clone`. The GitHub connector can write files to this branch but does not provide a checkout/build environment. **No upstream clone, build or test has yet been completed.** Run the bootstrap script in an environment with GitHub network access, or use ChatGPT Work's cloud computer.

## Compatibility gates

For each ported opcode or runtime feature: golden fixture, differential comparison with TurboWarp, failure-mode coverage, benchmark, and fallback path. Do not optimize before correctness.

## Feature inventory (audit required)

- Current Lithium C++ runtime, SB3 loader, C API and browser shell
- Packin / OutList / storage interfaces
- SmartRepeat, inlining, recursion and SIMD paths: verify actual repository state before claiming parity
- Editor, costumes, sound, extensions, clone behavior and project serialization from TurboWarp
