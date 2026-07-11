#!/usr/bin/env bash
# sundog: idempotent asset fetcher.
# Downloads "Spot" (Keenan Crane's cartoon cow, CC0 1.0) into assets/spot.obj
# and its texture atlas into scenes/textures/spot_texture.png.
# Safe to re-run: exits early if valid assets are already present.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/assets"
OBJ="$ASSETS/spot.obj"
TEX="$ROOT/scenes/textures/spot_texture.png"
MIN_TRIS=4000
URL="https://www.cs.cmu.edu/~kmcrane/Projects/ModelRepository/spot.zip"

mkdir -p "$ASSETS" "$ROOT/scenes/textures"

valid_obj() {
  # A plausible OBJ with enough triangles and texture coordinates?
  [ -f "$1" ] || return 1
  grep -qm1 '^v ' "$1" || return 1
  grep -qm1 '^vt ' "$1" || return 1
  local nf
  nf=$(grep -c '^f ' "$1" || true)
  [ "$nf" -ge "$MIN_TRIS" ]
}

if valid_obj "$OBJ" && [ -s "$TEX" ]; then
  echo "[fetch-assets] $OBJ ($(grep -c '^f ' "$OBJ") faces) and $TEX already present — nothing to do."
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "[fetch-assets] fetching $URL"
curl -fSL --retry 3 --connect-timeout 20 -o "$TMP/spot.zip" "$URL"
( cd "$TMP" && unzip -o -q spot.zip )

SRC_OBJ="$TMP/spot/spot_triangulated.obj"
SRC_TEX="$TMP/spot/spot_texture.png"
if ! valid_obj "$SRC_OBJ"; then
  echo "[fetch-assets] ERROR: $SRC_OBJ missing or invalid (>= $MIN_TRIS faces with vt expected)" >&2
  exit 1
fi
[ -s "$SRC_TEX" ] || { echo "[fetch-assets] ERROR: texture missing in archive" >&2; exit 1; }

cp "$SRC_OBJ" "$OBJ.part" && mv "$OBJ.part" "$OBJ"
cp "$SRC_TEX" "$TEX.part" && mv "$TEX.part" "$TEX"
echo "[fetch-assets] wrote $OBJ ($(grep -c '^f ' "$OBJ") faces, $(grep -c '^vt ' "$OBJ") texcoords)"
echo "[fetch-assets] wrote $TEX"
