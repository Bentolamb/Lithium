# TurboWarp/Lithium shadow runtime

This milestone builds **the unmodified TurboWarp GUI and JavaScript VM**, then
injects a separate Lithium WebAssembly instance into the generated static site.
The JavaScript VM remains authoritative. The shadow currently mirrors green flag,
stop-all, and runtime steps for diagnostics only.

## Preview

GitHub Actions workflow: `turbowarp-shadow.yml`. The artifact
`turbowarp-lithium-shadow-editor` contains a ZIP of the complete static website
and `turbowarp-source-sha.txt` for provenance.

Serve the **extracted site directory as the HTTP root**, for example:

```sh
python3 -m http.server 8080
```

Then visit `http://localhost:8080/`. The WASM files are served from
`/lithium/`. Opening with `file://` or serving from a nested subdirectory will
not work.

When the editor loads, browser DevTools provides `window.LithiumShadow`
with `mode`, `frames`, `loadLog`, `lastError`, and
`snapshot()` (when attached). These are diagnostic data, not a parity result.

## Important limitations

- This is **not** a replacement VM, full TurboWarp fork, or evidence of
  execution parity. The two schedulers do not have synchronized virtual clocks.
- Unsupported Lithium opcodes may load as no-ops. Check `loadLog`.
- Input events, clones, extensions, storage, and renderer-side effects are not
  mirrored. Scratch results continue to come entirely from TurboWarp.
- A missing runtime hook produces `ready-no-step-hook`. Project/load failures
  are surfaced in status and console without blocking the original editor.
- Upstream TurboWarp remains untouched; the integration takes place in the
  generated site, allowing each engine to be tested independently.

Next: add deterministic differential execution with input/clock synchronization,
then selectively dispatch proven opcode families to Wasm with JS fallback.
