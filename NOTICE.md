# Third-party code

Lithium is AGPL-3.0-or-later (see `LICENSE`). It includes or builds on:

- **TurboWarp scratch-gui** (editor), pinned in `gui/TURBOWARP_COMMIT`, with Lithium's changes in `gui/lithium-gui.patch`;
  `tools/build_gui.sh` fetches the pinned commit and applies the patch. TurboWarp's code is GPL-3.0-or-later and
  incorporates Scratch code under BSD-3-Clause; their license files come with that checkout.
- **scratch-vm, scratch-render, scratch-blocks** (Scratch Foundation / TurboWarp), pulled in through scratch-gui and
  vendored in `web/vendor/` for the standalone web shell. BSD-3-Clause / GPL-3.0 as published by those projects.
- **nlohmann/json** (`third_party/nlohmann`), MIT.
- **Electron** and **NSIS** are used to build the desktop app and installer and are not stored here.

"Scratch" is a trademark of the Scratch Foundation, "TurboWarp" of its owner; Lithium is not affiliated with either.
