#!/usr/bin/env bash
# Windows installer (NSIS wizard) from the GUI build. Run tools/build_gui.sh first.
# Needs node/npm; downloads Electron and NSIS from GitHub. Works on Linux without wine.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"; cd "$here/desktop"
rm -rf gui && cp -r "${1:-$here/build-gui/scratch-gui/build}" gui
npm install --no-audit --no-fund
npx electron-builder --win dir --x64          # makes dist/win-unpacked (the app itself)
NS="$HOME/.cache/electron-builder/nsis/nsis-3.0.4.1"
( cd installer && NSISDIR="$NS" "$NS/linux/makensis" -V2 -DVERSION="$(node -p "require('../package.json').version")" lithium.nsi )
ls -la dist/*.exe
