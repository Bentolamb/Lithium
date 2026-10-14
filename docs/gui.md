# TurboWarp GUI + Lithium runtime

The editor UI is TurboWarp's scratch-gui (GPL-3.0, compatible with AGPL-3.0), pinned in
`gui/TURBOWARP_COMMIT`, patched by `gui/lithium-gui.patch` and built by `tools/build_gui.sh`.
Branding is Lithium; the TurboWarp feedback button is removed.

## How execution is swapped (web/gui_bridge.js)
The GUI keeps its scratch-vm for editing, serialization, costumes/sounds and monitors.
On green flag the bridge serializes the project (`vm.toJSON()`), loads it into the WASM runtime,
hands Lithium the VM's own costume skin ids and draws with the VM's renderer. VM drawables are hidden
while a session runs. Overrides: `runtime.greenFlag`, `runtime.stopAll`, `runtime._step`,
`vm.postIOData` (keys, mouse), `vm.startDrag` (disabled while running).
Stop copies Lithium state (position, direction, size, costume, effects, variables, lists) back into the VM,
then releases Lithium's drawables. Monitors are refreshed every 6 frames from Lithium state.
If Lithium fails to load or start, the bridge falls back to the built-in VM.

## Known limits
- Sessions start only on the green flag (key/click hats do not fire while idle).
- Editing blocks during a run does not affect the running session until the next flag.
- No sprite dragging while a session runs; ask/answer UI, TurboWarp-only blocks and extensions are not routed.
- Test: `node tests/web/gui_test.js` (needs the GUI build; 9 checks).
