# Renderer decision (M7)

**Choice: reuse scratch-render (AGPL-3.0, WebGL) behind a C++ `Host` interface.**

| Option | Fidelity | Cost | Verdict |
|---|---|---|---|
| scratch-render, unchanged | Pixel-identical to Scratch/TurboWarp: SVG/bitmap skins, effects shaders, touching/colour queries, pen skin, text bubbles | Needs JS + WebGL; native desktop gets it through Electron's Chromium | **Chosen** |
| Own C++ renderer (OpenGL ES/Skia) | Would need to re-derive CPU hit-testing and every effect shader exactly | Months; fidelity bugs show up as wrong `touching` results | Later, behind the same `Host` |
| Canvas2D | Cannot do fisheye/whirl/mosaic/colour effects or colour-touching reliably | Cheap | No |

The runtime never touches pixels. `runtime/include/lithium/host.h` mirrors the subset of
scratch-render scratch-vm uses (drawables, skins, order, geometry queries, pen, bubbles) plus
sound/ask requests, so each ported block is a line-for-line translation of scratch-vm's.
`c_api.h` flattens the interface into one callback (`op` + 4 doubles + pointer) so the WASM boundary
stays at int32/double. A runtime without a host behaves like scratch-vm without a renderer
(verified by the differential tests: 172 cases).

Browser glue lives in `web/stage.js`; scratch-render is vendored as `web/vendor/scratch-render.js`
(from the npm package, AGPL-3.0 like the rest of Lithium).

Tested: `tests/host_test.cpp` (SimHost: keys, mouse, ask, bubbles, sound completion, touching, pen) and
`tests/web/browser_test.js` (Chromium + WASM: SVG costumes drawn, layering, pen line, speech bubble,
sprite-vs-sprite touching through the GPU, sound-until-done).

Not done yet: sprite dragging, monitors (variable watchers), video sensing, loudness, text-to-speech,
screenshots compared pixel-for-pixel against scratch-vm + scratch-render (same renderer code, so low risk).
