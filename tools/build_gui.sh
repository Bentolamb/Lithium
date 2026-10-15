#!/usr/bin/env bash
# Builds the TurboWarp-based Lithium GUI.  Needs node/npm, git and a built build-wasm/.
#   tools/build_gui.sh [workdir]      -> <workdir>/scratch-gui/build/
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
work="${1:-$here/build-gui}"
mkdir -p "$work" && cd "$work"
if [ ! -d scratch-gui ]; then
  git clone https://github.com/TurboWarp/scratch-gui.git
  git -C scratch-gui checkout "$(cat "$here/gui/TURBOWARP_COMMIT")"
  git -C scratch-gui apply "$here/gui/lithium-gui.patch"
  (cd scratch-gui && npm install --legacy-peer-deps)
  # TurboWarp downloads this at build time; an empty stub is enough for Lithium
  mkdir -p scratch-gui/src/generated && echo 'module.exports = "";' > scratch-gui/src/generated/microbit-hex-url.cjs
fi
cd scratch-gui
mkdir -p src/lib/lithium static/lithium
cp "$here"/web/{lithium.js,stage.js,gui_bridge.js,extensions.js,fallback.js} src/lib/lithium/
cp "$here"/web/logo.svg static/lithium/logo.svg
cp "$here"/web/logo.png src/components/menu-bar/lithium_logo.png
cp "$here"/build-wasm/lithium_wasm.{js,wasm} static/lithium/
NODE_ENV=production NODE_OPTIONS=--max-old-space-size=6000 npx webpack --bail
echo "GUI built: $PWD/build/editor.html"
