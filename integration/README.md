# TurboWarp × Lithium: shadow runtime developer preview

This milestone builds the **official TurboWarp editor and JavaScript VM**, then
loads a separate Lithium C++/WebAssembly runtime alongside it. **TurboWarp remains
authoritative** for all visible Scratch project behavior. The Wasm engine
executes a copy of the project only for comparison.

## Running the preview

The GitHub Actions workflow `turbowarp-shadow.yml` builds the combined website,
tests the Wasm C API, runs JavaScript integration tests, verifies the package,
and launches Chromium to smoke-test the editor and a real 1,000-iteration Scratch
loop. Successful runs publish:

- `turbowarp-lithium-shadow-editor`: complete extracted static site ZIP
- `lithium-shadow-browser-screenshot`: screenshot taken in hosted Chromium

Serve the extracted site **as the HTTP root**, for example:

```sh
python3 -m http.server 8080
```

Visit `http://localhost:8080/editor.html`. Do not open `file://` or serve
the site from a nested directory: the bridge assets use paths under
`/lithium/`. New builds use versioned asset URLs to avoid serving previously
cached bridge scripts from the same localhost address.

## In-editor diagnostics

The lower-left **Lithium shadow** chip opens a compact diagnostics panel.
After clicking the green flag, the panel displays execution status, shadow
frame count, and any final-state differences. It can re-run the comparison or
copy a JSON report. No developer console is needed for routine checks.

For more detail, open browser DevTools:

```js
LithiumShadow.version
LithiumShadow.compare()
LithiumShadow.lastComparison
LithiumShadow.snapshot()
```

Possible comparison statuses:

| Status | Meaning |
|---|---|
| `not-run` | The green flag has not been pressed in this session |
| `incomplete` | Both engines have not completed the current run |
| `match` | Both completed; compared final-state fields agree |
| `mismatch` | Both completed; reported fields disagree |
| `unsupported` | Lithium rejected the project or reported dropped/unknown opcodes |
| `aborted` | The run was stopped before completion |
| `error` | The shadow runtime threw an exception |

Only `match` with `match: true` indicates a successful final-state comparison.
A successful match does **not** establish exact timing, scheduling, event
semantics, rendering equivalence, or full Scratch compatibility.

The comparator checks scalar variables, lists, original/clone target counts,
and sprite position, direction, size, visibility, and costume index. It uses
relative numeric tolerance `1e-9` for finite numbers and strict string/boolean
equality. Duplicated variable names can be ambiguous because the current C++
JSON snapshot indexes values by name, and input timing is not synchronized.

## Remaining work

- Record and replay input events using synchronized virtual clocks and seeds
- Compare broadcasts, clones, sounds, sensing, pen effects, timing, and extensions
- Introduce a feature-gated runtime interface with proven Wasm execution and
  TurboWarp JavaScript fallback
- Preserve Lithium optimizations, Packin, OutList, and other established features
- Complete UI branding, project editing workflows, packaging and cross-platform
  release testing

This is a **developer preview**, not yet a completed Lithium application.
