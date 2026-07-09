#!/usr/bin/env bash
# sundog: idempotent asset fetcher.
# Downloads the Stanford bunny as OBJ into assets/bunny.obj.
# Safe to re-run: exits early if a valid bunny.obj is already present.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/assets"
OBJ="$ASSETS/bunny.obj"
MIN_TRIS=4000

mkdir -p "$ASSETS"

valid_obj() {
  # A plausible OBJ with enough triangles?
  [ -f "$1" ] || return 1
  grep -qm1 '^v ' "$1" || return 1
  local nf
  nf=$(grep -c '^f ' "$1" || true)
  [ "$nf" -ge "$MIN_TRIS" ]
}

if valid_obj "$OBJ"; then
  echo "[fetch-assets] $OBJ already present ($(grep -c '^f ' "$OBJ") faces) — nothing to do."
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fetch_mcguire() {
  # McGuire Computer Graphics Archive — high-res Stanford bunny (~70k faces).
  local url="https://casual-effects.com/g3d/data10/research/model/bunny/bunny.zip"
  echo "[fetch-assets] trying $url"
  curl -fSL --retry 3 --connect-timeout 20 -o "$TMP/bunny.zip" "$url" || return 1
  ( cd "$TMP" && unzip -o -q bunny.zip ) || return 1
  # Pick the OBJ with the most faces from the archive.
  local best="" best_nf=0 f nf
  while IFS= read -r -d '' f; do
    nf=$(grep -c '^f ' "$f" || true)
    if [ "$nf" -gt "$best_nf" ]; then best="$f"; best_nf="$nf"; fi
  done < <(find "$TMP" -iname '*.obj' -print0)
  [ -n "$best" ] || return 1
  cp "$best" "$OBJ.part"
}

fetch_github() {
  # Fallback: alecjacobson/common-3d-test-models (research test-model mirror).
  local url="https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj"
  echo "[fetch-assets] trying $url"
  curl -fSL --retry 3 --connect-timeout 20 -o "$OBJ.part" "$url" || return 1
}

ok=0
for src in fetch_mcguire fetch_github; do
  rm -f "$OBJ.part"
  if "$src" && valid_obj "$OBJ.part"; then ok=1; break; fi
  echo "[fetch-assets] source $src failed or produced an invalid OBJ, trying next..."
done

if [ "$ok" -ne 1 ]; then
  echo "[fetch-assets] ERROR: could not obtain a valid bunny.obj (>= $MIN_TRIS faces)" >&2
  rm -f "$OBJ.part"
  exit 1
fi

mv "$OBJ.part" "$OBJ"
echo "[fetch-assets] wrote $OBJ ($(grep -c '^f ' "$OBJ") faces, $(grep -c '^v ' "$OBJ") vertices)"
