#!/usr/bin/env bash
# Fetch official TurboWarp upstreams into a disposable workspace.
# This does NOT alter Lithium source files or install dependencies.
set -Eeuo pipefail

command -v git >/dev/null 2>&1 || { echo "ERROR: git is required" >&2; exit 1; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace="${1:-"$repo_root/.upstream-turbowarp"}"

if [[ -e "$workspace" && ! -d "$workspace" ]]; then
  echo "ERROR: workspace is not a directory: $workspace" >&2
  exit 1
fi
mkdir -p "$workspace"
workspace="$(cd "$workspace" && pwd)"

for name in scratch-gui scratch-vm scratch-render; do
  dest="$workspace/$name"
  url="https://github.com/TurboWarp/$name.git"
  if [[ -e "$dest" ]]; then
    if [[ ! -d "$dest/.git" ]]; then
      echo "ERROR: $dest exists but is not a git checkout" >&2
      exit 1
    fi
    actual="$(git -C "$dest" remote get-url origin)"
    if [[ "$actual" != "$url" && "$actual" != "git@github.com:TurboWarp/$name.git" ]]; then
      echo "ERROR: unexpected origin for $name: $actual" >&2
      exit 1
    fi
    echo "Using existing checkout: $dest (no automatic pull)"
  else
    echo "Cloning $url"
    git clone "$url" "$dest"
  fi
done

manifest="$workspace/UPSTREAM_COMMITS.txt"
{
  echo "# Official TurboWarp upstream revisions"
  for name in scratch-gui scratch-vm scratch-render; do
    printf '%s %s\n' "$name" "$(git -C "$workspace/$name" rev-parse HEAD)"
  done
} > "$manifest"
echo "Wrote $manifest"
echo "Next: inspect each checkout's README, package.json and lockfile before installing or building."
