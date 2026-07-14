#!/usr/bin/env bash
# sundog: idempotent asset fetcher.
# Downloads "Spot" (Keenan Crane's cartoon cow, CC0 1.0) into assets/spot.obj
# (+ texture atlas into scenes/textures/spot_texture.png) and the sunny-sky
# HDRI "Kloofendal 48d Partly Cloudy (Pure Sky)" (Poly Haven, CC0 1.0) into
# assets/. Safe to re-run: each section exits early if its asset is valid.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/assets"
OBJ="$ASSETS/spot.obj"
TEX="$ROOT/scenes/textures/spot_texture.png"
MIN_TRIS=4000
URL="https://www.cs.cmu.edu/~kmcrane/Projects/ModelRepository/spot.zip"

HDR="$ASSETS/kloofendal_48d_partly_cloudy_puresky_4k.hdr"
HDR_URL="https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_puresky_4k.hdr"
HDR_MIN_BYTES=10000000

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

valid_hdr() {
  # Radiance magic + expected 4k equirect resolution + plausible size.
  [ -f "$1" ] || return 1
  head -c 11 "$1" | grep -aq '^#?RADIANCE' || return 1
  head -c 256 "$1" | grep -aq -- '-Y 2048 +X 4096' || return 1
  [ "$(stat -c %s "$1")" -ge "$HDR_MIN_BYTES" ]
}

# ---- Spot ------------------------------------------------------------------
if valid_obj "$OBJ" && [ -s "$TEX" ]; then
  echo "[fetch-assets] $OBJ ($(grep -c '^f ' "$OBJ") faces) and $TEX already present."
else
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
fi

# ---- sky HDRI ----------------------------------------------------------------
if valid_hdr "$HDR"; then
  echo "[fetch-assets] $HDR ($(stat -c %s "$HDR") bytes) already present."
else
  echo "[fetch-assets] fetching $HDR_URL"
  curl -fSL --retry 3 --connect-timeout 20 -o "$HDR.part" "$HDR_URL"
  if ! valid_hdr "$HDR.part"; then
    echo "[fetch-assets] ERROR: downloaded HDR failed validation (Radiance magic / 4096x2048 / >= $HDR_MIN_BYTES bytes)" >&2
    rm -f "$HDR.part"
    exit 1
  fi
  mv "$HDR.part" "$HDR"
  echo "[fetch-assets] wrote $HDR ($(stat -c %s "$HDR") bytes)"
fi
